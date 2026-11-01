// See StageExport.hpp. The paths and member names in quotes are the ROM's own names (data
// literals). The opening keeps its own exporter (SceneExport.cpp), whose output is validated
// against the original frame by frame; this exporter follows the same package format with the
// generic parts only.
#include "StageExport.hpp"

#include "Animation.hpp"
#include "ExportCommon.hpp"
#include "SceneExport.hpp"
#include "Shaders.hpp"
#include "Texture.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace captain_toad {

namespace fs = std::filesystem;
using json::Value;
using namespace exporter;

namespace {

const char* const kLuts = "ObjectData/LutCurveData.szs";
const char* const kProtagonistArchive = "ObjectData/Kinopio.szs";
const char* const kProtagonistMiniArchive = "ObjectData/KinopioMini.szs";
const char* const kProtagonistAnimArchive = "ObjectData/KinopioAnimation.szs";
const char* const kHeadlight = "ObjectData/PlayerHeadlight.szs";

// The protagonist's clips the recovered player uses (as in the opening's player.json).
const char* const kPlayerClips[] = {
    "Wait", "WalkSlowly", "Walk", "WalkSpeedy", "Run", "TurnAround", "RunTurn", "Fall", "FallPanic", "Land",
    "LandPanic", "PullOutSign", "RunSign", "Turn", "WaitRuck", "WaitRove", "WaitYawn", "WaitSitDown", "WaitSleepLoop",
    "Shout", "Sneeze",
    // Phase 2 (PullUpStage): pulling, ladders, the miss.
    "PullOutStart", "PullOutSuccess", "PullOutSuccessQuick", "PullOutSuccessCarry", "PullOutMiss", "PullOutMissWait",
    "LadderClimbUp", "LadderClimbDown", "LadderClimbWait", "LadderClimbUpDash", "LadderClimbDownDash", "DieOver",
    // Carrying (the carry controller's table 0x480720: kinds 0 front, 1 / 3 up).
    "CarryFrontStart", "CarryFront", "CarryFrontThrow", "CarryUpStart", "CarryUp", "CarryUpThrow",
    // Power forms: the Mini variants the clip resolver 0x2c716c prefers ("Mini" + action), the
    // invincible pickaxe clip.
    "MiniFall", "MiniLadderClimbUp", "MiniLadderClimbDown", "MiniLadderClimbWait", "MiniWaitCold", "MiniWaitFear",
    "Invincible", "MiniInvincible",
    // The goal demo (goal object init 0x1ec824: solo or pair poses; Mini variants by the resolver).
    "GoalPoseSign", "GoalPose", "GoalPosePairSign", "GoalPosePair", "MiniGoalPoseSign", "MiniGoalPose",
    // Phase 2 (RevolverRuinsStage): a player bind's clip (PlayerBindAnimObj PlayerAnimName) and the
    // walk in water (ActionAnimCtrl MoveWater and its neighbours).
    "FallSign", "WaitWater", "WalkWaterSlowly", "WalkWater", "WalkWaterSpeedy", "RunWater", "TurnWater"};

// Units whose init loads an archive of another name (code literals): HeyhoRail 0x3a7a8c "Heyho",
// KinopioMemberNpc 0x1a4060 "KinopioMemberNPC", CoinChameleon units 0x16f8d4 "CoinChameleon",
// DoubleMario (the double cherry, init 0x152418) "DoubleItem".
const std::pair<const char*, const char*> kArchiveOfUnit[] = {{"HeyhoRail", "Heyho"},
                                                              {"KinopioMemberNpc", "KinopioMemberNPC"},
                                                              {"CoinTransparent", "CoinChameleon"},
                                                              {"DoubleMario", "DoubleItem"}};

// The placement lists of a stage map that hold objects (the map's other entries are file data).
const char* const kObjectLists[] = {"ObjectList", "GoalList", "DemoObjList", "Objs"};

std::string withoutStage(const std::string& name) {
    const std::string suffix = "Stage";
    if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return name.substr(0, name.size() - suffix.size());
    }
    return name;
}

// Row-major 3x4 affine matrices (the session's Affine convention: scale, then XYZ Euler
// rotation, then translation).
using M34 = std::array<double, 12>;

M34 trs(const double s[3], const double r[3], const double t[3]) {
    const double x = std::cos(r[0]), X = std::sin(r[0]), y = std::cos(r[1]), Y = std::sin(r[1]);
    const double z = std::cos(r[2]), Z = std::sin(r[2]);
    return {y * z * s[0],
            (X * Y * z - x * Z) * s[1],
            (x * Y * z + X * Z) * s[2],
            t[0],
            y * Z * s[0],
            (X * Y * Z + x * z) * s[1],
            (x * Y * Z - X * z) * s[2],
            t[1],
            -Y * s[0],
            X * y * s[1],
            x * y * s[2],
            t[2]};
}

M34 multiply(const M34& a, const M34& b) {
    M34 c{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            double v = j == 3 ? a[size_t(i * 4 + 3)] : 0;
            for (int k = 0; k < 3; ++k) {
                v += a[size_t(i * 4 + k)] * b[size_t(k * 4 + j)];
            }
            c[size_t(i * 4 + j)] = v;
        }
    }
    return c;
}

std::array<double, 3> apply(const M34& m, const double p[3], bool position) {
    std::array<double, 3> q{};
    for (int i = 0; i < 3; ++i) {
        q[size_t(i)] = position ? m[size_t(i * 4 + 3)] : 0;
        for (int k = 0; k < 3; ++k) {
            q[size_t(i)] += m[size_t(i * 4 + k)] * p[k];
        }
    }
    return q;
}

// The placement transform of a map object: Translate, Rotate (degrees) and Scale.
M34 placementMatrix(const Value& o) {
    double s[3], r[3], t[3];
    const char* axes[3] = {"X", "Y", "Z"};
    for (int k = 0; k < 3; ++k) {
        s[k] = o.has("Scale") ? o.at("Scale").at(axes[k]).num() : 1.0;
        r[k] = (o.has("Rotate") ? o.at("Rotate").at(axes[k]).num() : 0.0) * M_PI / 180.0;
        t[k] = o.has("Translate") ? o.at("Translate").at(axes[k]).num() : 0.0;
    }
    return trs(s, r, t);
}

Value matrixJson(const M34& m) {
    Value v = Value::array();
    for (double x : m) {
        v.push(x);
    }
    return v;
}

// A placement axis-aligned box contains the point (areas: unit cube of 1000 scaled, base at
// Translate.Y for AreaCubeBase).
bool areaContains(const Value& a, const double p[3]) {
    const Value& t = a.at("Translate");
    const Value& s = a.at("Scale");
    const double dx = p[0] - t.at("X").num(), dy = p[1] - t.at("Y").num(), dz = p[2] - t.at("Z").num();
    return std::abs(dx) <= 500 * s.at("X").num() && dy >= 0 && dy <= 1000 * s.at("Y").num() &&
           std::abs(dz) <= 500 * s.at("Z").num();
}

// The stand-in for a material whose combiner or lighting the translation does not cover yet: the
// albedo texture times the vertex colour with a simple directional term, and the material's
// alpha test.
std::string plainShader(const Material& m) {
    std::string test;
    if (auto it = m.fragment.find(0x104); it != m.fragment.end() && (it->second & 1)) {
        test = "if(c.a<" + std::to_string(double((it->second >> 8) & 255) / 255) + ")discard;";
    }
    return "#version 120\n"
           "varying vec4 vertexColor; varying vec3 normal;\n"
           "uniform sampler2D albedo; uniform int useLighting;\n"
           "void main(){vec4 c=texture2D(albedo,gl_TexCoord[0].xy)*vertexColor;" +
           test +
           "float l=useLighting==0?1.0:0.62+0.38*max(0.0,dot(normalize(normal),normalize(gl_LightSource[0].position."
           "xyz)));gl_FragColor=vec4(c.rgb*l,c.a);}\n";
}

// Texture coordinates of slot one: scale, rotation and translation. Without rotation this is
// the opening's convention (scaleOffset); the rotation about the scaled origin is a hypothesis.
void transformUv(const TexTransform& t, double uv[2]) {
    if (!(t.flags & 255)) {
        return;
    }
    const double r = t.v[2];
    if (r == 0 && !((t.flags >> 8) & 255)) {
        double s[2], o[2];
        scaleOffset(t, s, o);
        uv[0] = uv[0] * s[0] + o[0];
        uv[1] = uv[1] * s[1] + o[1];
        return;
    }
    const double x = uv[0] * t.v[0] - t.v[0] * t.v[3], y = uv[1] * t.v[1] - t.v[1] * t.v[4];
    uv[0] = x * std::cos(r) - y * std::sin(r);
    uv[1] = x * std::sin(r) + y * std::cos(r);
}

class StageBuilder {
public:
    StageBuilder(Inputs& in, const std::string& stage, fs::path out, ProgressFn progress)
        : in_(in), out_(std::move(out)), progress_(std::move(progress)) {
        assets_ = stageAssets(in_, stage);
    }
    void run();

private:
    // A model archive used by one or more placements: its BCH, chosen model and materials.
    struct ModelEntry {
        std::string archive;
        std::shared_ptr<Bch> bch;
        Model model;
        size_t materialBase = 0;
        std::map<std::string, long> textures;   // material texture name -> package texture index
        std::vector<M34> boneWorld;             // rest-pose bone matrices
        bool terrain = false;                   // drawn in the terrain category
        bool visible = true;                    // drawn in the scene (not only in the shadow map)
        // Material animations (table 9): per file ("Wait.cmtsa", "Color.cmcla") the animated
        // materials; the rest values of every animated material; the materials whose texture
        // coordinates the shader transforms (exported untransformed).
        Value materialClips = Value::object();
        Value materialRest = Value::array();
        std::set<size_t> rawUv;
        // Level-of-detail models (0x298824: the archive's models after the first, except a last
        // "_shd" shadow model, take the slots 1..3; they are drawn with the first model's
        // skeleton) and the switch parameters (InitModel, 0x26d64c).
        std::vector<ModelEntry*> lods;
        Value lodParams;
        std::string textureArc;   // InitModel TextureArc (textures kept in another archive)
    };

    void step(double f, const char* what) {
        if (progress_) {
            progress_(f, what);
        }
    }
    void note(const std::string& kind, const std::string& text) {
        if (!report_.has(kind)) {
            report_[kind] = Value::array();
        }
        report_[kind].push(text);
    }
    size_t textureCount() const { return scene_.at("textures").size(); }
    const std::vector<LutRecord>& luts() {
        if (luts_.empty()) {
            luts_ = lutRecords(in_.bch(kLuts, "LutCurveData.bch"));
        }
        return luts_;
    }
    // A model archive's own lighting tables (modelLutRecords), appended to the LUT atlas after the
    // rows lighting() wrote (the shared set, then the headlamp's); once per archive.
    // The shared archive's other sets (LutCurveData: "Water", "Object_Tables") come first, once.
    std::map<std::string, std::vector<ModelLut>> modelLuts_;
    std::vector<ModelLut> appendLuts(const Bch& bch) {
        std::vector<ModelLut> out;
        std::vector<std::string> tables;
        std::vector<LutRecord> records = modelLutRecords(bch, tables);
        for (size_t k = 0; k < records.size(); ++k) {
            const size_t row = size_t(scene_.at("lighting").at("lut_rows").num());
            for (uint32_t w : records[k].words) {
                pack(lutAtlas_, {(w & 4095) / 4095.0, ((w >> 12) & 2047) / 2047.0 * ((w & (1u << 23)) ? -1 : 1), 0, 1});
            }
            scene_["lighting"]["lut_rows"] = double(row + 1);
            out.push_back({tables[k], std::move(records[k]), row});
        }
        return out;
    }
    const std::vector<ModelLut>& modelLuts(const std::string& archive, const Bch& bch) {
        auto it = modelLuts_.find(archive);
        if (it != modelLuts_.end()) {
            return it->second;
        }
        if (!modelLuts_.count(kLuts)) {
            modelLuts_[kLuts] = appendLuts(in_.bch(kLuts, "LutCurveData.bch"));
        }
        std::vector<ModelLut> out = appendLuts(bch);   // the model's own sets
        const std::vector<ModelLut>& shared = modelLuts_.at(kLuts);
        out.insert(out.end(), shared.begin(), shared.end());
        return modelLuts_[archive] = std::move(out);
    }
    std::string mapArchive() const { return "StageData/" + assets_.stage + "Map1.szs"; }
    std::string designArchive() const { return "StageData/" + assets_.stage + "Design1.szs"; }
    bool hasMember(const std::string& archive, const std::string& name) {
        if (!Inputs::has(archive)) {
            return false;
        }
        for (const auto& m : in_.archive(archive)) {
            if (m.name == name) {
                return true;
            }
        }
        return false;
    }

    void load();
    void presentation();
    void sky();
    void lighting();
    void placements();
    void character(int form);
    void player();
    void shadows();
    void camera();
    void finish();

    long texture(const std::string& name, const Bch& own, const std::string& textureArc);
    long addTextureFrom(const Bch& b, const TextureRecord& t, const std::string& source);
    ModelEntry* modelFor(const std::string& name) { return modelFor(name, name); }
    // A model of an archive (ObjectData/<archiveName>.szs, BCH <archiveName>.bch): the model named
    // modelName, else the first.
    ModelEntry* modelFor(const std::string& archiveName, const std::string& modelName);
    void placeModel(ModelEntry& e, const M34& placement, const std::string& id);
    void placeModelMeshes(ModelEntry& src, ModelEntry& e, const M34& placement, const std::string& id, int level);
    void materialAnimations(ModelEntry& e);
    // Objects of model `model` share its material animations (scene "material_animations").
    void shareMaterialAnimations(const std::string& model, const ModelEntry& e) {
        if (e.materialClips.size() == 0) {
            return;
        }
        if (!scene_.has("material_animations")) {
            scene_["material_animations"] = Value::object();
        }
        if (!scene_.at("material_animations").has(model)) {
            Value m = Value::object();
            m["clips"] = e.materialClips;
            m["rest"] = e.materialRest;
            scene_["material_animations"][model] = m;
        }
    }
    // Skeletal clips, billboards, or visibility animations (the sign's SignAim) need the actor path.
    // The GrassControl placement whose GrassObj link names this object (null: none).
    const Value* grassControlFor(const std::string& id) const {
        for (const char* list : {"ObjectList", "MapPartsList"}) {
            const Value* l = map_.find(list);
            if (!l || !l->isArray()) {
                continue;
            }
            for (const auto& c : l->items()) {
                const Value* links = c.find("Links");
                const Value* targets = links && links->isObject() ? links->find("GrassObj") : nullptr;
                if (!unitIs(c, "GrassControl") || !targets || !targets->isArray()) {
                    continue;
                }
                for (const auto& t : targets->items()) {
                    if (t.isObject() && t.has("Id") && t.at("Id").str() == id) {
                        return &c;
                    }
                }
            }
        }
        return nullptr;
    }
    bool animated(const ModelEntry& e) const {
        return !e.bch->table(8).empty() || !e.bch->table(10).empty() || billboarded(e);
    }
    // Bones that turn to the camera (H3D billboard modes, bone flags bits 16-18) need the posed
    // actor path even without clips (e.g. the coin stack: its column Y-axial, its sparkle facing
    // the camera).
    bool billboarded(const ModelEntry& e) const {
        size_t modelIndex = 0;
        const auto models = e.bch->models();
        while (modelIndex + 1 < models.size() && models[modelIndex].name != e.model.name) {
            ++modelIndex;
        }
        for (uint32_t f : boneFlags(*e.bch, modelIndex)) {
            if ((f >> 16) & 7) {
                return true;
            }
        }
        return false;
    }
    void placeActor(ModelEntry& e, const Value& o, const std::string& id);
    void placeCollision(const std::string& name, const M34& placement, const Value& o);
    void placeCollisionFile(const std::string& name, const std::string& kclName, const M34& placement, const Value& o,
                            bool playerOnly);
    void addObject(const Value& o, const std::string& unit, const std::string& model, const M34& placement);
    void effects();
    json::Value actor2d(const std::string& actor);
    std::string cubeMapArchive() const;
    static std::string archiveMember(const std::string& archive);
    json::Value cameraMask(const json::Value& p);
    long cubeAtlas();
    long cubeAtlas_ = -2;   // -2: not built yet
    void sound();

    Inputs& in_;
    fs::path out_;
    ProgressFn progress_;
    StageAssets assets_;
    Value scene_ = Value::object();
    Value map_, design_, report_ = Value::object();
    std::vector<LutRecord> luts_;
    Bytes vertices_, tangents_, characterSkin_, characterMiniSkin_, casters_, collision_, lutAtlas_;
    // The second UV set (v5) of the vertices whose material reads it (Material::uvSet), by vertex.
    std::vector<std::pair<size_t, std::array<float, 2>>> secondUv_;
    std::map<size_t, std::string> shaders_;
    std::string vertexShader_;
    Value character_, characterMini_, player_;
    Value collisionParts_ = Value::array();
    Value objects_ = Value::array();
    Value actors_ = Value::array();   // scene.json actors (animated objects)
    std::set<std::string> reportedClips_;
    std::vector<std::pair<std::string, Value>> actorFiles_;   // actors/<id>.json documents
    std::map<std::string, Bytes> actorSkins_;                 // actors/<id>-skin.bin
    std::map<std::string, std::unique_ptr<ModelEntry>> models_;
    std::set<std::string> missingModels_;
    std::map<std::string, long> texturePool_;   // "archive:name" -> package texture index
    std::vector<std::pair<std::string, Bch>> textureArchives_;
    double spawn_[3] = {0, 0, 0};
    // Effects and sound: the EffectYaml / SeDataBase entries of the placed models (InitEffect Name,
    // InitAudio SeUserName) and their hit reaction tables (ActorHitReactionCtrl), by model.
    std::set<std::string> effectUsers_, soundUsers_;
    Value hitReactions_ = Value::object();
};

// ---- stage files ----------------------------------------------------------------------------

void StageBuilder::load() {
    map_ = in_.byml(mapArchive(), assets_.stage + "Map.byml");
    design_ = Value::object();
    for (const auto& m : in_.archive(designArchive())) {
        if (m.name.size() > 5 && m.name.compare(m.name.size() - 5, 5, ".byml") == 0 && m.data.size() > 16) {
            try {
                design_[m.name.substr(0, m.name.size() - 5)] = byml(m.data);
            } catch (const ImportError&) {
                note("unread", designArchive() + "/" + m.name);
            }
        }
    }
    const Value& start = map_.at("PlayerList")[0].at("Translate");
    spawn_[0] = start.at("X").num();
    spawn_[1] = start.at("Y").num();
    spawn_[2] = start.at("Z").num();
    scene_["schema"] = 1;
    scene_["stage_schema"] = kStageSchema;
    scene_["name"] = assets_.stage;
    scene_["asset_stage"] = assets_.assetStage;
    scene_["textures"] = Value::array();
    scene_["materials"] = Value::array();
    scene_["meshes"] = Value::array();
}

// ---- textures -------------------------------------------------------------------------------

long StageBuilder::addTextureFrom(const Bch& b, const TextureRecord& t, const std::string& source) {
    const std::string key = source + ":" + t.name;
    if (auto it = texturePool_.find(key); it != texturePool_.end()) {
        return it->second;
    }
    const size_t i = textureCount();
    TextureRecord renamed = t;
    renamed.name = key;
    scene_["textures"].push(toJson(renamed, false));
    writeFile(
        out_ / "textures" / (std::to_string(i) + ".rgba"),
        decodePica(slice(b.bytes(), t.offset, picaSize(t.width, t.height, t.format)), t.width, t.height, t.format));
    texturePool_[key] = long(i);
    return long(i);
}

// A material's texture: the model's own textures, then its InitModel TextureArc archive, then
// the stage texture archive (0x253a50 loads it into the shared texture pool).
long StageBuilder::texture(const std::string& name, const Bch& own, const std::string& textureArc) {
    for (const auto& t : own.textureRecords()) {
        if (t.name == name) {
            return addTextureFrom(own, t, "model");
        }
    }
    std::vector<std::string> archives;
    if (!textureArc.empty()) {
        archives.push_back("ObjectData/" + textureArc + ".szs");
    }
    archives.push_back("ObjectData/" + assets_.base + "Textures.szs");
    // The cube texture CubeMap of the course's cube map archive: its six ETC1 faces (+X, -X, +Y, -Y,
    // +Z, -Z, registers 0x85..0x8a) stacked top to bottom in one texture, sampled by cubeShader
    // (as the opening's goal star, SceneExport cubeAtlas).
    if (name == "CubeMap") {
        return cubeAtlas();
    }
    // The reflection texture SphereMap of the course's cube map archive, as the protagonist's
    // (0x253a50): map parts and objects name it without holding it (TouchPuzzleMiniTouchPartsA /
    // B, TouchPuzzleMiniStep, the companion KinopioMemberNPC).
    if (name == "SphereMap") {
        archives.push_back(cubeMapArchive());
    }
    for (const auto& a : archives) {
        if (!Inputs::has(a)) {
            continue;
        }
        const std::string member = archiveMember(a);
        if (!hasMember(a, member)) {
            continue;
        }
        const Bch b = in_.bch(a, member);
        for (const auto& t : b.textureRecords()) {
            if (t.name == name) {
                return addTextureFrom(b, t, a);
            }
        }
    }
    return -1;
}

// The camera mask's export (3DSEft.byml CameraMask; render/CameraMask.hpp): its parameters with
// the constructor's defaults, the CameraMaskFull model's triangles (position, uv) and its texture.
Value StageBuilder::cameraMask(const Value& p) {
    const std::string archive = "ObjectData/CameraMaskFull.szs";
    Value out = Value::object();
    if (!Inputs::has(archive) || !hasMember(archive, "CameraMaskFull.bch")) {
        note("without_model", "CameraMaskFull");
        return out;
    }
    auto num = [&](const char* k, double d) {
        const Value* v = p.find(k);
        return v && v->isNumber() ? v->num() : d;
    };
    // Defaults of the constructor 0x247340: colours and alpha 0.8, Pos (0, 0, -200), Scale
    // (460, 260, 100), Depth 0.5.
    out["color1"] = doubles({num("Red1", 0.8), num("Green1", 0.8), num("Blue1", 0.8)});
    out["color2"] = doubles({num("Red2", 0.8), num("Green2", 0.8), num("Blue2", 0.8)});
    out["alpha"] = num("Alpha", 0.8);
    out["position"] = doubles({num("PosX", 0), num("PosY", 0), num("PosZ", -200)});
    out["scale"] = doubles({num("ScaleX", 460), num("ScaleY", 260), num("ScaleZ", 100)});
    out["depth"] = num("Depth", 0.5);
    const Bch b = in_.bch(archive, "CameraMaskFull.bch");
    const Model model = b.models().at(0);
    Value tris = Value::array();
    for (const Mesh& mesh : model.meshes) {
        if (!mesh.visible) {
            continue;
        }
        const Material& m = model.materials.at(mesh.material);
        if (m.textures[0] && !out.has("texture")) {
            for (const auto& t : b.textureRecords()) {
                if (t.name == *m.textures[0]) {
                    out["texture"] = double(addTextureFrom(b, t, archive));
                }
            }
            if (!m.samplers.empty()) {
                out["wrap"] = doubles({double(m.samplers[0].wrapS), double(m.samplers[0].wrapT)});
            }
            const TexTransform& tt = m.transforms[0];
            out["uv_transform"] = doubles({tt.v[0], tt.v[1], tt.v[2], tt.v[3], tt.v[4]});
        }
        const Vec4d& sc = mesh.uniform(7);
        const Vec4d& po = mesh.uniform(6);
        const double u8s = mesh.uniform(8)[0];
        for (const Face& face : mesh.faces) {
            for (uint32_t index : face.indices) {
                const Vertex& v = mesh.vertices.at(index);
                tris.push(doubles({v.v[0][0] * sc[0] + po[0], v.v[0][1] * sc[0] + po[1], v.v[0][2] * sc[0] + po[2],
                                   v.has(4) ? v.v[4][0] * u8s : 0.0, v.has(4) ? v.v[4][1] * u8s : 0.0}));
            }
        }
    }
    out["vertices"] = tris;
    return out;
}

// The course cube map's six faces as one texture (index into the scene textures; -1: none).
long StageBuilder::cubeAtlas() {
    if (cubeAtlas_ != -2) {
        return cubeAtlas_;
    }
    cubeAtlas_ = -1;
    const std::string cube = cubeMapArchive();
    const std::string member = archiveMember(cube);
    if (!Inputs::has(cube) || !hasMember(cube, member)) {
        return cubeAtlas_;
    }
    const Bch cb = in_.bch(cube, member);
    for (const auto& rec : cb.textureRecords()) {
        if (rec.name != "CubeMap" || rec.format != 12 || rec.registers.size() == 0) {
            continue;
        }
        Bytes atlas;
        for (uint32_t reg = 0x85; reg < 0x8b; ++reg) {
            const Bytes face = decodeEtc(slice(cb.bytes(), rec.registers.at(reg), size_t(rec.width) * rec.height / 2),
                                         rec.width, rec.height, 12);
            atlas.insert(atlas.end(), face.begin(), face.end());
        }
        const size_t i = textureCount();
        Value t = Value::object();
        t["name"] = cube + ":CubeMapAtlas";
        t["width"] = rec.width;
        t["height"] = rec.height * 6;
        t["format"] = 12;
        scene_["textures"].push(t);
        writeFile(out_ / "textures" / (std::to_string(i) + ".rgba"), atlas);
        cubeAtlas_ = long(i);
        break;
    }
    if (cubeAtlas_ < 0) {
        note("unapplied", "cube map " + cube);
    }
    return cubeAtlas_;
}

// The course's cube map archive (CubeMap<stage>.szs, else CubeMapDefault.szs).
std::string StageBuilder::cubeMapArchive() const {
    const std::string cube = "ObjectData/CubeMap" + assets_.assetStage + ".szs";
    return Inputs::has(cube) ? cube : std::string("ObjectData/CubeMapDefault.szs");
}

// "ObjectData/X.szs" -> "X.bch", the archive's model member.
std::string StageBuilder::archiveMember(const std::string& archive) {
    return archive.substr(11, archive.size() - 15) + ".bch";
}

// ---- models ---------------------------------------------------------------------------------

StageBuilder::ModelEntry* StageBuilder::modelFor(const std::string& archiveName, const std::string& name) {
    const std::string key = archiveName == name ? name : archiveName + "/" + name;
    if (auto it = models_.find(key); it != models_.end()) {
        return it->second.get();
    }
    const std::string archive = "ObjectData/" + archiveName + ".szs";
    if (!Inputs::has(archive) || !hasMember(archive, archiveName + ".bch")) {
        models_[key] = nullptr;
        return nullptr;
    }
    auto e = std::make_unique<ModelEntry>();
    e->archive = archive;
    e->bch = std::make_shared<Bch>(in_.file(archive, archiveName + ".bch"));
    const auto models = e->bch->models();
    if (models.empty()) {
        models_[key] = nullptr;
        return nullptr;
    }
    e->model = models.at(0);
    for (const auto& m : models) {
        if (m.name == name) {
            e->model = m;
        }
    }
    std::string& textureArc = e->textureArc;
    if (hasMember(archive, "InitModel.byml")) {
        const Value init = in_.byml(archive, "InitModel.byml");
        if (const Value* t = init.isObject() ? init.find("TextureArc") : nullptr; t && t->isString()) {
            textureArc = t->str();
        }
    }
    if (hasMember(archive, "InitExecutor.byml")) {
        const Value exec = in_.byml(archive, "InitExecutor.byml");
        if (const Value* d = exec.isObject() ? exec.find("Drawer") : nullptr; d && d->isArray()) {
            // Drawer categories (ROM names): a model drawn only into the depth-shadow map ("デプス
            // シャドウ[..]"), the photo-mode capture ("撮影用[..]"), the depth pre-pass ("Ｚプリパス
            // [..]") or a light buffer ("[ライトバッファ]") is not drawn in the scene itself.
            e->visible = false;
            for (const auto& c : d->items()) {
                const std::string cat = c.has("CategoryName") ? c.at("CategoryName").str() : "";
                if (cat == "地形") {   // "terrain"
                    e->terrain = true;
                }
                const bool hidden = cat.rfind("デプスシャドウ", 0) == 0 || cat.rfind("撮影用", 0) == 0 ||
                                    cat.rfind("Ｚプリパス", 0) == 0 || cat.find("ライトバッファ") != std::string::npos;
                e->visible = e->visible || !hidden;
            }
        }
    }
    // Rest pose: each bone's local scale / rotation (radians) / translation under its parent.
    for (const auto& bone : e->model.bones) {
        const double s[3] = {bone.scale[0], bone.scale[1], bone.scale[2]};
        const double r[3] = {bone.rotation[0], bone.rotation[1], bone.rotation[2]};
        const double t[3] = {bone.translation[0], bone.translation[1], bone.translation[2]};
        const M34 local = trs(s, r, t);
        e->boneWorld.push_back(bone.parent < 0 ? local : multiply(e->boneWorld.at(size_t(bone.parent)), local));
    }
    for (const auto& m : e->model.materials) {
        for (const auto& n : m.textures) {
            if (n && *n != "shadowmap" && !e->textures.count(*n)) {
                const long id = texture(*n, *e->bch, textureArc);
                e->textures[*n] = id;
                if (id < 0) {
                    note("missing_textures", name + ":" + *n);
                }
            }
        }
    }
    // Materials: the combiner and PICA fragment lighting, as for the opening's terrain. Material
    // features the translation does not cover yet get the plain textured shader below and are
    // listed in the report.
    e->materialBase = scene_.at("materials").size();
    const std::vector<ModelLut>& ownLuts = modelLuts(archive, *e->bch);
    for (size_t i = 0; i < e->model.materials.size(); ++i) {
        const Material& m = e->model.materials[i];
        Value entry = toJson(m);
        try {
            TevOptions o;
            o.texture0 = m.textures[0] && *m.textures[0] != "shadowmap";
            if (!usesLighting(m)) {
                if (m.textures[0] == std::optional<std::string>("CubeMap")) {
                    note("unapplied", name + ":" + m.name + " cube map without lighting");
                }
                shaders_[e->materialBase + i] = generateTev(m.fragment, o);
                scene_["materials"].push(entry);
                continue;
            }
            const PicaState st = picaState(*e->bch, m, luts(), true, &ownLuts);
            o.colors = &m.colors;
            std::string source = generateTev(m.fragment, o);
            // Slot 0 reading the course's cube texture (DoorLock, DoorKey, KinokoTreasure, GoalItem):
            // the reflection vector into the face atlas, as the opening's goal star.
            if (m.textures[0] == std::optional<std::string>("CubeMap")) {
                source = cubeShader(source);
            } else if (m.textures[1] == std::optional<std::string>("CubeMap") ||
                       m.textures[2] == std::optional<std::string>("CubeMap")) {
                note("unapplied", name + ":" + m.name + " cube map in slot 1 / 2");
            }
            if (((m.transforms[1].flags >> 16) & 255) == 2) {
                source = replaceAll(source, "vec4 tex1=texture2D(albedo,gl_TexCoord[0].xy);",
                                    "vec3 reflected=viewToWorld*reflect(normalize(eyePosition),normalize(normal));vec4 "
                                    "tex1=texture2D(albedo,reflected.xy*0.5+0.5);");
                source = replaceFirst(source, "void main()", "uniform mat3 viewToWorld;\nvoid main()");
            }
            shaders_[e->materialBase + i] = picaLightingFragment(source, m, st);
            Value pica = Value::object();
            pica["kind"] = "terrain";
            pica["config0"] = st.config0;
            pica["config1"] = st.config1;
            pica["lut_abs"] = st.lutAbs;
            pica["lut_input"] = st.lutInput;
            pica["lut_scale"] = st.lutScale;
            pica["lut_names"] = st.lutNamesJson();
            pica["geometric0"] = st.geometric0;
            pica["geometric1"] = st.geometric1;
            pica["shadow_mode"] = st.shadowMode ? Value(*st.shadowMode) : Value();
            pica["colors"] = st.colorsJson();
            pica["flags"] = st.flags;
            pica["light_set"] = st.lightSet;
            pica["locals"] = strings({"headlamp"});
            entry["pica"] = pica;
        } catch (const std::exception& err) {
            shaders_[e->materialBase + i] = plainShader(m);
            note("approximated_materials", name + ":" + m.name + " (" + err.what() + ")");
        }
        scene_["materials"].push(entry);
    }
    materialAnimations(*e);
    for (size_t i = 0; i < e->model.materials.size(); ++i) {   // after the animated slots' uniforms
        std::string& source = shaders_[e->materialBase + i];
        source = slotTwoCoordinates(source, e->model.materials[i]);
        source = waterShader(source, e->model.materials[i]);
        if (source.find("eyePosition") != std::string::npos) {
            source = projectionShader(source, e->model.materials[i]);
        }
    }
    ModelEntry* raw = e.get();
    models_[key] = std::move(e);
    if (models.front().name == raw->model.name && models.size() > 1) {
        size_t count = models.size();
        if (models.back().name.find("_shd") != std::string::npos) {
            --count;
        }
        for (size_t i = 1; i < count && i < 4; ++i) {
            if (ModelEntry* lod = modelFor(archiveName, models[i].name)) {
                raw->lods.push_back(lod);
                // The models share one material animation player: their clips apply to all.
                if (lod->materialClips.isObject()) {
                    for (const auto& member : lod->materialClips.members()) {
                        const std::string& file = member.first;
                        if (!raw->materialClips.has(file)) {
                            raw->materialClips[file] = member.second;
                            continue;
                        }
                        for (const Value& m : member.second.at("materials").items()) {
                            raw->materialClips[file]["materials"].push(m);
                        }
                    }
                }
                for (const Value& r : lod->materialRest.items()) {
                    raw->materialRest.push(r);
                }
            }
        }
        if (!raw->lods.empty()) {
            // 0x26d64c: Lod0SwitchThreshold / Lod1SwitchThreshold (defaults 800 / 1100, the
            // second at least the first, 0x220f08), LodCheckAxis (default xyz), LodDistanceOnly.
            double lod0 = 800, lod1 = 1100;
            std::string axes = "xyz";
            bool distanceOnly = false;
            if (hasMember(archive, "InitModel.byml")) {
                const Value init = in_.byml(archive, "InitModel.byml");
                if (init.isObject()) {
                    if (const Value* v = init.find("Lod0SwitchThreshold")) {
                        lod0 = v->num();
                    }
                    if (const Value* v = init.find("Lod1SwitchThreshold")) {
                        lod1 = v->num();
                    }
                    if (const Value* v = init.find("LodCheckAxis"); v && v->isString()) {
                        axes = v->str();
                    }
                    if (const Value* v = init.find("LodDistanceOnly"); v && v->type() == Value::Type::Bool) {
                        distanceOnly = v->boolean();
                    }
                }
            }
            int mask = 0;
            for (char c : axes) {
                mask |= (c == 'x' || c == 'X') ? 1 : (c == 'y' || c == 'Y') ? 2 : (c == 'z' || c == 'Z') ? 4 : 0;
            }
            raw->lodParams = Value::object();
            raw->lodParams["thresholds"] = Value::array();
            raw->lodParams["thresholds"].push(lod0);
            raw->lodParams["thresholds"].push(std::max(lod0, lod1));
            raw->lodParams["axes"] = mask;
            raw->lodParams["distance_only"] = distanceOnly;
            raw->lodParams["levels"] = int(raw->lods.size()) + 1;
        }
    }
    return raw;
}

// Material colour targets of fragment lighting: index into the material colours (emission 0,
// ambient 1, diffuse 2, specular0 3, specular1 4); -1 otherwise.
static int lightTarget(const std::string& t) {
    static const char* names[5] = {"emission", "ambient", "diffuse", "specular0", "specular1"};
    for (int k = 0; k < 5; ++k) {
        if (t == names[k]) {
            return k;
        }
    }
    return -1;
}

// The combiner stages whose constant colour is material constant `index` (the material's
// stage-to-constant assignment).
static std::vector<int> constantStages(const Material& m, int index) {
    std::vector<int> stages;
    for (int st = 0; st < 6; ++st) {
        if (m.constantAssignment[size_t(st)] == index) {   // indices above 5: no material constant
            stages.push_back(st);
        }
    }
    return stages;
}

// Material animations of a model archive (table 9): colour constants (.cmcla) and texture
// transforms (.cmtsa). The shaders of the animated materials read uniforms instead of their
// constants (MaterialAnimationSet uploads them); a material whose texture transform animates gets
// untransformed texture coordinates and every textured slot as a uniform matrix.
void StageBuilder::materialAnimations(ModelEntry& e) {
    std::map<std::string, Value> anims;
    for (uint32_t p : e.bch->table(9)) {
        const std::string file = e.bch->nameOf(p);
        try {
            anims[file] = materialAnimation(*e.bch, file);
        } catch (const ImportError& err) {
            note("unread_material_animations", e.archive + ":" + file + " (" + err.what() + ")");
        }
    }
    if (anims.empty()) {
        return;
    }
    std::map<std::string, std::set<int>> uvSlots, constants;
    for (const auto& [file, a] : anims) {
        for (const auto& el : a.at("elements").items()) {
            const std::string t = el.at("target").str(), mat = el.at("material").str();
            if (startsWith(t, "tc")) {
                uvSlots[mat].insert(t[2] - '0');
            } else if (startsWith(t, "constant")) {
                constants[mat].insert(t[8] - '0');
            } else if (lightTarget(t) >= 0 || startsWith(t, "mapper")) {
                // lighting colours and texture patterns: per clip below
            } else {
                note("unapplied_material_targets", e.archive + ":" + file + ":" + mat + ":" + t);
            }
        }
    }
    // Shaders and rest values.
    std::map<std::string, std::vector<int>> slotsOf;                   // material -> uniform slots
    std::map<std::string, std::map<int, std::vector<int>>> stagesOf;   // material -> constant -> stages
    for (size_t i = 0; i < e.model.materials.size(); ++i) {
        const Material& m = e.model.materials[i];
        std::string& source = shaders_[e.materialBase + i];
        Value rest = Value::object();
        rest["material_index"] = e.materialBase + i;
        rest["slots"] = Value::object();
        rest["constants"] = Value::array();
        for (int k = 0; k < 3; ++k) {
            if (m.textures[size_t(k)] && m.uvSet[size_t(k)] != 0 &&
                !(uvSlots.count(m.name) && m.uvSet[size_t(k)] == 1)) {
                fprintf(stderr, "Stage: %s slot %d reads UV set %d (only animated slots on set 1 are ported)\n",
                        m.name.c_str(), k, m.uvSet[size_t(k)]);
            }
        }
        if (uvSlots.count(m.name)) {
            std::vector<int> slots;
            for (int k = 0; k < 3; ++k) {
                if (m.textures[size_t(k)] && *m.textures[size_t(k)] != "shadowmap" &&
                    ((m.transforms[size_t(k)].flags & 255) || uvSlots[m.name].count(k))) {
                    slots.push_back(k);
                }
            }
            try {
                source = animatedUvShader(source, slots);
                // Slots reading the second UV set sample with it (gl_TexCoord[2], uv1.bin).
                for (int k : slots) {
                    if (m.uvSet[size_t(k)] == 1) {
                        const std::string n = std::to_string(k);
                        for (const char* axis : {"texU", "texV"}) {
                            source =
                                replaceAll(source, "dot(vec3(gl_TexCoord[0].xy,1.0)," + std::string(axis) + n + ")",
                                           "dot(vec3(gl_TexCoord[2].xy,1.0)," + std::string(axis) + n + ")");
                        }
                    }
                }
                e.rawUv.insert(i);
                slotsOf[m.name] = slots;
                for (int k : slots) {
                    const TexTransform& t = m.transforms[size_t(k)];
                    Value slot = Value::object();
                    slot["convention"] = (t.flags >> 8) & 255;
                    slot["scale"] = doubles({t.v[0], t.v[1]});
                    slot["rotation"] = t.v[2];
                    slot["translation"] = doubles({t.v[3], t.v[4]});
                    slot["curves"] = Value::object();
                    slot["frames"] = 1.0;
                    rest["slots"][std::to_string(k)] = slot;
                }
            } catch (const ImportError& err) {
                note("unapplied_material_animations", e.archive + ":" + m.name + " texture (" + err.what() + ")");
            }
        }
        for (int c : constants[m.name]) {
            const std::vector<int> stages = constantStages(m, c);
            if (stages.empty()) {
                note("unapplied_material_animations",
                     e.archive + ":" + m.name + " constant" + std::to_string(c) + " (no combiner stage holds it)");
                continue;
            }
            const std::string uniform = "animatedConstant" + std::to_string(c);
            source = animatedConstantShader(source, stages, uniform);
            stagesOf[m.name][c] = stages;
            Value base = Value::array(), axes = Value::array();
            for (int k = 0; k < 4; ++k) {
                base.push(m.colors[size_t(20 + 4 * c + k)] / 255.0);
                axes.push(nullptr);
            }
            Value entry = Value::object();
            entry["base"] = base;
            entry["axes"] = axes;
            entry["frames"] = 1.0;
            entry["uniform"] = uniform;
            rest["constants"].push(entry);
        }
        if (rest.at("slots").size() || rest.at("constants").size()) {
            e.materialRest.push(rest);
        }
    }
    // The clips: per animated material its curves (slots and constants the shader reads).
    for (const auto& [file, a] : anims) {
        Value mats = Value::array();
        for (size_t i = 0; i < e.model.materials.size(); ++i) {
            const Material& m = e.model.materials[i];
            Value entry = Value::object();
            entry["material_index"] = e.materialBase + i;
            entry["slots"] = Value::object();
            entry["constants"] = Value::array();
            for (int k : slotsOf[m.name]) {
                Value curves = Value::object();
                const std::string prefix = "tc" + std::to_string(k) + "_";
                for (const auto& el : a.at("elements").items()) {
                    if (el.at("material").str() == m.name && startsWith(el.at("target").str(), prefix)) {
                        curves[el.at("target").str().substr(prefix.size())] = el.at("axes");
                    }
                }
                if (curves.size()) {
                    const TexTransform& t = m.transforms[size_t(k)];
                    Value slot = Value::object();
                    slot["convention"] = (t.flags >> 8) & 255;
                    slot["scale"] = doubles({t.v[0], t.v[1]});
                    slot["rotation"] = t.v[2];
                    slot["translation"] = doubles({t.v[3], t.v[4]});
                    slot["curves"] = curves;
                    slot["frames"] = a.at("frames");
                    entry["slots"][std::to_string(k)] = slot;
                }
            }
            for (const auto& [c, stages] : stagesOf[m.name]) {
                (void)stages;
                for (const auto& el : a.at("elements").items()) {
                    if (el.at("material").str() == m.name && el.at("target").str() == "constant" + std::to_string(c)) {
                        Value base = Value::array();
                        for (int k = 0; k < 4; ++k) {
                            base.push(m.colors[size_t(20 + 4 * c + k)] / 255.0);
                        }
                        Value entryC = Value::object();
                        entryC["base"] = base;
                        entryC["axes"] = el.at("axes");
                        entryC["frames"] = a.at("frames");
                        entryC["uniform"] = "animatedConstant" + std::to_string(c);
                        entry["constants"].push(entryC);
                    }
                }
            }
            // Lighting colours (fragment-lit materials: LightService takes them instead of the
            // material's own).
            entry["lights"] = Value::array();
            const Value& sceneMaterial = scene_.at("materials")[e.materialBase + i];
            for (const auto& el : a.at("elements").items()) {
                const int lt = lightTarget(el.at("target").str());
                if (el.at("material").str() != m.name || lt < 0) {
                    continue;
                }
                if (!sceneMaterial.has("pica")) {
                    note("unapplied_material_targets", e.archive + ":" + file + ":" + m.name + ":" +
                                                           el.at("target").str() +
                                                           " (material without fragment lighting)");
                    continue;
                }
                Value base = Value::array();
                for (int k = 0; k < 4; ++k) {
                    base.push(m.colors[size_t(4 * lt + k)] / 255.0);
                }
                Value light = Value::object();
                light["target"] = lt;
                light["base"] = base;
                light["axes"] = el.at("axes");
                light["frames"] = a.at("frames");
                entry["lights"].push(light);
            }
            // Texture patterns: mapper n is the material's n-th texture slot other than the shadow
            // map (all 42 .cmtpa elements in the ROM whose first frame shows one of the material's
            // own textures fit this); the curve selects one of the animation's textures.
            entry["patterns"] = Value::array();
            for (const auto& el : a.at("elements").items()) {
                const std::string t = el.at("target").str();
                if (el.at("material").str() != m.name || !startsWith(t, "mapper")) {
                    continue;
                }
                int slot = -1;
                for (int k = 0, n = 0; k < 3; ++k) {
                    if (m.textures[size_t(k)] && *m.textures[size_t(k)] != "shadowmap") {
                        if (n++ == t[6] - '0') {
                            slot = k;
                        }
                    }
                }
                if (slot < 0) {
                    continue;   // no such texture slot: nothing to change
                }
                Value ids = Value::array();
                for (const auto& name : a.at("textures").items()) {
                    long id = -1;
                    if (auto it = e.textures.find(name.str()); it != e.textures.end()) {
                        id = it->second;
                    } else {
                        id = texture(name.str(), *e.bch, e.textureArc);
                        e.textures[name.str()] = id;
                    }
                    if (id < 0) {
                        note("missing_textures", e.archive + ":" + name.str());
                    }
                    ids.push(id);
                }
                Value pattern = Value::object();
                pattern["slot"] = slot;
                pattern["textures"] = ids;
                pattern["curve"] = el.at("axes")[0];
                pattern["frames"] = a.at("frames");
                entry["patterns"].push(pattern);
            }
            if (entry.at("slots").size() || entry.at("constants").size() || entry.at("lights").size() ||
                entry.at("patterns").size()) {
                mats.push(entry);
            }
        }
        if (mats.size()) {
            Value clip = Value::object();
            clip["frames"] = a.at("frames");
            clip["loop"] = a.at("loop");
            clip["materials"] = mats;
            e.materialClips[file] = clip;
        }
    }
}

// One placement of a model in its rest pose, as static world-space triangles.
void StageBuilder::placeModel(ModelEntry& e, const M34& placement, const std::string& objectId) {
    placeModelMeshes(e, e, placement, objectId, 0);
    for (size_t k = 0; k < e.lods.size(); ++k) {
        placeModelMeshes(*e.lods[k], e, placement, objectId, int(k) + 1);
    }
}

// Bone `i` of model `src` as a bone of the skeleton's model (by name; LOD models are drawn with
// the first model's skeleton, 0x37528c).
static size_t skeletonBone(const Model& src, const Model& skeleton, size_t i) {
    if (&src == &skeleton || i >= src.bones.size()) {
        return i;
    }
    for (size_t k = 0; k < skeleton.bones.size(); ++k) {
        if (skeleton.bones[k].name == src.bones[i].name) {
            return k;
        }
    }
    return i < skeleton.bones.size() ? i : 0;
}

// The LOD fields of a placed object's meshes (its level and the switch parameters).
static void lodFields(Value& m, const Value& params, int level) {
    if (params.isObject()) {
        m["lod"] = level;
        m["lod_params"] = params;
    }
}

void StageBuilder::placeModelMeshes(ModelEntry& src, ModelEntry& e, const M34& placement, const std::string& objectId,
                                    int level) {
    for (const Mesh& mesh : src.model.meshes) {
        if (!mesh.visible || (mesh.flags & 1)) {
            continue;
        }
        const Material& mat = src.model.materials.at(mesh.material);
        const size_t start = vertices_.size() / 48;
        const Vec4d &sc = mesh.uniform(7), &u = mesh.uniform(8), &po = mesh.uniform(6);
        for (const Face& f : mesh.faces) {
            for (uint32_t idx : f.indices) {
                const Vertex& v = mesh.vertices.at(idx);
                double pos[3], normal[3], t[3], uv[2], c[4];
                const double defaultNormal[3] = {0, 1, 0};
                for (int k = 0; k < 3; ++k) {
                    pos[k] = v.v[0][size_t(k)] * sc[0] + po[size_t(k)];
                    normal[k] = v.has(1) ? v.v[1][size_t(k)] * sc[1] : defaultNormal[k];
                    t[k] = (v.has(2) ? v.v[2][size_t(k)] * sc[2] : (k == 0 ? 1.0 : 0.0));
                }
                for (int k = 0; k < 2; ++k) {
                    uv[k] = (v.has(4) ? v.v[4][size_t(k)] : 0.0) * u[0];
                }
                if (!src.rawUv.count(mesh.material)) {
                    transformUv(mat.transforms[1], uv);
                }
                for (int k = 0; k < 4; ++k) {
                    c[k] = v.has(3) ? v.v[3][size_t(k)] * sc[3] : 1.0;
                }
                // Skinning in the rest pose: smooth-skinned vertices (mode 1) are stored in model
                // space; rigid (2) and unskinned (0) ones relative to their bone.
                M34 m = placement;
                if (f.skin != 1 && !e.boneWorld.empty()) {
                    const Skin s = skinOf(v, f, u[3], 1e-3, true);
                    const size_t bone =
                        skeletonBone(src.model, e.model, s.indices.empty() ? size_t(0) : size_t(s.indices[0]));
                    m = multiply(placement, e.boneWorld.at(std::min(bone, e.boneWorld.size() - 1)));
                }
                const auto wp = apply(m, pos, true);
                auto wn = apply(m, normal, false);
                auto wt = apply(m, t, false);
                const double nl = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
                if (nl > 0) {
                    for (auto& x : wn) {
                        x /= nl;
                    }
                }
                if (v.has(5) && std::count(mat.uvSet.begin(), mat.uvSet.end(), 1)) {
                    secondUv_.push_back({vertices_.size() / 48, {float(v.v[5][0] * u[1]), float(v.v[5][1] * u[1])}});
                }
                pack(vertices_, {wp[0], wp[1], wp[2], wn[0], wn[1], wn[2], uv[0], uv[1], c[0], c[1], c[2], c[3]});
                pack(tangents_, {wt[0], wt[1], wt[2]});
            }
        }
        auto id = [&](const std::optional<std::string>& n) {
            if (!n) {
                return -1L;
            }
            auto it = src.textures.find(*n);
            return it == src.textures.end() ? -1L : it->second;
        };
        const uint32_t reg = mat.registers.count(0x93) ? mat.registers.at(0x93) : 0;
        Value m = Value::object();
        m["name"] = mat.name;
        m["first"] = start;
        m["count"] = vertices_.size() / 48 - start;
        m["texture"] = id(mat.textures[1]);
        m["texture0"] = id(mat.textures[0]);
        m["detail_texture"] = id(mat.textures[2]);
        m["wrap_s"] = (reg >> 12) & 15;
        m["wrap_t"] = (reg >> 8) & 15;
        m["material_index"] = src.materialBase + mesh.material;
        m["samplers"] = samplersJson(mat.samplers);
        m["object"] = objectId;
        lodFields(m, e.lodParams, level);
        scene_["meshes"].push(m);
    }
}

// One placement of an animated model (its archive has skeletal clips) as an actor: model-space
// vertices with their skin, posed in the session by the object's behaviour (CharacterAnimation).
// Every skeletal clip of the archive is carried as a named curve clip with its visibility tracks;
// AnimInterp.byml gives the clip-change blend lengths. The actor starts in the rest pose.
void StageBuilder::placeActor(ModelEntry& e, const Value& o, const std::string& objectId) {
    const std::string prefix = objectId;
    Bytes skin;
    const size_t first = vertices_.size() / 48;
    Value entries = Value::array();
    const Value names = nodeNames(*e.bch);
    auto placeMeshes = [&](ModelEntry& src, int level) {
        for (const Mesh& mesh : src.model.meshes) {
            if (!mesh.visible || (mesh.flags & 1)) {
                continue;
            }
            const Material& mat = src.model.materials.at(mesh.material);
            const size_t start = vertices_.size() / 48;
            const Vec4d &sc = mesh.uniform(7), &u = mesh.uniform(8), &po = mesh.uniform(6);
            for (const Face& f : mesh.faces) {
                for (uint32_t idx : f.indices) {
                    const Vertex& v = mesh.vertices.at(idx);
                    double pos[3], normal[3], t[3], uv[2], c[4];
                    const double defaultNormal[3] = {0, 1, 0};
                    for (int k = 0; k < 3; ++k) {
                        pos[k] = v.v[0][size_t(k)] * sc[0] + po[size_t(k)];
                        normal[k] = v.has(1) ? v.v[1][size_t(k)] * sc[1] : defaultNormal[k];
                        t[k] = (v.has(2) ? v.v[2][size_t(k)] * sc[2] : (k == 0 ? 1.0 : 0.0));
                    }
                    for (int k = 0; k < 2; ++k) {
                        uv[k] = (v.has(4) ? v.v[4][size_t(k)] : 0.0) * u[0];
                    }
                    if (!src.rawUv.count(mesh.material)) {
                        transformUv(mat.transforms[1], uv);
                    }
                    for (int k = 0; k < 4; ++k) {
                        c[k] = v.has(3) ? v.v[3][size_t(k)] * sc[3] : 1.0;
                    }
                    Skin sk = skinOf(v, f, u[3], 1e-3, true);   // quantized weights (the sign: 1e-3, as placeModel)
                    for (auto& bi : sk.indices) {
                        bi = uint32_t(skeletonBone(src.model, e.model, size_t(bi)));
                    }
                    packSkin(skin, pos, normal, t, sk, (f.skin == 1 || f.skin == 2) ? f.skin : 0);
                    if (v.has(5) && std::count(mat.uvSet.begin(), mat.uvSet.end(), 1)) {
                        secondUv_.push_back(
                            {vertices_.size() / 48, {float(v.v[5][0] * u[1]), float(v.v[5][1] * u[1])}});
                    }
                    pack(vertices_, {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], uv[0], uv[1], c[0], c[1],
                                     c[2], c[3]});
                    pack(tangents_, {t[0], t[1], t[2]});
                }
            }
            auto id = [&](const std::optional<std::string>& n) {
                if (!n) {
                    return -1L;
                }
                auto it = src.textures.find(*n);
                return it == src.textures.end() ? -1L : it->second;
            };
            const uint32_t reg = mat.registers.count(0x93) ? mat.registers.at(0x93) : 0;
            Value m = Value::object();
            m["name"] = mat.name;
            m["first"] = start;
            m["count"] = vertices_.size() / 48 - start;
            m["texture"] = id(mat.textures[1]);
            m["texture0"] = id(mat.textures[0]);
            m["detail_texture"] = id(mat.textures[2]);
            m["wrap_s"] = (reg >> 12) & 15;
            m["wrap_t"] = (reg >> 8) & 15;
            m["material_index"] = src.materialBase + mesh.material;
            m["samplers"] = samplersJson(mat.samplers);
            m["actor"] = prefix;
            m["object"] = objectId;
            lodFields(m, e.lodParams, level);
            scene_["meshes"].push(m);
            Value entry = Value::object();
            entry["mesh_index"] = scene_.at("meshes").size() - 1;
            entry["node"] = mesh.node >= 0 && size_t(mesh.node) < names.size() ? names[size_t(mesh.node)] : Value("");
            entries.push(entry);
        }
    };
    placeMeshes(e, 0);
    for (size_t k = 0; k < e.lods.size(); ++k) {
        placeMeshes(*e.lods[k], int(k) + 1);
    }
    size_t modelIndex = 0;
    const auto models = e.bch->models();
    while (modelIndex + 1 < models.size() && models[modelIndex].name != e.model.name) {
        ++modelIndex;
    }
    const auto flags = boneFlags(*e.bch, modelIndex);
    Value bones = Value::array();
    for (size_t i = 0; i < e.model.bones.size(); ++i) {
        Value bone = toJson(e.model.bones[i]);
        bone["billboard"] = i < flags.size() ? (flags[i] >> 16) & 7 : 0;
        bones.push(bone);
    }
    if (e.model.bones.empty()) {   // a model without a skeleton (the sign): one identity root
        Bone root{"Root", -1, 0, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
        Value bone = toJson(root);
        bone["billboard"] = 0;
        bones.push(bone);
    }
    Value clips = Value::object(), clipVisibility = Value::object();
    for (uint32_t p : e.bch->table(8)) {
        const std::string clip = e.bch->nameOf(p);
        try {
            clips[clip] = actorAnimation(*e.bch, clip);
        } catch (const ImportError& err) {
            if (!reportedClips_.count(e.archive + ":" + clip)) {
                reportedClips_.insert(e.archive + ":" + clip);
                note("unread_clips", e.archive + ":" + clip + " (" + err.what() + ")");
            }
            continue;
        }
        try {
            clipVisibility[clip] = visibility(*e.bch, {clip});
        } catch (const ImportError&) {
            // the clip has no visibility tracks
        }
    }
    // Visibility animations without a skeletal clip of their name (e.g. "Glasses"): played by
    // their own player (game/StageObjects.hpp Object::visAnim).
    Value overlays = Value::object();
    for (uint32_t p : e.bch->table(10)) {
        const std::string name = e.bch->nameOf(p);
        if (clips.has(name)) {
            continue;
        }
        try {
            Value v = Value::object();
            v["bits"] = visibility(*e.bch, {name});
            v["frames"] = e.bch->f32(p + 8);
            v["loop"] = bool(e.bch->u32(p + 4) & 1);
            overlays[name] = v;
        } catch (const ImportError&) {
            note("unread_visibility", e.archive + ":" + name);
        }
    }
    Value rest = Value::object();
    rest["name"] = nullptr;
    rest["frames"] = 1.0;
    rest["loop"] = true;
    rest["tracks"] = Value::object();
    rest["matrices"] = Value::object();
    Value actor = Value::object();
    actor["name"] = prefix;
    actor["model"] = e.model.name;
    actor["bones"] = bones;
    actor["animation"] = rest;
    actor["curve_clips"] = clips;
    actor["curve_clip_visibility"] = clipVisibility;
    actor["visibility_overlays"] = overlays;
    actor["material_base"] = e.materialBase;
    actor["visibility"] = Value::object();
    actor["meshes"] = entries;
    actor["first_vertex"] = first;
    actor["vertex_count"] = skin.size() / 72;
    actor["translation"] = xyz(o.at("Translate"));
    actor["rotation"] = xyz(o.at("Rotate"));
    actor["scale"] = xyz(o.at("Scale"));
    actor["frames_per_second"] = 60;
    actor["frame_offset"] = 0;
    actor["object"] = objectId;
    if (hasMember(e.archive, "AnimInterp.byml")) {
        actor["anim_interp"] = in_.byml(e.archive, "AnimInterp.byml");
    }
    actor["source"] = "rom:" + e.archive;
    Value ref = Value::object();
    ref["name"] = prefix;
    ref["file"] = "actors/" + prefix + ".json";
    ref["skin"] = "actors/" + prefix + "-skin.bin";
    actors_.push(ref);
    actorFiles_.emplace_back(prefix, actor);
    actorSkins_[prefix] = std::move(skin);
}

// The placement as a stage object (scene.json objects) for the session's object runtime: the
// creator-table class (UnitConfig.ParameterConfigName, which selects the factory entry at
// 0x48a1cc), the unit and model names, the placement matrix, the placement's own scalar
// parameters and its links (target id, unit and placement matrix: link targets such as rail
// points, move targets and stage switches are not themselves in the object lists). The object's
// meshes and collision part carry its id.
void StageBuilder::addObject(const Value& o, const std::string& unit, const std::string& model, const M34& placement) {
    Value obj = Value::object();
    obj["id"] = o.at("Id");
    const Value* uc = o.find("UnitConfig");
    const Value* cls = uc && uc->isObject() ? uc->find("ParameterConfigName") : nullptr;
    obj["class"] = cls && cls->isString() ? cls->str() : unit;
    obj["unit"] = unit;
    obj["model"] = model;
    obj["matrix"] = matrixJson(placement);
    Value params = Value::object();
    for (const auto& [k, v] : o.members()) {
        if (!v.isObject() && !v.isArray() && k != "Id" && k != "UnitConfigName" && k != "Comment") {
            params[k] = v;
        }
    }
    obj["params"] = params;
    Value links = Value::object();
    if (const Value* l = o.find("Links"); l && l->isObject()) {
        for (const auto& [k, v] : l->members()) {
            if (!v.isArray()) {
                continue;
            }
            Value targets = Value::array();
            for (const auto& t : v.items()) {
                if (t.isObject() && t.has("Id")) {
                    Value target = Value::object();
                    target["id"] = t.at("Id");
                    target["unit"] = t.has("UnitConfigName") ? t.at("UnitConfigName") : Value("");
                    if (t.has("Translate")) {
                        target["matrix"] = matrixJson(placementMatrix(t));
                    }
                    if (const Value* rp = t.find("RailPoints"); rp && rp->isArray()) {   // rails: points
                        Value points = Value::array();
                        for (const auto& p : rp->items()) {
                            points.push(xyz(p.at("Translate")));
                        }
                        target["points"] = points;
                        target["closed"] = t.has("IsClosed") ? t.at("IsClosed") : Value(false);
                    }
                    targets.push(target);
                }
            }
            links[k] = targets;
        }
    }
    obj["links"] = links;
    // The object archive's sensor declarations (InitSensor.byml: name, radius, type, joint and
    // offsets); the session's contact tests use them as the original's sensors.
    const std::string archive = "ObjectData/" + model + ".szs";
    if (Inputs::has(archive) && hasMember(archive, "InitSensor.byml")) {
        obj["sensors"] = in_.byml(archive, "InitSensor.byml");
    }
    // Its effect and sound entries (InitEffect.byml Name, InitAudio.byml SeUserName) and hit
    // reactions (ActorHitReactionCtrl.byml: reaction name -> effect, sound PlayInfo, camera shake).
    if (Inputs::has(archive)) {
        if (hasMember(archive, "InitEffect.byml")) {
            const Value v = in_.byml(archive, "InitEffect.byml");
            if (const Value* n = v.isObject() ? v.find("Name") : nullptr; n && n->isString()) {
                obj["effect_user"] = n->str();
                effectUsers_.insert(n->str());
            }
        }
        if (hasMember(archive, "InitAudio.byml")) {
            const Value v = in_.byml(archive, "InitAudio.byml");
            if (const Value* n = v.isObject() ? v.find("SeUserName") : nullptr; n && n->isString()) {
                obj["sound_user"] = n->str();
                soundUsers_.insert(n->str());
            }
        }
        if (hasMember(archive, "ActorHitReactionCtrl.byml") && !hitReactions_.has(model)) {
            const Value v = in_.byml(archive, "ActorHitReactionCtrl.byml");
            if (v.isArray()) {
                hitReactions_[model] = v;
            }
        }
    }
    // Its body box (BodyBox.byml Min / Max, e.g. KyoroHei's hurting box, init 0x3cf3d0) as the
    // parameters BodyBoxMinX .. BodyBoxMaxZ.
    if (Inputs::has(archive) && hasMember(archive, "BodyBox.byml")) {
        const Value box = in_.byml(archive, "BodyBox.byml");
        for (const char* corner : {"Min", "Max"}) {
            const Value* c = box.isObject() ? box.find(corner) : nullptr;
            for (const char* axis : {"X", "Y", "Z"}) {
                if (c && c->isObject() && c->has(axis)) {
                    obj["params"][std::string("BodyBox") + corner + axis] = c->at(axis);
                }
            }
        }
    }
    // Its touch points (InitScreenPoint.byml): the spheres the touch screen picks objects by.
    if (Inputs::has(archive) && hasMember(archive, "InitScreenPoint.byml")) {
        obj["screen_points"] = in_.byml(archive, "InitScreenPoint.byml");
    }
    objects_.push(obj);
}

// The object's KCL (if any) as a collision part of the recovered collider, and as world
// triangles for the collision overlay and the headlamp's beam length.
void StageBuilder::placeCollision(const std::string& name, const M34& placement, const Value& o) {
    const std::string archive = "ObjectData/" + name + ".szs";
    if (!Inputs::has(archive)) {
        return;
    }
    // The actor init creates a collision only from InitCollision.byml (its Sensor names the
    // CollisionParts sensor); a KCL without it is unused (RevolverRuinsLeaf: the box under its
    // leaves, which the original's protagonist walks through).
    if (hasMember(archive, "InitCollision.byml")) {
        placeCollisionFile(name, name, placement, o, false);
    }
    // The locked door's door panel (init 0x3ccc3c: the collision actor "DoorLockDoor", 0x2f0064,
    // switched off as the door opens): slot 1 of the door object (the frame is slot 0).
    if (name == "DoorLock" && hasMember(archive, "DoorLockDoor.kcl") && collisionParts_.size() > 0 &&
        collisionParts_[collisionParts_.size() - 1].at("object").str() == o.at("Id").str()) {
        collisionParts_[collisionParts_.size() - 1]["slot"] = 0;
        placeCollisionFile(name, "DoorLockDoor", placement, o, false);
        collisionParts_[collisionParts_.size() - 1]["slot"] = 1;
    }
    // Collision sub-actors of the same model (InitSubActor entries with FixFileSuffixName S and
    // this ModelName: the sub-actor inits with the archive's Init*S files; its InitCollisionS
    // names the KCL and its SpecialPurpose, e.g. RevolverRuinsStep's "PlayerOnly" ramp over the
    // stairs out of the water, a collision only the player collides with).
    if (!hasMember(archive, "InitSubActor.byml")) {
        return;
    }
    const Value init = in_.byml(archive, "InitSubActor.byml");
    const Value* list = init.isObject() ? init.find("CreatorList") : nullptr;
    if (!list || !list->isArray()) {
        return;
    }
    for (const auto& c : list->items()) {
        const Value* suffix = c.find("FixFileSuffixName");
        const Value* model = c.find("ModelName");
        if (!suffix || !suffix->isString() || !model || !model->isString() || model->str() != name ||
            !hasMember(archive, "InitCollision" + suffix->str() + ".byml")) {
            continue;
        }
        const Value col = in_.byml(archive, "InitCollision" + suffix->str() + ".byml");
        const Value* kclName = col.isObject() ? col.find("Name") : nullptr;
        const Value* purpose = col.isObject() ? col.find("SpecialPurpose") : nullptr;
        if (kclName && kclName->isString()) {
            placeCollisionFile(name, kclName->str(), placement, o,
                               purpose && purpose->isString() && purpose->str() == "PlayerOnly");
        }
    }
}

// One KCL of an object archive as a collision part (`kclName`.kcl; attributes in
// `kclName`Attribute.byml); `playerOnly`: registered for the player only.
void StageBuilder::placeCollisionFile(const std::string& name, const std::string& kclName, const M34& placement,
                                      const Value& o, bool playerOnly) {
    const std::string archive = "ObjectData/" + name + ".szs";
    if (!hasMember(archive, kclName + ".kcl")) {
        return;
    }
    const Bytes& kcl = in_.file(archive, kclName + ".kcl");
    std::vector<std::array<std::array<double, 3>, 3>> triangles;
    try {
        kclGeometry(kcl, triangles, true);
    } catch (const ImportError& err) {
        note("unread", archive + "/" + kclName + ".kcl: " + err.what());
        return;
    }
    if (getenv("DIORAMA_BAKE_DEBUG")) {
        double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
        for (const auto& t : triangles) {
            for (const auto& v : t) {
                for (int k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], v[size_t(k)]);
                    hi[k] = std::max(hi[k], v[size_t(k)]);
                }
            }
        }
        fprintf(stderr, "collision %s %s: %zu triangles, %g %g %g .. %g %g %g\n", name.c_str(), kclName.c_str(),
                triangles.size(), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    }
    const std::string file = "collision/" + (kclName == name ? name : name + "-" + kclName) + ".kcl";
    if (!fs::exists(out_ / file)) {
        writeFile(out_ / file, kcl);
    }
    for (const auto& t : triangles) {
        for (const auto& v : t) {
            const auto w = apply(placement, v.data(), true);
            pack(collision_, {w[0], w[1], w[2]});
        }
    }
    const Value& s = o.at("Scale");
    if (s.at("X").num() != s.at("Y").num() || s.at("X").num() != s.at("Z").num()) {
        note("non_uniform_collision", name + " " + o.at("Id").str());
    }
    Value part = Value::object();
    part["file"] = file;
    part["matrix"] = matrixJson(placement);
    // The collider queries in model space: radius * scale (model units per world unit) and the
    // depth back * depth_scale (Collision.hpp Part +0x110 / +0x114).
    part["scale"] = 1.0 / s.at("X").num();
    part["depth_scale"] = s.at("X").num();
    part["object"] = o.at("Id");
    // Floor materials of its attributes (<name>Attribute.byml MaterialCode; the sound and effect
    // keepers' floor variants, docs/sound.md).
    if (playerOnly) {
        part["player_only"] = true;
    }
    if (hasMember(archive, kclName + "Attribute.byml")) {
        Value materials = Value::array();
        const Value attributes = in_.byml(archive, kclName + "Attribute.byml");
        if (attributes.isArray()) {
            for (const auto& a : attributes.items()) {
                materials.push(a.isObject() && a.has("MaterialCode") ? a.at("MaterialCode") : Value(""));
            }
        }
        part["materials"] = materials;
    }
    collisionParts_.push(part);
}

// Links whose targets the owning class's init creates as actors (the factory routine 0x2d2398 by
// link name and index; map parts' ChildStep through 0x29fedc -> 0x3331b4). Such targets are placed
// in the stage files only as links (IsLinkDest). Ported so far: the ones the courses need.
//   0x178270 AllDeadWatcher WatchTargetEnemy (e.g. RevolverRuinsStage's bridge)
//   0x29fedc map parts' ChildStep (the steps of a ClockMapParts)
// Not followed yet: GrassObj (0x1670bc, 0x167494), Member (0x172518, 0x1b2268), WatchTargetObj
// (0x18c654), EffectUseActor (0x1de790), ClippingTarget (0x1f7acc), ShuffleItem / RareItem
// (0x26c7ec, 0x26c8a4), BreakParts (0x3c013c).
// GrassObj: GrassControl init 0x167438; LinkMoking: MokingLeader init 0x1692f0 creates one Moking
// (0x3b6698) per link; TrampleSwitchTogetherList: the switches of a TrampleSwitchTogetherWatcher.
const char* const kCreatedLinks[] = {"WatchTargetEnemy", "ChildStep", "GrassObj", "LinkMoking",
                                     "TrampleSwitchTogetherList"};

// The placements of an object list in creation order: each entry, then the actors its links
// create (depth first), each once.
static void collectPlacements(const Value& o, std::vector<const Value*>& out, std::set<std::string>& seen) {
    if (!o.isObject() || !o.has("Id") || !seen.insert(o.at("Id").str()).second) {
        return;
    }
    out.push_back(&o);
    const Value* links = o.find("Links");
    if (!links || !links->isObject()) {
        return;
    }
    for (const char* name : kCreatedLinks) {
        const Value* targets = links->find(name);
        if (targets && targets->isArray()) {
            for (const auto& t : targets->items()) {
                collectPlacements(t, out, seen);
            }
        }
    }
}

// Actor2dData/InGameUIA2Ddata.bin, the in-game 2D actors: a directory of 0x3c-byte entries from
// +0xc up to the first block (name, 0x24 bytes; block offset; header size; data size). A block's
// header holds a u32 size, a u32 sequence count and 0x2c-byte sequence entries (name, 0x24 bytes;
// u32 key count; u32 offset of the sequence record). A sequence record: key count, flags (bit 0
// loop), the texture window, the key table offset, the quad corners (s16 pairs, 32 units per
// texel) and the texture size; a key (0x18 bytes): u16 cell, u16 x, u16 y, s16 y offset, u16
// flags, u16 scale x, u32 scale y (32 = 1), u32 colour (RGBA bytes), u32 second colour. The block's
// data is a CTPK texture package (u16 count at +6; 0x20-byte entries from +0x20: name offset,
// size, data offset, format, u16 width, u16 height; data from the u32 at +8).
// Returns {sequences: {name: {frames, loop, keys: [[y offset, scale x, scale y, r, g, b, a]]}},
// quad: [x0, y0, x1, y1], textures: {name: file}} and writes the textures as RGBA files.
Value StageBuilder::actor2d(const std::string& actor) {
    Value out = Value::object();
    const std::string path = "Actor2dData/InGameUIA2Ddata.bin";
    if (!Inputs::has(path)) {
        return out;
    }
    const Bytes& b = in_.romfs(path);
    auto u32 = [&](size_t at) -> uint32_t {
        return at + 4 <= b.size()
                   ? uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24
                   : 0;
    };
    auto u16 = [&](size_t at) -> uint32_t {
        return at + 2 <= b.size() ? uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 : 0;
    };
    auto s16 = [&](size_t at) {
        return double(int16_t(u16(at)));
    };
    auto name = [&](size_t at, size_t max) {
        std::string n;
        for (size_t k = at; k < at + max && k < b.size() && b[k]; ++k) {
            n += char(b[k]);
        }
        return n;
    };
    const size_t first = u32(0x30);
    for (size_t e = 0xc; e + 0x3c <= first; e += 0x3c) {
        if (name(e, 0x24) != actor) {
            continue;
        }
        const size_t block = u32(e + 0x24), header = u32(e + 0x28), dataSize = u32(e + 0x2c);
        Value sequences = Value::object();
        Value quad = Value::array();
        const uint32_t count = u32(block + 4);
        for (uint32_t k = 0; k < count; ++k) {
            const size_t entry = block + 8 + k * 0x2c;
            const size_t seq = block + u32(entry + 0x28);
            const uint32_t frames = u32(seq);
            const size_t keys = block + u32(seq + 0x18);
            Value v = Value::object();
            v["frames"] = double(frames);
            v["loop"] = (u32(seq + 4) & 1) != 0;
            Value list = Value::array();
            for (uint32_t f = 0; f < frames; ++f) {
                const size_t key = keys + f * 0x18;
                const uint32_t colour = u32(key + 0x10);
                list.push(doubles({s16(key + 6), double(u16(key + 0xa)) / 32.0, double(u32(key + 0xc)) / 32.0,
                                   double(colour & 255), double((colour >> 8) & 255), double((colour >> 16) & 255),
                                   double(colour >> 24)}));
            }
            v["keys"] = list;
            sequences[name(entry, 0x24)] = v;
            if (quad.size() == 0) {   // the corners (s16 x / y pairs at +0x1c; 32 units per texel)
                double lo[2] = {1e9, 1e9}, hi[2] = {-1e9, -1e9};
                for (int c = 0; c < 4; ++c) {
                    for (int a = 0; a < 2; ++a) {
                        const double x = s16(seq + 0x1c + c * 4 + a * 2) / 32.0;
                        lo[a] = std::min(lo[a], x);
                        hi[a] = std::max(hi[a], x);
                    }
                }
                quad = doubles({lo[0], lo[1], hi[0], hi[1]});
            }
        }
        // The CTPK textures.
        Value textures = Value::object();
        const size_t ctpk = block + header;
        if (ctpk + 0x20 <= b.size() && std::memcmp(&b[ctpk], "CTPK", 4) == 0 && ctpk + dataSize <= b.size()) {
            const uint32_t n = u16(ctpk + 6);
            const size_t data = ctpk + u32(ctpk + 8);
            for (uint32_t t = 0; t < n; ++t) {
                const size_t te = ctpk + 0x20 + t * 0x20;
                const std::string tex = name(ctpk + u32(te), 0x40);
                const uint32_t size = u32(te + 4), offset = u32(te + 8), format = u32(te + 0xc);
                const uint32_t w = u16(te + 0x10), h = u16(te + 0x12);
                const Bytes raw(b.begin() + long(data + offset), b.begin() + long(data + offset + size));
                std::string stem = tex.substr(0, tex.find('.'));
                writeFile(out_ / "actor2d" / (stem + ".rgba"), decodePica(raw, w, h, format));
                Value tv = Value::object();
                tv["file"] = "actor2d/" + stem + ".rgba";
                tv["size"] = doubles({double(w), double(h)});
                textures[stem] = tv;
            }
        }
        out["sequences"] = sequences;
        out["quad"] = quad;
        out["textures"] = textures;
        break;
    }
    return out;
}

void StageBuilder::placements() {
    std::map<std::string, int> counts;
    std::set<std::string> seen;
    for (const char* list : kObjectLists) {
        const Value* l = map_.find(list);
        if (!l || !l->isArray()) {
            continue;
        }
        std::vector<const Value*> entries;
        for (const auto& o : l->items()) {
            collectPlacements(o, entries, seen);
        }
        for (const Value* entry : entries) {
            const Value& o = *entry;
            if (!o.isObject() || !o.has("UnitConfigName")) {
                continue;
            }
            const std::string unit = o.at("UnitConfigName").str();
            const Value* mn = o.find("ModelName");
            const std::string name = mn && mn->isString() ? mn->str() : unit;
            const M34 placement = placementMatrix(o);
            // The archive the class's init loads: named by the model, else by the unit (with the
            // model inside it, e.g. SignBoardWood / SignBoardWoodLeft), else by the code
            // (kArchiveOfUnit).
            ModelEntry* e = modelFor(name);
            std::string archiveName = name;
            if (!e && name != unit) {
                e = modelFor(unit, name);
                archiveName = unit;
            }
            if (!e) {   // variants of a class load the class's archive (TransparentWallPlayerOnly)
                const Value* uc = o.find("UnitConfig");
                const Value* cls = uc && uc->isObject() ? uc->find("ParameterConfigName") : nullptr;
                if (cls && cls->isString() && cls->str() != unit && cls->str() != name &&
                    Inputs::has("ObjectData/" + cls->str() + ".szs")) {
                    e = modelFor(cls->str());
                    if (e || !Inputs::has("ObjectData/" + archiveName + ".szs")) {
                        archiveName = cls->str();   // also for collision-only archives
                    }
                }
            }
            if (!e) {
                for (const auto& [u, a] : kArchiveOfUnit) {
                    if (unit == u) {
                        e = modelFor(a);
                        archiveName = a;
                    }
                }
            }
            const Value* grass = grassControlFor(o.at("Id").str());
            if (e && !e->visible) {
                counts[name + " (shadow only, not drawn)"] += 1;
            } else if (e && grass) {
                // A GrassControl's grass (link GrassObj, init 0x167438): a posed actor whose single
                // Grass00Root chain the recovered wind sways (game/GrassWind.hpp, as the opening's).
                placeActor(*e, o, o.at("Id").str());
                Value gc = Value::object();
                gc["chain"] = strings({"Grass00Root", "Grass00Tip01", "Grass00Tip02"});
                gc["bend_rate"] = grass->at("BendRate");
                gc["slide_max"] = grass->at("SlideMax");
                gc["wind_x"] = grass->at("BaseWindX");
                gc["wind_z"] = grass->at("BaseWindZ");
                gc["time_shift"] = grass->at("TimeShift");
                gc["frames"] = 90;       // 0x452ca8 (+0xcc)
                gc["start_index"] = 1;   // hypothesis: index 0 at creation, one update before the first frame
                actorFiles_.back().second["grass_control"] = gc;
                counts[name + " (grass)"] += 1;
            } else if (e && animated(*e)) {
                try {
                    placeActor(*e, o, o.at("Id").str());
                } catch (const std::out_of_range& err) {
                    throw ImportError("animated object " + name + ": " + err.what());
                } catch (const ImportError& err) {
                    throw ImportError("animated object " + name + ": " + err.what());
                }
                counts[name + " (animated)"] += 1;
            } else if (e) {
                placeModel(*e, placement, o.at("Id").str());
                counts[name] += 1;
            } else if (!missingModels_.count(name)) {
                missingModels_.insert(name);
                note("without_model", unit + (name != unit ? " (" + name + ")" : ""));
            }
            const size_t partsBefore = collisionParts_.size();
            placeCollision(archiveName, placement, o);
            // A conveyor (ConveyerMapParts, init 0x247698) moves floor(length / PartsInterval) + 1
            // copies of its collision along the path to KeyMoveNext: one part per copy ("slot").
            const Value* ucv = o.find("UnitConfig");
            const Value* pcn = ucv && ucv->isObject() ? ucv->find("ParameterConfigName") : nullptr;
            if (pcn && pcn->isString() && pcn->str() == "ConveyerMapParts" &&
                collisionParts_.size() == partsBefore + 1) {
                const Value* l = o.find("Links");
                const Value* next = l && l->isObject() ? l->find("KeyMoveNext") : nullptr;
                if (next && next->isArray() && next->size() > 0) {
                    const M34 to = placementMatrix((*next)[0]);
                    const double dx = to[3] - placement[3], dy = to[7] - placement[7], dz = to[11] - placement[11];
                    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
                    double interval = o.has("PartsInterval") ? o.at("PartsInterval").num() : 0;
                    interval = interval <= 10 ? 10 : interval;   // literals 0x2479bc / 0x2479c0
                    const int count = int(float(length) / float(interval)) + 1;
                    const Value first = collisionParts_[partsBefore];
                    for (int k = 0; k < count; ++k) {
                        Value part = first;
                        part["slot"] = k;
                        if (k == 0) {
                            collisionParts_[partsBefore] = part;
                        } else {
                            collisionParts_.push(part);
                        }
                    }
                }
            }
            addObject(o, unit, archiveName, placement);
            if (e) {
                shareMaterialAnimations(archiveName, *e);
            }
            // The transparent coin's real coin (init 0x16f8d4: a Coin actor of the variant
            // "コイン[透明コイン]" created dead at the placement, made alive at the end of the
            // reveal, 0x16f2f0): a hidden object "<coin>/coin" of the Coin class.
            if (objects_[objects_.size() - 1].at("class").str() == "CoinChameleon") {
                if (ModelEntry* coin = modelFor("Coin")) {
                    Value spawn = o;
                    spawn["Id"] = o.at("Id").str() + "/coin";
                    spawn["UnitConfigName"] = "Coin";
                    spawn["UnitConfig"]["ParameterConfigName"] = "Coin";
                    spawn["ModelName"] = nullptr;
                    spawn["Links"] = Value::object();
                    spawn["SpawnedBy"] = o.at("Id");
                    if (animated(*coin)) {
                        placeActor(*coin, spawn, spawn.at("Id").str());
                    } else {
                        placeModel(*coin, placement, spawn.at("Id").str());
                    }
                    addObject(spawn, "Coin", "Coin", placement);
                    objects_[objects_.size() - 1]["effect_user"] = "Coin";
                    objects_[objects_.size() - 1]["sound_user"] = "Coin";
                    effectUsers_.insert("Coin");
                    soundUsers_.insert("Coin");
                }
            }
            // Items that objects bring out at run time (KinopioMemberNpc with
            // KinopioMemberNpcAppearItem "CollectItem": the gem taken out by TakeOut 0x20df48 via
            // 0x151370): a hidden object of the item's class, spawned_by the carrier.
            const Value* appear = o.find("KinopioMemberNpcAppearItem");
            if (appear && appear->isString() && appear->str() == "CollectItem") {
                if (ModelEntry* item = modelFor("CollectItem")) {
                    Value spawn = o;
                    spawn["Id"] = o.at("Id").str() + "/item";
                    spawn["UnitConfigName"] = "CollectItem";
                    spawn["UnitConfig"]["ParameterConfigName"] = "CollectItem";
                    spawn["ModelName"] = nullptr;
                    spawn["Links"] = Value::object();
                    spawn["SpawnedBy"] = o.at("Id");
                    placeActor(*item, spawn, spawn.at("Id").str());
                    addObject(spawn, "CollectItem", "CollectItem", placement);
                }
            }
            // The coin a coin plant brings out (InitItem コインx1[自動取得] -> the item generator
            // 0x3e5c1c takes a pooled "Coin" actor of the CountUp variant, vtable 0x4bdad8): a
            // hidden object "<plant>/coin" with the CountUp variant's effect, sound and reaction
            // tables (InitActorCountUp: InitEffect Coin, SeUserName Coin, reactions 出現 / 消滅).
            const Value* itemType = o.find("ItemType");
            if (unit.rfind("PullOutObj", 0) == 0 && unit != "PullOutObjAssist" && itemType && itemType->isString() &&
                itemType->str() == "Coin") {
                if (ModelEntry* coin = modelFor("Coin")) {
                    Value spawn = o;
                    spawn["Id"] = o.at("Id").str() + "/coin";
                    spawn["UnitConfigName"] = "Coin";
                    spawn["UnitConfig"]["ParameterConfigName"] = "ItemCoinCountUp";
                    spawn["ModelName"] = nullptr;
                    spawn["Links"] = Value::object();
                    spawn["SpawnedBy"] = o.at("Id");
                    if (animated(*coin)) {
                        placeActor(*coin, spawn, spawn.at("Id").str());
                    } else {
                        placeModel(*coin, placement, spawn.at("Id").str());
                    }
                    addObject(spawn, "Coin", "Coin", placement);
                    objects_[objects_.size() - 1]["effect_user"] = "Coin";
                    objects_[objects_.size() - 1]["sound_user"] = "Coin";
                    objects_[objects_.size() - 1]["reaction_table"] = "Coin/CountUp";
                    effectUsers_.insert("Coin");
                    soundUsers_.insert("Coin");
                    if (!hitReactions_.has("Coin/CountUp") &&
                        hasMember("ObjectData/Coin.szs", "ActorHitReactionCtrlCountUp.byml")) {
                        hitReactions_["Coin/CountUp"] =
                            in_.byml("ObjectData/Coin.szs", "ActorHitReactionCtrlCountUp.byml");
                    }
                }
            }
            // The touch hints' 2D actor (GuideAppearPoint init 0x1a25cc: 0x2d4e14 "GUIDE_BALLOON"
            // of the in-game 2D actor data): its sequence lengths time the hint's nerves.
            if (unit == "GuideAppearPoint" && !scene_.has("actor2d")) {
                scene_["actor2d"]["GUIDE_BALLOON"] = actor2d("GUIDE_BALLOON");
            }
            // The item a plant brings out (PullOutObj InitItem by ItemType, item generator 0x3e5c1c):
            //   Radish      -> カブ[引っこ抜き]          pooled "Radish" (vtable 0x4cf830, init 0x3bb2ec)
            //   KinokoSuper -> スーパーキノコ[真上出現]  pooled "KinokoSuper" (vtable 0x4bdfe4, init 0x155800)
            //   PickaxeItem -> 無敵ツルハシ[真上出現]    pooled "PickaxeItem" (vtable 0x4be2ac, init 0x1570cc)
            // Each is a hidden object "<plant>/<suffix>" with the archive's model, sensors, effect,
            // sound and hit reactions (InitEffect / SeUserName / ActorHitReactionCtrl, all named
            // after the archive).
            struct PlantItem {
                const char* itemType;
                const char* suffix;
                const char* archive;
            };
            static const PlantItem kPlantItems[] = {
                {"Radish", "radish", "Radish"},
                {"KinokoSuper", "kinoko", "KinokoSuper"},
                {"PickaxeItem", "pickaxe", "PickaxeItem"},
                // DoorKey -> 扉カギ: the plant's own key (init 0x143fa8 builds it dead, 0x3c42e0;
                // the keeper starts it at the item position, 0x2ecdb4 -> 0x3c39c0).
                {"DoorKey", "key", "DoorKey"},
                // KinokoTreasure -> お宝キノコ / お宝キノコ[地形追従] (PullOutObjZoom's item, 0x185880).
                {"KinokoTreasure", "treasure", "KinokoTreasure"},
            };
            if (unit.rfind("PullOutObj", 0) == 0 && unit != "PullOutObjAssist" && itemType && itemType->isString()) {
                for (const PlantItem& pi : kPlantItems) {
                    if (itemType->str() != pi.itemType) {
                        continue;
                    }
                    ModelEntry* model = modelFor(pi.archive);
                    if (!model) {
                        break;
                    }
                    Value spawn = o;
                    spawn["Id"] = o.at("Id").str() + "/" + pi.suffix;
                    spawn["UnitConfigName"] = pi.archive;
                    spawn["UnitConfig"]["ParameterConfigName"] = pi.archive;
                    spawn["ModelName"] = nullptr;
                    spawn["Links"] = Value::object();
                    spawn["SpawnedBy"] = o.at("Id");
                    if (animated(*model)) {
                        placeActor(*model, spawn, spawn.at("Id").str());
                    } else {
                        placeModel(*model, placement, spawn.at("Id").str());
                    }
                    addObject(spawn, pi.archive, pi.archive, placement);
                    objects_[objects_.size() - 1]["effect_user"] = pi.archive;
                    objects_[objects_.size() - 1]["sound_user"] = pi.archive;
                    objects_[objects_.size() - 1]["reaction_table"] = pi.archive;
                    effectUsers_.insert(pi.archive);
                    soundUsers_.insert(pi.archive);
                    const std::string arc = std::string("ObjectData/") + pi.archive + ".szs";
                    if (!hitReactions_.has(pi.archive) && hasMember(arc, "ActorHitReactionCtrl.byml")) {
                        hitReactions_[pi.archive] = in_.byml(arc, "ActorHitReactionCtrl.byml");
                    }
                    break;
                }
            }
            // The wooden sign's sub-actors (init 0x3dd4c8: InitSubActor of its archive, the break
            // model BreakModel "SignBoardWoodBreak" and the plain model "SignBoardWoodTrace", both
            // created dead; with IsSnowCover the snow cover "SignBoardSnowCover", 0x2c9590): hidden
            // or shown objects "<sign>/<model>", spawned_by the sign, which shows them when it breaks.
            {
                const Value* uc = o.find("UnitConfig");
                const Value* cls = uc && uc->isObject() ? uc->find("ParameterConfigName") : nullptr;
                const std::string arc = "ObjectData/" + archiveName + ".szs";
                // Classes whose init creates its InitSubActor models (the sign 0x3dd4c8; KyoroHei
                // 0x3cf3d0: its break model KyoroHeiBreak, 壊れモデル).
                if (e && cls && cls->isString() && (cls->str() == "SignBoard" || cls->str() == "KyoroHei")) {
                    struct Sub {
                        std::string model, cls;
                        bool alive;
                    };
                    std::vector<Sub> subs;
                    if (hasMember(arc, "InitSubActor.byml")) {
                        const Value init = in_.byml(arc, "InitSubActor.byml");
                        if (const Value* list = init.isObject() ? init.find("CreatorList") : nullptr;
                            list && list->isArray()) {
                            for (const auto& c : list->items()) {
                                const Value* m = c.find("ModelName");
                                const Value* k = c.find("ClassName");
                                const Value* a = c.find("IsAlive");
                                if (m && m->isString()) {
                                    subs.push_back({m->str(), k && k->isString() ? k->str() : "SubActorModel",
                                                    a && a->type() == Value::Type::Bool && a->boolean()});
                                }
                            }
                        }
                    }
                    const Value* snow = o.find("IsSnowCover");
                    if (snow && snow->type() == Value::Type::Bool && snow->boolean()) {
                        subs.push_back({"SignBoardSnowCover", "SignBoardSnowCover", true});
                    }
                    for (const Sub& sub : subs) {
                        ModelEntry* m = modelFor(sub.model);
                        if (!m) {
                            continue;
                        }
                        Value spawn = o;
                        spawn["Id"] = o.at("Id").str() + "/" + sub.model;
                        spawn["UnitConfigName"] = sub.model;
                        spawn["UnitConfig"]["ParameterConfigName"] = sub.cls;
                        spawn["ModelName"] = nullptr;
                        spawn["Links"] = Value::object();
                        spawn["SpawnedBy"] = o.at("Id");
                        spawn["IsAliveAtStart"] = sub.alive;
                        if (animated(*m)) {
                            placeActor(*m, spawn, spawn.at("Id").str());
                        } else {
                            placeModel(*m, placement, spawn.at("Id").str());
                        }
                        addObject(spawn, sub.cls, sub.model, placement);
                    }
                }
            }
            // Break pieces of a brick (IsAppearBreakModel; init 0x1cea00 loads "<model>Break" and
            // places it at the brick): a hidden actor, spawned_by the brick, that plays "Break".
            // A hard block always makes its pieces (init 0x3d51d0: BlockHardBreak, "Break").
            const Value* breakModel = o.find("IsAppearBreakModel");
            const bool hard = unit == "BlockHard";
            if (hard || (unit == "BlockBrickBreakable" && breakModel && breakModel->type() == Value::Type::Bool &&
                         breakModel->boolean())) {
                const std::string pieces = hard ? std::string("BlockHardBreak") : archiveName + "Break";
                const std::string piecesClass = hard ? "BlockHardBreak" : "BlockBrickBreak";
                if (ModelEntry* piecesModel = modelFor(pieces)) {
                    Value spawn = o;
                    spawn["Id"] = o.at("Id").str() + "/break";
                    spawn["UnitConfigName"] = piecesClass;
                    spawn["UnitConfig"]["ParameterConfigName"] = piecesClass;
                    spawn["ModelName"] = nullptr;
                    spawn["Links"] = Value::object();
                    spawn["SpawnedBy"] = o.at("Id");
                    placeActor(*piecesModel, spawn, spawn.at("Id").str());
                    addObject(spawn, piecesClass, pieces, placement);
                }
            }
        }
    }
    // The protagonist's pickaxe (InitSubActor: WarpedMtxPartsModel "Pickaxe", fixed to HandR by
    // InitPartsFixInfoPickaxe; shown in the invincible mode): a hidden object the session places on
    // the joint, with the Pickaxe archive's sensor and hit reactions.
    if (Inputs::has("ObjectData/Pickaxe.szs")) {
        if (ModelEntry* pickaxe = modelFor("Pickaxe")) {
            Value spawn = Value::object();
            spawn["Id"] = "player/pickaxe";
            spawn["UnitConfigName"] = "Pickaxe";
            Value uc = Value::object();
            uc["ParameterConfigName"] = "PlayerPickaxe";
            spawn["UnitConfig"] = uc;
            M34 identity{};
            identity[0] = identity[5] = identity[10] = 1;
            if (animated(*pickaxe)) {
                placeActor(*pickaxe, spawn, "player/pickaxe");
            } else {
                placeModel(*pickaxe, identity, "player/pickaxe");
            }
            addObject(spawn, "Pickaxe", "Pickaxe", identity);
            Value& obj = objects_[objects_.size() - 1];
            obj["reaction_table"] = "Pickaxe";
            // A joint sensor sits at the joint's matrix times its offset (0x331638 -> 0x2fd284);
            // the model is not animated, so its joints keep their rest pose (JointRoot is off
            // the root): the rest matrix relative to the model root.
            if (obj.has("sensors") && obj.at("sensors").type() == Value::Type::Array) {
                for (Value& sd : obj["sensors"].items()) {
                    if (!sd.has("Joint") || sd.at("Joint").type() != Value::Type::String) {
                        continue;
                    }
                    const auto& bones = pickaxe->model.bones;
                    for (size_t b = 0; b < bones.size() && b < pickaxe->boneWorld.size(); b++) {
                        if (bones[b].name == sd.at("Joint").str()) {
                            sd["joint_rest"] = matrixJson(pickaxe->boneWorld[b]);
                            break;
                        }
                    }
                }
            }
            if (!hitReactions_.has("Pickaxe") && hasMember("ObjectData/Pickaxe.szs", "ActorHitReactionCtrl.byml")) {
                hitReactions_["Pickaxe"] = in_.byml("ObjectData/Pickaxe.szs", "ActorHitReactionCtrl.byml");
            }
        }
    }
    // The goal object's star (GoalList entry, Links.GoalItem): placed like the objects, and its
    // root (Translate + UnitConfig.DisplayTranslate, as in the goal initialiser 0x1ec824) for the
    // player's Head / star Body contact test of the goal demo (Goal.hpp headTouchesStar).
    if (const Value* goals = map_.find("GoalList"); goals && goals->isArray() && goals->size() > 0) {
        const Value& goal = (*goals)[0];
        const Value* links = goal.find("Links");
        const Value* items = links && links->isObject() ? links->find("GoalItem") : nullptr;
        if (items && items->isArray() && items->size() > 0) {
            const Value& item = (*items)[0];
            if (ModelEntry* e = modelFor("GoalItem")) {
                placeActor(*e, item, item.at("Id").str());   // turns with its "Wait" (run loadB)
                shareMaterialAnimations("GoalItem", *e);
                counts["GoalItem"] += 1;
            }
            addObject(item, item.at("UnitConfigName").str(), "GoalItem", placementMatrix(item));
            Value root = Value::array();
            for (const char* k : {"X", "Y", "Z"}) {
                root.push(item.at("Translate").at(k).num() + item.at("UnitConfig").at("DisplayTranslate").at(k).num());
            }
            // The goal demo (goal object init 0x1ec824, goal demo 0x1e0868; game/Goal.hpp): the
            // goal object's place and front, the code's camera literals (0x1ecc8c..0x1ecc94) and
            // demo steps (+0x18 96, +0x1c 160), solo or pair poses (a PlayerNpc link), the
            // clear-after demo flag, and a second GoalItem model, the demo star (init: made from the
            // GoalItem's placement; posed at the goal by the Pose nerve 0x1df6a8), as a hidden
            // object "<star>/demo".
            const bool pair = links->find("PlayerNpc") != nullptr;
            Value d = Value::object();
            d["position"] = xyz(goal.at("Translate"));
            d["rotate_y"] = goal.at("Rotate").at("Y");
            d["star_root"] = root;
            Value body = Value::object();
            body["offset"] = 80.0;
            body["radius"] = 100.0;
            d["star_body"] = body;
            Value head = Value::object();
            head["offset"] = 125.0;
            head["radius"] = 55.0;
            d["player_head"] = head;
            Value cam = Value::object();
            cam["eye_up"] = 100.0;
            cam["eye_front"] = 550.0;
            cam["target_up"] = 130.0;
            cam["fovy"] = 35.0;
            d["camera"] = cam;
            Value steps = Value::object();
            steps["course_clear"] = 96;
            steps["end"] = 160;
            d["demo_steps"] = steps;
            d["pair"] = pair;
            const Value* after = goal.find("IsClearAfterDemo");
            d["clear_after_demo"] = after && after->type() == Value::Type::Bool && after->boolean();
            Value clips = Value::object();
            clips["sign"] = pair ? "GoalPosePairSign" : "GoalPoseSign";
            clips["pair"] = pair ? "GoalPosePair" : "GoalPose";
            clips["star_sign"] = "GoalPoseSign";
            clips["mini"] = "MiniGoalPose";   // GoalPose step 0 (0x1df400) for a Mini player (0x2d6dac)
            d["clips"] = clips;
            d["jingle"] = "KinotanClear";   // BGM keeper name (0x1df400; KinotanClear2 on a season's last course)
            d["status"] = "Goal demo of the course (Goal.hpp nerve machines; parameters from 0x1ec824 / 0x1df400).";
            scene_["goal"] = d;
            if (ModelEntry* e = modelFor("GoalItem")) {
                Value spawn = item;
                spawn["Id"] = item.at("Id").str() + "/demo";
                spawn["UnitConfigName"] = "GoalDemoStar";
                spawn["UnitConfig"]["ParameterConfigName"] = "GoalDemoStar";
                spawn["Translate"] = goal.at("Translate");
                spawn["Rotate"] = goal.at("Rotate");
                spawn["SpawnedBy"] = item.at("Id");
                placeActor(*e, spawn, spawn.at("Id").str());
                addObject(spawn, "GoalDemoStar", "GoalItem", placementMatrix(spawn));
            }
        }
    }
    // The stage-start bind demo (DemoObjList entry, actor 0x206204; game/StageStart.hpp): its
    // kind, front and the player's start action (StartAnimation), and the player's placement front.
    if (const Value* demos = map_.find("DemoObjList"); demos && demos->isArray() && demos->size() > 0) {
        const Value& demo = (*demos)[0];
        if (demo.find("StartAnimation")) {
            Value ss = Value::object();
            ss["status"] = "Recovered stage-start demo (actor 0x206204, as for the opening).";
            ss["demo_kind"] = demo.at("UnitConfigName");
            ss["demo_yaw"] = demo.at("Rotate").at("Y");
            ss["start_animation"] = demo.at("StartAnimation");
            ss["player_yaw"] = map_.at("PlayerList")[0].at("Rotate").at("Y");
            // 0x147b3c: a ZoomIn demo starts the music after 109 steps instead of 100 on these
            // stages (by name).
            bool ghost = false;
            for (const char* g :
                 {"GhostPlayerMaze1Stage", "GhostPlayerMaze2Stage", "GhostPlayerMaze3Stage",
                  "GhostPlayerPatanPanelStage", "GhostPlayerChikuwaFallStage", "GhostPlayerBossFireStage"}) {
                ghost |= assets_.stage == g;
            }
            ss["ghost_stage"] = ghost;
            scene_["stage_start"] = ss;
        }
    }
    scene_["objects"] = objects_;
    // Areas the course's objects and the player react to (AreaCubeBase boxes: the placement's
    // matrix maps the unit box x, z in [-500, 500], y in [0, 1000]; their stage-switch links):
    // SwitchOnArea / SwitchKeepOnArea (the scene's switch-area director, 0x256fdc), WaterArea,
    // PlayerFallActionControlArea, LightArea (the light areas, LightService).
    Value stageAreas = Value::array();
    if (const Value* list = map_.find("AreaList"); list && list->isArray()) {
        for (const auto& a : list->items()) {
            const std::string kind = a.at("UnitConfigName").str();
            if (kind != "SwitchOnArea" && kind != "SwitchKeepOnArea" && kind != "WaterArea" &&
                kind != "PlayerFallActionControlArea" && kind != "LightArea") {
                continue;
            }
            Value e = Value::object();
            e["id"] = a.at("Id");
            e["kind"] = kind;
            e["matrix"] = matrixJson(placementMatrix(a));
            Value params = Value::object();
            for (const auto& [k, v] : a.members()) {
                if (v.isNumber() || v.type() == Value::Type::Bool) {
                    params[k] = v;
                }
            }
            e["params"] = params;
            Value links = Value::object();
            if (const Value* l = a.find("Links"); l && l->isObject()) {
                for (const auto& [k, v] : l->members()) {
                    Value ids = Value::array();
                    if (v.isArray()) {
                        for (const auto& t : v.items()) {
                            if (t.isObject() && t.has("Id")) {
                                ids.push(t.at("Id"));
                            }
                        }
                    }
                    links[k] = ids;
                }
            }
            e["links"] = links;
            stageAreas.push(e);
        }
    }
    scene_["stage_areas"] = stageAreas;
    Value placed = Value::object();
    for (const auto& [k, v] : counts) {
        placed[k] = v;
    }
    report_["placed"] = placed;
}

// ---- presentation ---------------------------------------------------------------------------

void StageBuilder::presentation() {
    const std::string archive = "ObjectData/" + assets_.base + "Scene.szs";
    const Bch lights = in_.bch(archive, assets_.base + "Scene.bch");
    Value records = Value::array();
    Value sceneLights = Value::object();
    for (uint32_t p : lights.table(5)) {
        const uint32_t kind = lights.u32(p + 40) & 15;
        sceneLights[lights.nameOf(p)] = lightRecord(lights, p);
        if (kind != 2 && kind != 9) {
            note("lights", "unsupported light kind " + std::to_string(kind) + " " + lights.nameOf(p));
            continue;
        }
        Value rec = Value::object();
        rec["name"] = lights.nameOf(p);
        rec["kind"] = kind;
        if (kind == 2) {
            rec["ambient"] = doubles({lights.u8(p + 48) / 255.0, lights.u8(p + 49) / 255.0, lights.u8(p + 50) / 255.0});
        } else {
            rec["diffuse"] = doubles({lights.u8(p + 52) / 255.0, lights.u8(p + 53) / 255.0, lights.u8(p + 54) / 255.0});
            rec["direction"] = doubles({lights.f32(p + 64), lights.f32(p + 68), lights.f32(p + 72)});
        }
        records.push(rec);
    }
    Value sets = Value::object();
    for (uint32_t q : lights.table(14)) {
        const uint32_t entries = lights.u32(q + 12), count = lights.u32(q + 16);
        for (uint32_t j = 0; j < count; ++j) {
            const size_t e = entries + j * 12;
            const uint32_t index = lights.u32(e), names = lights.u32(e + 4), n = lights.u32(e + 8);
            Value list = Value::array();
            for (uint32_t k = 0; k < n; ++k) {
                list.push(lights.nameOf(names + k * 4));
            }
            sets[std::to_string(index)] = list;
        }
    }
    Value lighting = Value::object();
    lighting["lights"] = records;
    lighting["scene_lights"] = sceneLights;
    lighting["scene_sets"] = sets;
    lighting["active"] = sets.has("1") ? sets.at("1") : Value::array();
    if (design_.has("LightAreas")) {
        lighting["light_areas"] = design_.at("LightAreas");
    }
    lighting["source"] = "rom:" + archive;
    lighting["generic"] = true;
    scene_["lighting"] = lighting;
}

void StageBuilder::sky() {
    const Value* list = map_.find("SkyList");
    if (!list || !list->isArray() || list->size() == 0) {
        return;
    }
    const Value& placement = (*list)[0];
    const std::string name =
        placement.at("ModelName").isString() ? placement.at("ModelName").str() : placement.at("UnitConfigName").str();
    const std::string archive = "ObjectData/" + name + ".szs";
    if (!Inputs::has(archive) || !hasMember(archive, name + ".bch")) {
        note("without_model", "sky " + name);
        return;
    }
    const Bch b = in_.bch(archive, name + ".bch");
    std::map<std::string, long> texids;
    for (const auto& t : b.textureRecords()) {
        texids[t.name] = addTextureFrom(b, t, archive);
    }
    const Model model = b.models().at(0);
    const Value translation = xyz(placement.at("Translate"));
    const double tr[3] = {translation[0].num(), translation[1].num(), translation[2].num()};
    const size_t materialBase = scene_.at("materials").size();
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const Material& m = model.materials[i];
        TevOptions o;
        o.colors = &m.colors;
        o.sky = true;
        o.transforms = &m.transforms;
        try {
            shaders_[materialBase + i] = generateTev(m.fragment, o);
        } catch (const std::exception& err) {
            shaders_[materialBase + i] = plainShader(m);
            note("approximated_materials", name + ":" + m.name + " (" + err.what() + ")");
        }
        scene_["materials"].push(toJson(m));
    }
    // Its texture animation (e.g. SkyPullUp.cmtsa, started by the generic model init 0x2fd7fc
    // under the model's name, as for the placed objects; observed in run loadB). The sky's texture
    // coordinates are exported untransformed already.
    ModelEntry entry;
    entry.archive = archive;
    entry.bch = std::make_shared<Bch>(in_.file(archive, name + ".bch"));
    entry.model = model;
    entry.materialBase = materialBase;
    materialAnimations(entry);
    Value skyMeshes = Value::array();
    size_t skyVertices = 0;
    for (const Mesh& mesh : model.meshes) {
        if (!mesh.visible || (mesh.flags & 1)) {
            continue;
        }
        const Material& m = model.materials.at(mesh.material);
        const size_t start = vertices_.size() / 48;
        const Vec4d& sc = mesh.uniform(7);
        const Vec4d& po = mesh.uniform(6);
        const double u8s = mesh.uniform(8)[0];
        for (const Face& face : mesh.faces) {
            for (uint32_t index : face.indices) {
                const Vertex& v = mesh.vertices.at(index);
                const double uv0 = v.has(4) ? v.v[4][0] * u8s : 0, uv1 = v.has(4) ? v.v[4][1] * u8s : 0;
                double c[4] = {1, 1, 1, 1};
                for (int k = 0; k < 4 && v.has(3); ++k) {
                    c[k] = v.v[3][size_t(k)] * sc[3];
                }
                pack(vertices_, {v.v[0][0] * sc[0] + po[0] + tr[0], v.v[0][1] * sc[0] + po[1] + tr[1],
                                 v.v[0][2] * sc[0] + po[2] + tr[2], 0, 1, 0, uv0, uv1, c[0], c[1], c[2], c[3]});
                pack(tangents_, {1, 0, 0});
            }
        }
        auto id = [&](const std::optional<std::string>& n) {
            if (!n) {
                return -1L;
            }
            auto it = texids.find(*n);
            return it == texids.end() ? -1L : it->second;
        };
        Value x = Value::object();
        x["name"] = m.name;
        x["first"] = start;
        x["count"] = vertices_.size() / 48 - start;
        skyVertices += vertices_.size() / 48 - start;
        x["texture"] = id(m.textures[1]);
        x["texture0"] = id(m.textures[0]);
        x["detail_texture"] = -1;
        x["wrap_s"] = 2;
        x["wrap_t"] = 2;
        x["material_index"] = materialBase + mesh.material;
        x["sky"] = true;
        x["samplers"] = samplersJson(m.samplers);
        skyMeshes.push(x);
    }
    for (const auto& m : scene_.at("meshes").items()) {
        skyMeshes.push(m);
    }
    scene_["meshes"] = skyMeshes;
    Value info = Value::object();
    info["model"] = model.name;
    info["translation"] = translation;
    info["triangles"] = skyVertices / 3;
    if (entry.materialClips.size()) {
        Value anims = Value::object();
        anims["clips"] = entry.materialClips;
        anims["rest"] = entry.materialRest;
        anims["play"] = Value::array();
        for (const char* ext : {".cmcla", ".cmtsa"}) {
            if (entry.materialClips.has(model.name + ext)) {
                anims["play"].push(model.name + ext);
            }
        }
        info["material_animations"] = anims;
    }
    info["status"] = "ROM sky mesh; its material animations named after the model play from the start.";
    scene_["sky"] = info;
}

// Lighting tables (LutCurveData plus the headlamp's own rows) and the headlamp light, as for the
// opening (SceneExport lighting()).
void StageBuilder::lighting() {
    struct Row {
        std::string table, name;
        size_t row;
    };
    std::vector<Row> rows;
    for (size_t i = 0; i < luts().size(); ++i) {
        rows.push_back({"lightSet", luts()[i].name, i});
    }
    const Bch hb = in_.bch(kHeadlight, "PlayerHeadlight.bch");
    struct Extra {
        std::string name, table;
        std::vector<uint32_t> words;
        uint32_t absolute;
    };
    std::vector<Extra> extra;
    for (uint32_t p : hb.table(4)) {
        const std::string table = hb.nameOf(p + 8);
        for (uint32_t i = 0; i < hb.u32(p + 4); ++i) {
            const size_t q = p + 12 + i * 16;
            const uint32_t cp = hb.u32(q + 4);
            Extra e{hb.nameOf(q + 12), table, {hb.u32(cp)}, hb.u32(q)};
            for (int k = 2; k < 257; ++k) {
                e.words.push_back(hb.u32(cp + size_t(k) * 4));
            }
            rows.push_back({table, e.name, luts().size() + extra.size()});
            extra.push_back(std::move(e));
        }
    }
    auto atlasRow = [&](const std::vector<uint32_t>& words) {
        for (uint32_t w : words) {
            pack(lutAtlas_, {(w & 4095) / 4095.0, ((w >> 12) & 2047) / 2047.0 * ((w & (1u << 23)) ? -1 : 1), 0, 1});
        }
    };
    for (const auto& l : luts()) {
        atlasRow(l.words);
    }
    for (const auto& e : extra) {
        atlasRow(e.words);
    }
    Value& lighting = scene_["lighting"];
    lighting["lut_rows"] = luts().size() + extra.size();
    Value lutList = Value::array();
    for (const auto& l : luts()) {
        lutList.push(toJson(l));
    }
    lighting["luts"] = lutList;
    auto rowOf = [&](const Value& table, const Value& name) -> Value {
        if (!name.truthy()) {
            return Value();
        }
        for (const auto& r : rows) {
            if (table.isString() && r.table == table.str() && r.name == name.str()) {
                return r.row;
            }
        }
        throw ImportError("Unknown light LUT");
    };
    Value rec;
    for (uint32_t q : hb.table(5)) {
        if (hb.name(q) == "spotLight") {
            rec = lightRecord(hb, q);
        }
    }
    const Value& s = rec.at("samplers");
    rec["distance_row"] = rowOf(s[0], s[1]);
    rec["spot_row"] = rowOf(s[2], s[3]);
    bool spotAbsolute = false;
    for (const auto& e : extra) {
        if (s[3].truthy() && e.name == s[3].str()) {
            spotAbsolute = e.absolute != 0;
        }
    }
    rec["spot_absolute"] = spotAbsolute;
    Value attach = Value::object();
    attach["kind"] = "joint";
    attach["joint"] = "HeadLight";
    rec["attach"] = attach;
    rec["source"] = std::string("rom:") + kHeadlight + "/PlayerHeadlight.bch";
    // The light's check points in the joint's frame (the protagonist's InitLightActor entry of
    // "spotLight", 0x222fa0: missing coordinates are 0): the light areas holding them decide the
    // scene light sets it joins (LightService).
    {
        Value points = Value::array();
        const Value init = in_.byml(kProtagonistArchive, "InitLightActor.byml");
        for (const auto& l : init.at("Lights").items()) {
            if (!l.has("lightName") || l.at("lightName").str() != "spotLight" || !l.has("checkPoints")) {
                continue;
            }
            for (const auto& c : l.at("checkPoints").items()) {
                Value pt = Value::array();
                for (const char* k : {"x", "y", "z"}) {
                    pt.push(c.has(k) ? c.at(k).num() : 0.0);
                }
                points.push(pt);
            }
        }
        rec["check_points"] = points;
    }
    Value locals = Value::object();
    locals["headlamp"] = rec;
    lighting["local_lights"] = locals;
    Value lists = Value::object();
    lists["terrain"] = strings({"mainLight", "secondLight", "headlamp"});
    lists["character"] = strings({"mainLight", "headlamp"});
    lighting["light_lists"] = lists;
}

// ---- the protagonist ------------------------------------------------------------------------

// The protagonist's model of one power form (UserModelList: Kinopio for Super, KinopioMini for
// Mini; the player's model container 0x2dac74 holds one per form, switched by 0x1bd1c4): its
// materials, skinned meshes (flagged with the form) and shadow proxy, and the skeleton document.
void StageBuilder::character(int form) {
    const char* archive = form == 0 ? kProtagonistArchive : kProtagonistMiniArchive;
    const std::string modelName = form == 0 ? "Kinopio" : "KinopioMini";
    Bytes& skin = form == 0 ? characterSkin_ : characterMiniSkin_;
    Value& document = form == 0 ? character_ : characterMini_;
    const Bch b = in_.bch(archive, modelName + ".bch");
    const auto protagonistModels = b.models();
    const Model& model = protagonistModels.at(0);
    const Bch ab = in_.bch(kProtagonistAnimArchive, "KinopioAnimation.bch");
    const Value names = nodeNames(b);
    std::map<std::string, long> texids;
    for (const auto& t : b.textureRecords()) {
        texids[t.name] = addTextureFrom(b, t, archive);
    }
    // The reflection texture: SphereMap of the stage's cube map archive (0x253a50).
    const std::string cube = cubeMapArchive();
    const std::string cubeMember = archiveMember(cube);
    if (Inputs::has(cube) && hasMember(cube, cubeMember)) {
        const Bch eb = in_.bch(cube, cubeMember);
        for (const auto& t : eb.textureRecords()) {
            if (t.name == "SphereMap") {
                texids[t.name] = addTextureFrom(eb, t, cube);
            }
        }
    }
    const size_t materialBase = scene_.at("materials").size();
    for (const auto& m : model.materials) {
        const PicaState st = picaState(b, m, luts());
        TevOptions o;
        o.colors = &m.colors;
        o.texture0 = true;
        std::string source = generateTev(m.fragment, o);
        if (((m.transforms[1].flags >> 16) & 255) == 2) {
            source = replaceAll(source, "vec4 tex1=texture2D(albedo,gl_TexCoord[0].xy);",
                                "vec3 reflected=viewToWorld*reflect(normalize(eyePosition),normalize(normal));vec4 "
                                "tex1=texture2D(albedo,reflected.xy*0.5+0.5);");
            source = replaceFirst(source, "void main()", "uniform mat3 viewToWorld;\nvoid main()");
        }
        shaders_[scene_.at("materials").size()] = picaLightingFragment(source, m, st);
        Value entry = toJson(m);
        Value pica = Value::object();
        pica["kind"] = "character";
        pica["config0"] = st.config0;
        pica["config1"] = st.config1;
        pica["lut_abs"] = st.lutAbs;
        pica["lut_input"] = st.lutInput;
        pica["lut_scale"] = st.lutScale;
        pica["lut_names"] = st.lutNamesJson();
        pica["geometric0"] = st.geometric0;
        pica["geometric1"] = st.geometric1;
        pica["shadow_mode"] = st.shadowMode ? Value(*st.shadowMode) : Value();
        pica["colors"] = st.colorsJson();
        pica["flags"] = st.flags;
        pica["light_set"] = st.lightSet;
        pica["locals"] = strings({"headlamp"});
        entry["pica"] = pica;
        scene_["materials"].push(entry);
    }
    const size_t first = vertices_.size() / 48;
    Value characterMeshes = Value::array();
    for (const Mesh& mesh : model.meshes) {
        const Material& mat = model.materials.at(mesh.material);
        const size_t start = vertices_.size() / 48;
        const Vec4d &sc = mesh.uniform(7), &u = mesh.uniform(8), &po = mesh.uniform(6);
        double uvs[2], uvo[2];
        scaleOffset(mat.transforms[1], uvs, uvo);
        for (const Face& f : mesh.faces) {
            for (uint32_t idx : f.indices) {
                const Vertex& v = mesh.vertices.at(idx);
                double pos[3], normal[3], t[3], uv[2], c[4];
                for (int k = 0; k < 3; ++k) {
                    pos[k] = v.v[0][size_t(k)] * sc[0] + po[size_t(k)];
                    normal[k] = v.v[1][size_t(k)] * sc[1];
                    t[k] = (v.has(2) ? v.v[2][size_t(k)] * sc[2] : (k == 0 ? 1.0 : 0.0));
                }
                for (int k = 0; k < 2; ++k) {
                    uv[k] = (v.has(4) ? v.v[4][size_t(k)] : 0.0) * u[0] * uvs[k] + uvo[k];
                }
                for (int k = 0; k < 4; ++k) {
                    c[k] = v.has(3) ? v.v[3][size_t(k)] * sc[3] : 1.0;
                }
                packSkin(skin, pos, normal, t, skinOf(v, f, u[3], 1e-5, false), f.skin);
                pack(vertices_,
                     {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], uv[0], uv[1], c[0], c[1], c[2], c[3]});
                pack(tangents_, {t[0], t[1], t[2]});
            }
        }
        auto id = [&](const std::optional<std::string>& n) {
            return n && texids.count(*n) ? texids.at(*n) : -1L;
        };
        const uint32_t reg = mat.registers.count(0x93) ? mat.registers.at(0x93) : 0;
        Value m = Value::object();
        m["name"] = mat.name;
        m["first"] = start;
        m["count"] = vertices_.size() / 48 - start;
        m["texture"] = id(mat.textures[1]);
        m["texture0"] = id(mat.textures[0]);
        m["detail_texture"] = id(mat.textures[2]);
        m["wrap_s"] = (reg >> 12) & 15;
        m["wrap_t"] = (reg >> 8) & 15;
        m["material_index"] = materialBase + mesh.material;
        m["samplers"] = samplersJson(mat.samplers);
        m["character"] = true;
        m["character_form"] = form;
        scene_["meshes"].push(m);
        Value entry = Value::object();
        entry["mesh_index"] = scene_.at("meshes").size() - 1;
        entry["node"] = names[mesh.node];
        characterMeshes.push(entry);
    }
    // The skinned shadow proxy (Kinopio_shd) as a shadow-only mesh, as in the opening.
    const Model& shd = modelNamed(protagonistModels, modelName + "_shd");
    const size_t start = vertices_.size() / 48;
    for (const Mesh& mesh : shd.meshes) {
        const Vec4d &sc = mesh.uniform(7), &u = mesh.uniform(8), &po = mesh.uniform(6);
        for (const Face& f : mesh.faces) {
            for (uint32_t idx : f.indices) {
                const Vertex& v = mesh.vertices.at(idx);
                const double pos[3] = {v.v[0][0] * sc[0] + po[0], v.v[0][1] * sc[0] + po[1], v.v[0][2] * sc[0] + po[2]};
                const double normal[3] = {0, 1, 0}, tangent[3] = {1, 0, 0};
                packSkin(skin, pos, normal, tangent, skinOf(v, f, u[3], 1e-5, false), f.skin);
                pack(vertices_, {pos[0], pos[1], pos[2], 0, 1, 0, 0, 0, 1, 1, 1, 1});
                pack(tangents_, {1, 0, 0});
            }
        }
    }
    Value proxy = Value::object();
    proxy["name"] = modelName + "_shd";
    proxy["first"] = start;
    proxy["count"] = vertices_.size() / 48 - start;
    proxy["texture"] = -1;
    proxy["texture0"] = -1;
    proxy["detail_texture"] = -1;
    proxy["wrap_s"] = 2;
    proxy["wrap_t"] = 2;
    proxy["material_index"] = materialBase;
    proxy["character"] = true;
    proxy["shadow_only"] = true;
    proxy["character_form"] = form;
    scene_["meshes"].push(proxy);
    const Value& placement = map_.at("PlayerList")[0];
    // The Mini form's retargeting (PlayerRetargettingInfo, built by 0x214934): the shared clips
    // are authored for the Kinopio skeleton; each bone's source length is the "Kinopio" entry.
    // The form's own clips (resolver kind 2, e.g. MiniFall) use the form's entry
    // ("KinopioMini") at scale 1 (run hold: the player's selector tables, 0x43a3b4).
    Value retarget, retargetForm;
    const std::string retargetArc = "ObjectData/PlayerRetargettingInfo.szs";
    if (Inputs::has(retargetArc) && hasMember(retargetArc, "PlayerRetargettingInfo.byml")) {
        const Value info = in_.byml(retargetArc, "PlayerRetargettingInfo.byml");
        if (info.has("Kinopio")) {
            retarget = info.at("Kinopio");
        }
        if (form == 1 && info.has(model.name)) {
            retargetForm = info.at(model.name);
        }
    }
    Value bones = Value::array();
    for (const auto& bone : model.bones) {
        Value j = toJson(bone);
        // H3D bone flag bit 22, IsSegmentScaleCompensate: the bone does not inherit its parent's
        // scale (e.g. the hands while the arms stretch in the pickaxe swing).
        j["segment_scale_compensate"] = (bone.flags & 0x400000u) != 0;
        if (retarget.type() == Value::Type::Object && retarget.has(bone.name)) {
            j["retarget_source_length"] = retarget.at(bone.name).num();
        }
        if (retargetForm.type() == Value::Type::Object && retargetForm.has(bone.name)) {
            j["retarget_form_source_length"] = retargetForm.at(bone.name).num();
        }
        bones.push(j);
    }
    document = Value::object();
    document["model"] = model.name;
    document["bones"] = bones;
    document["animation"] = characterAnimation(ab, "Wait");
    document["visibility"] = visibility(ab, {"WaitEye", "WaitFace", "WaitHandL", "WaitHandR"});
    document["meshes"] = characterMeshes;
    document["first_vertex"] = first;
    document["vertex_count"] = skin.size() / 72;
    document["translation"] = xyz(placement.at("Translate"));
    document["rotation"] = xyz(placement.at("Rotate"));
    document["frames_per_second"] = 60;
    // The wet look (WetStart / WetEnd, KinopioAnimation.bch table 9, .cmcla): lighting-colour
    // curves of the protagonist's materials, started by the wet state (game/PlayerWater.hpp).
    {
        std::map<std::string, Value> wet;
        for (uint32_t p : ab.table(9)) {
            const std::string file = ab.nameOf(p);
            if (file == "WetStart.cmcla" || file == "WetEnd.cmcla") {
                wet[file] = materialAnimation(ab, file);
            }
        }
        // Animated combiner constants: the material's shader reads them from a uniform (as the
        // stage objects' materialAnimations); the rest values are the material's own.
        std::map<size_t, std::map<int, std::vector<int>>> constantStagesOf;
        Value rest = Value::array();
        for (size_t i = 0; i < model.materials.size(); ++i) {
            const Material& m = model.materials[i];
            std::set<int> animated;
            for (const auto& [file, a] : wet) {
                for (const auto& el : a.at("elements").items()) {
                    const std::string t = el.at("target").str();
                    if (el.at("material").str() == m.name && startsWith(t, "constant")) {
                        animated.insert(t[8] - '0');
                    }
                }
            }
            Value entry = Value::object();
            entry["material_index"] = materialBase + i;
            entry["slots"] = Value::object();
            entry["constants"] = Value::array();
            for (int c : animated) {
                const std::vector<int> stages = constantStages(m, c);
                if (stages.empty()) {
                    note("unapplied_material_animations", std::string(kProtagonistAnimArchive) + ":" + m.name +
                                                              " constant" + std::to_string(c) +
                                                              " (no combiner stage holds it)");
                    continue;
                }
                const std::string uniform = "animatedConstant" + std::to_string(c);
                std::string& source = shaders_[materialBase + i];
                source = animatedConstantShader(source, stages, uniform);
                constantStagesOf[i][c] = stages;
                Value base = Value::array(), axes = Value::array();
                for (int k = 0; k < 4; ++k) {
                    base.push(m.colors[size_t(20 + 4 * c + k)] / 255.0);
                    axes.push(nullptr);
                }
                Value e = Value::object();
                e["base"] = base;
                e["axes"] = axes;
                e["frames"] = 1.0;
                e["uniform"] = uniform;
                entry["constants"].push(e);
            }
            if (entry.at("constants").size()) {
                rest.push(entry);
            }
        }
        document["material_rest"] = rest;
        Value clips = Value::object();
        for (const auto& [file, a] : wet) {
            Value mats = Value::array();
            for (size_t i = 0; i < model.materials.size(); ++i) {
                const Material& m = model.materials[i];
                Value lights = Value::array();
                Value constants = Value::array();
                for (const auto& el : a.at("elements").items()) {
                    if (el.at("material").str() != m.name) {
                        continue;
                    }
                    const std::string t = el.at("target").str();
                    if (startsWith(t, "constant")) {
                        const int c = t[8] - '0';
                        if (!constantStagesOf[i].count(c)) {
                            continue;
                        }
                        Value base = Value::array();
                        for (int k = 0; k < 4; ++k) {
                            base.push(m.colors[size_t(20 + 4 * c + k)] / 255.0);
                        }
                        Value e = Value::object();
                        e["base"] = base;
                        e["axes"] = el.at("axes");
                        e["frames"] = a.at("frames");
                        e["uniform"] = "animatedConstant" + std::to_string(c);
                        constants.push(e);
                        continue;
                    }
                    const int lt = lightTarget(t);
                    if (lt < 0) {
                        note("unapplied_material_targets",
                             std::string(kProtagonistAnimArchive) + ":" + file + ":" + m.name + ":" + t);
                        continue;
                    }
                    Value base = Value::array();
                    for (int k = 0; k < 4; ++k) {
                        base.push(m.colors[size_t(4 * lt + k)] / 255.0);
                    }
                    Value light = Value::object();
                    light["target"] = lt;
                    light["base"] = base;
                    light["axes"] = el.at("axes");
                    light["frames"] = a.at("frames");
                    lights.push(light);
                }
                if (lights.size() || constants.size()) {
                    Value entry = Value::object();
                    entry["material_index"] = materialBase + i;
                    entry["slots"] = Value::object();
                    entry["constants"] = constants;
                    entry["lights"] = lights;
                    entry["patterns"] = Value::array();
                    mats.push(entry);
                }
            }
            Value clip = Value::object();
            clip["frames"] = a.at("frames");
            clip["materials"] = mats;
            clips[file.substr(0, file.size() - 6)] = clip;
        }
        document["material_animations"] = clips;
    }
    if (form == 0) {
        Value summary = Value::object();
        summary["model"] = model.name;
        summary["bones"] = model.bones.size();
        scene_["character"] = summary;
    } else {
        scene_["character_mini"] = true;
    }
}

void StageBuilder::player() {
    const Bch ab = in_.bch(kProtagonistAnimArchive, "KinopioAnimation.bch");
    // The recovered player's clips, and the stage-start demo's start action (DemoObjList
    // StartAnimation, e.g. "CourseInRove").
    std::vector<std::string> clipList(std::begin(kPlayerClips), std::end(kPlayerClips));
    if (const Value* demos = map_.find("DemoObjList"); demos && demos->isArray() && demos->size() > 0) {
        if (const Value* a = (*demos)[0].find("StartAnimation")) {
            if (std::find(clipList.begin(), clipList.end(), a->str()) == clipList.end()) {
                clipList.push_back(a->str());
            }
        }
    }
    std::set<std::string> present;
    for (uint32_t p : ab.table(8)) {
        present.insert(ab.nameOf(p));
    }
    Value clips = Value::object();
    for (const std::string& name : clipList) {
        const std::string n = name;
        if (!present.count(n)) {   // optional variants (e.g. a Mini goal pose the archive lacks)
            continue;
        }
        Value c = Value::object();
        c["animation"] = characterAnimation(ab, n);
        c["visibility"] = visibility(ab, {n + "Eye", n + "Face", n + "HandL", n + "HandR"});
        clips[name] = c;
    }
    character_["clips_curves"] = clips;
    Value actions = Value::object();
    std::map<std::string, std::pair<double, bool>> clipInfo;
    for (uint32_t p : ab.table(8)) {
        const std::string name = ab.nameOf(p);
        clipInfo[name] = {ab.f32(p + 8), bool(ab.u32(p + 4) & 1)};
        Value a = Value::object();
        a["clip"] = name;
        a["frames"] = ab.f32(p + 8);
        a["loop"] = bool(ab.u32(p + 4) & 1);
        a["clips"] = strings({});
        a["clips"].push(name);
        actions[name] = a;
    }
    const Value ctrl = in_.byml(kProtagonistAnimArchive, "ActionAnimCtrl.byml");
    for (const auto& e : ctrl.items()) {
        if (e.isNull()) {
            continue;
        }
        const Value& skl = e.at("SklAnim");
        Value subs = Value::array();
        if (skl.isArray()) {
            for (const auto& s : skl.items()) {
                subs.push(s.at("Name"));
            }
        } else {
            subs.push(skl.at("Name"));
        }
        auto info = clipInfo.find(subs[0].str());
        if (info == clipInfo.end()) {
            continue;
        }
        Value a = Value::object();
        a["clip"] = subs[0];
        a["frames"] = info->second.first;
        a["loop"] = info->second.second;
        a["clips"] = subs;
        actions[e.at("ActionName").str()] = a;
    }
    Value areas = Value::object();
    areas["death"] = Value::array();
    areas["player_anim"] = Value::array();
    areas["water"] = Value::array();
    if (const Value* list = map_.find("AreaList"); list && list->isArray()) {
        for (const auto& a : list->items()) {
            const std::string kind = a.at("UnitConfigName").str();
            if (kind != "DeathArea" && kind != "PlayerAnimArea" && kind != "WaterArea") {
                continue;
            }
            bool rotated = false;
            for (const char* k : {"X", "Y", "Z"}) {
                rotated = rotated || std::abs(a.at("Rotate").at(k).num()) > 1e-6;
            }
            if (rotated) {
                note("unported_areas", kind + " " + a.at("Id").str() + " (rotated)");
                continue;
            }
            Value box = Value::object();
            box["translate"] = xyz(a.at("Translate"));
            box["scale"] = xyz(a.at("Scale"));
            if (kind == "PlayerAnimArea") {
                if (!a.has("PlayerAnimName") || !a.at("PlayerAnimName").isString() ||
                    !clipInfo.count(a.at("PlayerAnimName").str())) {
                    continue;
                }
                box["anim"] = a.at("PlayerAnimName");
                box["start_frame"] = a.at("AnimStartFrame");
                box["interval"] = a.at("IntervalFrame");
                areas["player_anim"].push(box);
            } else if (kind == "WaterArea") {
                areas["water"].push(box);
            } else {
                areas["death"].push(box);
            }
        }
    }
    const Value& start = map_.at("PlayerList")[0];
    player_ = Value::object();
    player_["status"] = "Recovered player movement on a generic stage: collision of every placed object with a KCL.";
    Value s = Value::object();
    s["translate"] = xyz(start.at("Translate"));
    s["rotate"] = xyz(start.at("Rotate"));
    player_["start"] = s;
    Value col = Value::object();
    col["parts"] = collisionParts_;
    player_["collision"] = col;
    player_["areas"] = areas;
    player_["actions"] = actions;
    player_["anim_interp"] = in_.byml(kProtagonistAnimArchive, "AnimInterp.byml");
    Value clipNames = Value::array();
    for (const std::string& c : clipList) {
        clipNames.push(c);
    }
    player_["clips"] = clipNames;
    Value ref = Value::object();
    ref["file"] = "player.json";
    ref["clips"] = clipNames.size();
    scene_["player"] = ref;
}

// ---- shadows and camera ---------------------------------------------------------------------

void StageBuilder::shadows() {
    Value shadow = Value::object();
    double direction[3] = {0, -1, 0};
    Value tint = doubles({0, 0, 0, 0.5});
    if (design_.has("DepthShadow")) {
        const Value& raw = design_.at("DepthShadow");
        const Value& params = raw.at("DepthShadowParam").at("DefaultDepthShadowParam");
        const Value& light = raw.at("DirLitParam").at("DefaultDirLitParam");
        const double x = light.at("DirectionParamX").num(), y = light.at("DirectionParamY").num();
        direction[0] = -std::sin(x) * std::cos(y);
        direction[1] = -std::sin(y);
        direction[2] = -std::cos(x) * std::cos(y);
        tint = Value::array();
        for (const char* k : {"ColorR", "ColorG", "ColorB", "ColorA"}) {
            tint.push(params.at(k).num() / 255);
        }
        shadow["parameters"] = params;
        shadow["original_resolution"] = raw.at("GlobalParam").at("ShadowMapWidth");
        // Original clip volume: ShadowBounds box; bias = BiasFactor x projected depth span.
        if (const Value* list = map_.find("AreaList"); list && list->isArray()) {
            for (const auto& area : list->items()) {
                if (!unitIs(area, "ShadowBounds")) {
                    continue;
                }
                double t[3], sc[3], lo[3], hi[3], toward[3];
                const char* axes[3] = {"X", "Y", "Z"};
                for (int k = 0; k < 3; ++k) {
                    t[k] = area.at("Translate").at(axes[k]).num();
                    sc[k] = area.at("Scale").at(axes[k]).num();
                    toward[k] = -direction[k];
                }
                lo[0] = t[0] - 500 * sc[0];
                lo[1] = t[1];
                lo[2] = t[2] - 500 * sc[2];
                hi[0] = t[0] + 500 * sc[0];
                hi[1] = t[1] + 1000 * sc[1];
                hi[2] = t[2] + 500 * sc[2];
                const double n = std::sqrt(dot3(toward, toward));
                for (double& v : toward) {
                    v /= n;
                }
                Value corners = Value::array();
                double minDepth = std::numeric_limits<double>::infinity(), maxDepth = -minDepth;
                for (int c = 0; c < 8; ++c) {
                    const double p[3] = {(c & 4) ? hi[0] : lo[0], (c & 2) ? hi[1] : lo[1], (c & 1) ? hi[2] : lo[2]};
                    corners.push(doubles({p[0], p[1], p[2]}));
                    minDepth = std::min(minDepth, dot3(p, toward));
                    maxDepth = std::max(maxDepth, dot3(p, toward));
                }
                Value bounds = Value::object();
                bounds["corners"] = corners;
                bounds["bias_world"] = params.at("BiasFactor").num() * (maxDepth - minDepth);
                shadow["bounds"] = bounds;
                break;
            }
        }
    } else {
        note("missing", "DepthShadow.byml");
    }
    shadow["direction"] = doubles({direction[0], direction[1], direction[2]});
    shadow["tint"] = tint;
    shadow["native_resolution"] = 2048;
    Value casters = Value::object();
    casters["mode"] = "native";
    shadow["casters"] = casters;
    scene_["shadow"] = shadow;
    // The terrain vertex shader with the native shadow and eye-space outputs (as the opening's).
    std::string v = shadowVertex(kTerrainVertexShader);
    v = replaceAll(v, "void main()", "varying vec3 eyePosition; varying vec3 surfaceTangent;\nvoid main()");
    v = replaceAll(v, "vertexColor=gl_Color;",
                   "eyePosition=(gl_ModelViewMatrix*gl_Vertex).xyz;surfaceTangent=gl_NormalMatrix*gl_MultiTexCoord1."
                   "xyz;vertexColor=gl_Color;");
    v = replaceAll(v, "gl_TexCoord[0]=gl_MultiTexCoord0;",
                   "gl_TexCoord[0]=gl_MultiTexCoord0;gl_TexCoord[2]=gl_MultiTexCoord2;");
    vertexShader_ = v;
}

// Camera tickets (CameraParam.byml): the Start ticket and the player camera of the camera area at
// the player's start (or the Default ticket). Only one area camera is carried for now; the
// switching between camera areas is part of the level work.
void StageBuilder::camera() {
    const Value cam = in_.byml(mapArchive(), "CameraParam.byml");
    const Value* startTicket = nullptr;
    if (const Value* l = cam.find("StartTickets"); l && l->isArray() && l->size() > 0) {
        startTicket = &(*l)[0].at("Param");
    }
    static const Value kEmpty = Value::object();
    const Value* defaults = cam.find("DefaultTickets");
    const Value& def = defaults && defaults->isArray() && defaults->size() > 0 ? (*defaults)[0].at("Param") : kEmpty;
    // The area ticket: the CameraArea containing the start, else the first area ticket.
    const Value* ticket = nullptr;
    const Value* area = nullptr;
    std::map<std::string, const Value*> byId;
    if (const Value* l = cam.find("Tickets"); l && l->isArray()) {
        for (const auto& t : l->items()) {
            const Value* id = t.find("Id");
            const Value* obj = id ? id->find("ObjId") : nullptr;
            if (obj && obj->isString() && t.at("Param").has("Distance")) {
                byId[obj->str()] = &t.at("Param");
            }
        }
    }
    if (const Value* list = map_.find("AreaList"); list && list->isArray()) {
        for (const auto& a : list->items()) {
            if (!unitIs(a, "CameraArea") || !byId.count(a.at("Id").str())) {
                continue;
            }
            if (!area || areaContains(a, spawn_)) {
                const bool better =
                    !area || !areaContains(*area, spawn_) || a.at("Priority").num() > area->at("Priority").num();
                if (better) {
                    area = &a;
                    ticket = byId.at(a.at("Id").str());
                }
            }
        }
    }
    if (!ticket) {
        ticket = &def;
        note("camera", "no area ticket at the start; using the Default ticket");
    }
    auto num = [](const Value& p, const char* k, double d) {
        const Value* v = p.find(k);
        return v && v->isNumber() ? v->num() : d;
    };
    // A two-value parameter ({"Min", "Max"} or {"Vertical", "Horizontal"}) with defaults.
    auto pair = [&](const Value& p, const char* k, const char* a, const char* b, double da, double db) {
        const Value* v = p.find(k);
        if (!v || !v->isObject()) {
            return doubles({da, db});
        }
        return doubles({num(*v, a, da), num(*v, b, db)});
    };
    auto vec = [](const Value& p, const char* k) {
        const Value* v = p.find(k);
        return v && v->isObject() ? xyz(*v) : doubles({0, 0, 0});
    };
    Value gc = Value::object();
    Value s = Value::object();
    if (startTicket) {
        // Without IsSetLookAt the Start camera (0x210544) orbits the protagonist's position plus
        // LookAtOffset (TouchPuzzleMiniStage: traced target (500, 207.4, 450) = start + (0, 158, 0)).
        const bool setLookAt = startTicket->find("IsSetLookAt") && startTicket->at("IsSetLookAt").truthy();
        s["angle_v"] = num(*startTicket, "AngleV", 30);
        s["angle_h"] = num(*startTicket, "AngleH", 0);
        s["distance"] = num(*startTicket, "Distance", 2000);
        s["set_look_at"] = setLookAt;
        s["look_at"] = vec(*startTicket, "LookAtPos");
        s["look_at_offset"] = vec(*startTicket, "LookAtOffset");
    } else {
        // No Start view: begin at the area camera's own start angles around its look-at point.
        const Value angles = pair(*ticket, "AngleStart", "Vertical", "Horizontal", 30, 0);
        s["angle_v"] = angles[0];
        s["angle_h"] = angles[1];
        s["distance"] = num(*ticket, "Distance", 3000);
        s["look_at"] = vec(*ticket, "LookAtPos");
        s["look_at_offset"] = doubles({0, 0, 0});
        note("camera", "no Start ticket");
    }
    // The Start ticket's field of view (FovyDegree; traced: 28 at the first frame of
    // RevolverRuinsStage); 35 without one (hypothesis: the camera's constructor value).
    s["fovy"] = startTicket ? num(*startTicket, "FovyDegree", 35) : 35.0;
    s["interpole_step"] = 60;
    gc["start"] = s;
    Value a = Value::object();
    a["id"] = area ? area->at("Id") : Value("Default");
    a["look_at"] = vec(*ticket, "LookAtPos");
    a["distance"] = num(*ticket, "Distance", 3000);
    a["distance_xyz"] =
        doubles({num(*ticket, "DistanceX", 5500), num(*ticket, "DistanceY", 5500), num(*ticket, "DistanceZ", 5500)});
    a["angle_v"] = pair(*ticket, "AngleV", "Min", "Max", 0, 89);
    a["angle_start"] = pair(*ticket, "AngleStart", "Vertical", "Horizontal", 30, 0);
    a["interpole_step"] = num(*ticket, "InterpoleStep", 60);
    a["reset_force"] = ticket->find("IsResetForce") ? ticket->at("IsResetForce").truthy() : false;
    a["zoom_fovy"] =
        doubles({num(*ticket, "ZoomFovy", 10), num(*ticket, "ZoomFovyMedium", 19), num(*ticket, "ZoomFovyFar", 30)});
    a["follow_offset"] = vec(*ticket, "FollowLookAtPlayerOffset");
    gc["area"] = a;
    // Every camera area with its player-camera ticket (area box, priority, all ticket fields): the
    // session uses the highest-priority area containing the player.
    auto flag = [](const Value& p, const char* k) {
        return p.find(k) && p.at(k).truthy();
    };
    auto bools = [](bool a, bool b, bool c) {
        Value v = Value::array();
        v.push(a);
        v.push(b);
        v.push(c);
        return v;
    };
    auto ticketJson = [&](const Value& t) {
        Value v = Value::object();
        v["look_at"] = vec(t, "LookAtPos");
        v["distance"] = num(t, "Distance", 3000);
        v["distance_xyz"] = doubles({num(t, "DistanceX", 5500), num(t, "DistanceY", 5500), num(t, "DistanceZ", 5500)});
        v["distance_detail"] = flag(t, "IsSetDistanceDetail");
        v["angle_v"] = pair(t, "AngleV", "Min", "Max", 0, 89);
        v["angle_h"] = pair(t, "AngleH", "Min", "Max", -180, 180);
        v["angle_start"] = pair(t, "AngleStart", "Vertical", "Horizontal", 30, 0);
        v["interpole_step"] = num(t, "InterpoleStep", 60);
        v["reset_force"] = flag(t, "IsResetForce");
        v["zoom_fovy"] = doubles({num(t, "ZoomFovy", 10), num(t, "ZoomFovyMedium", 19), num(t, "ZoomFovyFar", 30)});
        v["init_zoom_mode"] = num(t, "InitZoomMode", -1);
        v["follow"] =
            bools(flag(t, "IsFollowLookAtPlayerX"), flag(t, "IsFollowLookAtPlayerY"), flag(t, "IsFollowLookAtPlayerZ"));
        v["follow_offset"] = vec(t, "FollowLookAtPlayerOffset");
        v["upside_down"] = flag(t, "IsUpSideDown");
        v["use_min"] =
            bools(flag(t, "IsUseMoveLimitMinX"), flag(t, "IsUseMoveLimitMinY"), flag(t, "IsUseMoveLimitMinZ"));
        v["use_max"] =
            bools(flag(t, "IsUseMoveLimitMaxX"), flag(t, "IsUseMoveLimitMaxY"), flag(t, "IsUseMoveLimitMaxZ"));
        v["limit_min"] = vec(t, "MoveLimitMin");
        v["limit_max"] = vec(t, "MoveLimitMax");
        return v;
    };
    Value areas = Value::array();
    if (const Value* list = map_.find("AreaList"); list && list->isArray()) {
        for (const auto& ar : list->items()) {
            if (!unitIs(ar, "CameraArea") || !byId.count(ar.at("Id").str())) {
                continue;
            }
            Value e = ticketJson(*byId.at(ar.at("Id").str()));
            e["id"] = ar.at("Id");
            e["priority"] = ar.at("Priority");
            e["translate"] = xyz(ar.at("Translate"));
            e["scale"] = xyz(ar.at("Scale"));
            areas.push(e);
        }
    }
    gc["areas"] = areas;
    gc["default"] = ticketJson(def);
    Value projection = Value::object();
    projection["near"] = 100.0;
    projection["far"] = 115000.0;
    projection["aspect"] = Value::list(std::vector<int>{400, 240});
    gc["projection"] = projection;
    scene_["game_camera"] = gc;
    // The inspection camera's preset: the same view.
    const Bytes& code = in_.code();
    Value c = Value::object();
    c["azimuth"] = s.at("angle_h");
    c["elevation"] = s.at("angle_v");
    c["distance"] = s.at("distance");
    c["target"] = s.at("look_at");
    c["fovy"] = num(def, "ZoomFovyFar", 30);
    c["near"] = double(f32(code, 0x14df80));   // camera constructor literal pool (0x24de64)
    c["far"] = double(f32(code, 0x14df84));
    c["aspect"] = 400.0 / 240;
    scene_["camera"] = c;
}

// ---- writing --------------------------------------------------------------------------------

void StageBuilder::finish() {
    if (tangents_.size() / 12 != vertices_.size() / 48) {
        throw ImportError("Internal error: tangent count");
    }
    writeFile(out_ / "vertices.bin", vertices_);
    writeFile(out_ / "tangents.bin", tangents_);
    if (!secondUv_.empty()) {
        std::vector<float> uv1(vertices_.size() / 48 * 2, 0.0f);
        for (const auto& [index, uv] : secondUv_) {
            uv1[index * 2] = uv[0];
            uv1[index * 2 + 1] = uv[1];
        }
        Bytes b(uv1.size() * sizeof(float));
        std::memcpy(b.data(), uv1.data(), b.size());
        writeFile(out_ / "uv1.bin", b);
    }
    writeFile(out_ / "collision.bin", collision_);
    writeFile(out_ / "shadow-casters.bin", casters_);
    writeFile(out_ / "lighting-luts.bin", lutAtlas_);
    writeFile(out_ / "character-skin.bin", characterSkin_);
    if (!characterMiniSkin_.empty()) {
        // The Mini form's document: the clips are shared (KinopioAnimation, UserModelList).
        writeFile(out_ / "character-mini-skin.bin", characterMiniSkin_);
        writeFile(out_ / "character-mini.json", json::dump(characterMini_) + "\n");
    }
    writeFile(out_ / "shaders" / "terrain.vert", deviceShader(vertexShader_, ShaderStage::Vertex));
    for (const auto& [i, text] : shaders_) {
        // The model colour's alpha (an actor's 0x220f30: a fading object, Object::alpha) multiplies
        // the material's output alpha; the session sets objectAlpha for every draw.
        std::string t = text;
        if (t.find("gl_FragColor=previous;") != std::string::npos) {
            t = replaceAll(t, "gl_FragColor=previous;", "gl_FragColor=vec4(previous.rgb,previous.a*objectAlpha);");
            t = replaceFirst(t, "void main()", "uniform float objectAlpha;\nvoid main()");
        }
        writeFile(out_ / "shaders" / (std::to_string(i) + ".frag"), deviceShader(t, ShaderStage::Fragment));
    }
    writeFile(out_ / "character.json", json::dump(character_) + "\n");
    writeFile(out_ / "player.json", json::dump(player_, 1) + "\n");
    for (const auto& [name, actor] : actorFiles_) {
        writeFile(out_ / "actors" / (name + ".json"), json::dump(actor) + "\n");
        writeFile(out_ / "actors" / (name + "-skin.bin"), actorSkins_.at(name));
    }
    scene_["actors"] = actors_;
    scene_["sources"] = in_.sources();
    writeFile(out_ / "report.json", json::dump(report_, 2) + "\n");
    writeFile(out_ / "scene.json", json::dump(scene_, 2) + "\n");
}

// ---- effects and sound ------------------------------------------------------------------------

// The particle data every course shares lives once in the data directory (Effects/Common); the
// course's effects.json names it relatively and lists the EffectYaml entries of its actors.
void StageBuilder::effects() {
    const fs::path data = out_.parent_path().parent_path();
    const fs::path common = data / "Effects" / "Common";
    const std::string prefix = "../../Effects/Common/";
    const ParticleCommon c = writeParticleCommon(in_, common, prefix);
    Value triggers = Value::object();
    std::set<std::string> users = effectUsers_;
    // The protagonist; items; the course clear banner's confetti (the goal demo).
    for (const char* player : {"Kinopio", "PlayerHitReaction", "Coin", "UIHeadCourseClear"}) {
        users.insert(player);
    }
    auto addUser = [&](const std::string& u) {
        const std::string file = u + ".byml";
        if (triggers.has(u)) {
            return;
        }
        if (!hasMember("EffectData/EffectYaml.szs", file)) {
            note("without_effects", u);
            return;
        }
        const Value doc = in_.byml("EffectData/EffectYaml.szs", file);
        if (const Value* list = doc.isObject() ? doc.find("EffectList") : nullptr; list && list->isArray()) {
            triggers[u] = *list;
        }
    };
    for (const auto& u : users) {
        addUser(u);
    }
    // Map effect objects (EffectObj* units).
    Value objects = Value::array();
    for (const char* list : kObjectLists) {
        const Value* l = map_.find(list);
        if (!l || !l->isArray()) {
            continue;
        }
        for (const auto& o : l->items()) {
            const Value* unit = o.isObject() ? o.find("UnitConfigName") : nullptr;
            if (!unit || !unit->isString() || !startsWith(unit->str(), "EffectObj")) {
                continue;
            }
            Value x = Value::object();
            x["id"] = o.at("Id");
            x["unit"] = *unit;
            x["translate"] = xyz(o.at("Translate"));
            x["rotate"] = xyz(o.at("Rotate"));
            x["scale"] = xyz(o.at("Scale"));
            x["emit_ratio"] = o.has("EffectEmitRatio") ? o.at("EffectEmitRatio") : Value(1.0);
            // The class (EffectObjAreaCube applies the scale and the emit ratio, appear 0x24fb9c);
            // the EffectYaml entry is the unit's name (EffectObjFirefly.byml, EffectObjBonfire.byml).
            const Value* uc = o.find("UnitConfig");
            const Value* cls = uc && uc->isObject() ? uc->find("ParameterConfigName") : nullptr;
            x["class"] = cls && cls->isString() ? *cls : *unit;
            x["is_model"] = o.has("IsModel") ? Value(o.at("IsModel").truthy()) : Value(false);
            addUser(unit->str());
            // SwitchAppear (the generic appear listener of the init 0x330e7c -> 0x2ce368): the
            // object and its effect start when that stage switch turns on.
            if (const Value* l = o.find("Links"); l && l->isObject()) {
                if (const Value* a = l->find("SwitchAppear"); a && a->isArray() && a->size() > 0) {
                    x["switch_appear"] = (*a)[0].at("Id");
                }
            }
            objects.push(x);
        }
    }
    // The camera mask (3DSEft.byml CameraMask, read by 0x2e5460 into the object 0x247340 /
    // 0x2471d8; drawn by 0x247140 after the bloom): the model ObjectData/CameraMaskFull (a funnel
    // in camera space) with its own projection (vertical 1 / tan(0.5), aspect 1.6667: 0x24725c;
    // render/CameraMask.hpp), placed at Pos and scaled by Scale in view space. Its combiner (traced registers): colour = Colour1 + Colour2 x (1 - I), alpha = Alpha x
    // I, I the mask texture's intensity (constants Red1/Green1/Blue1/Alpha and Red2/Green2/Blue2).
    // Areas (0x2470c8: switch areas that hide it) not ported.
    if (const Value* mask = design_.has("3DSEft") ? design_.at("3DSEft").find("CameraMask") : nullptr) {
        scene_["camera_mask"] = cameraMask(*mask);
    }
    // The bloom (3DSEft.byml Bloom, read by 0x2e5460: DotColorX/Y/Z and Power as bytes, Spread,
    // Enabled; the post pass 0x388644 / 0x3888c4, render/Bloom.hpp). Areas / AreaPower not ported.
    if (const Value* bloom = design_.has("3DSEft") ? design_.at("3DSEft").find("Bloom") : nullptr) {
        auto byteOf = [&](const char* k) {
            const Value* v = bloom->find(k);
            return v && v->isNumber() ? double(int(v->num()) & 255) : 0.0;
        };
        Value b = Value::object();
        b["enabled"] = bloom->find("Enabled") ? bloom->at("Enabled").truthy() : false;
        b["dot_color"] = doubles({byteOf("DotColorX"), byteOf("DotColorY"), byteOf("DotColorZ")});
        b["power"] = byteOf("Power");
        b["spread"] = byteOf("Spread");
        if (bloom->find("Areas")) {
            note("unapplied", "3DSEft Bloom Areas / AreaPower");
        }
        scene_["bloom"] = b;
    }
    // Stage effect colours (3DSEft.byml StageEffectColor; without it the defaults).
    Value stageColor = Value::array();
    const Value* section = design_.has("3DSEft") ? design_.at("3DSEft").find("StageEffectColor") : nullptr;
    for (int i = 0; i < 2; ++i) {
        Value col = Value::array();
        for (const char* ch : {"Red", "Green", "Blue", "Alpha"}) {
            const Value* v = section ? section->find("Color" + std::to_string(i) + "_" + ch) : nullptr;
            col.push(v ? *v : Value(1.0));
        }
        stageColor.push(col);
    }
    Value meta = Value::object();
    meta["ptcl"] = c.ptcl;
    meta["tables"] = c.tables;
    meta["objects"] = objects;
    meta["stage_color"] = stageColor;
    meta["floor_materials"] = Value::array();   // per collision part instead (player.json)
    meta["ptcl_sha256"] = c.sha;
    meta["textures"] = c.textures;
    meta["sets"] = c.sets;
    meta["triggers"] = triggers;
    meta["shaders"] = c.shaders;
    // The player's own table (0x160a8c / 0x1575e0 play 引っこ抜き成功 from it: its scene stop).
    if (!hitReactions_.has("Player") && hasMember(kProtagonistArchive, "ActorHitReactionCtrlPlayer.byml")) {
        hitReactions_["Player"] = in_.byml(kProtagonistArchive, "ActorHitReactionCtrlPlayer.byml");
    }
    meta["hit_reactions"] = hitReactions_;
    meta["status"] = "Course effects: shared ROM particle data (Effects/Common), the course's EffectYaml entries "
                     "and hit reaction tables.";
    writeFile(out_ / "effects" / "effects.json", json::dump(meta) + "\n");
}

// The middleware data every course shares (and the courses' streams) lives once in Sound/Course;
// the course's sedb.json lists its sound users, stage entries and BGM.
void StageBuilder::sound() {
    const fs::path data = out_.parent_path().parent_path();
    const fs::path common = data / "Sound" / "Course";
    for (const char* f : {"sound_data.bcsar", "SNDTBLS.DAT", "SFXSCRIPTFILES.DAT", "STREAMFILEINFO.DAT",
                          "SND3DLIST.DAT", "ADSRINFO.DAT"}) {
        if (!fs::exists(common / f)) {
            writeFile(common / f, in_.romfs(std::string("SoundData/") + f));
        }
    }
    const auto& db = in_.archive("SoundData/SeDataBase.szs");
    auto doc = [&](const std::string& n) {
        return byml(member(db, n + ".byml"));
    };
    auto hasDoc = [&](const std::string& n) {
        for (const auto& m : db) {
            if (m.name == n + ".byml") {
                return true;
            }
        }
        return false;
    };
    Value ownerDocs = Value::object();
    std::set<std::string> users = soundUsers_;
    for (const char* u : {"PlayerBase", "PlayerActor", "Camera", "StageStartDemoAtmosphere", "StageStartEventSound",
                          "StageScene", "Coin"}) {
        users.insert(u);
    }
    std::vector<std::string> pending(users.begin(), users.end());
    while (!pending.empty()) {   // with the entries they inherit from (ParentName)
        const std::string u = pending.back();
        pending.pop_back();
        if (ownerDocs.has(u)) {
            continue;
        }
        if (!hasDoc(u)) {
            note("without_sound", u);
            continue;
        }
        ownerDocs[u] = doc(u);
        if (const Value* p = ownerDocs.at(u).isObject() ? ownerDocs.at(u).find("ParentName") : nullptr;
            p && p->isString()) {
            pending.push_back(p->str());
        }
    }
    std::set<std::string> names;
    std::function<void(const Value&)> walk = [&](const Value& x) {
        if (x.isObject()) {
            for (const auto& m : x.members()) {
                walk(m.second);
            }
        } else if (x.isArray()) {
            for (const auto& v : x.items()) {
                walk(v);
            }
        } else if (x.isString()) {
            names.insert(x.str());
        }
    };
    walk(ownerDocs);
    Value props = Value::object();
    const Value propertyList = doc("SePropertyList");
    for (const auto& p : propertyList.items()) {
        if (names.count(p.at("Name").str())) {
            props[p.at("Name").str()] = p;
        }
    }
    Value stage = Value::object();
    const Value stageList = doc("SeStageInfoList");
    for (const auto& e : stageList.items()) {
        if (e.at("Name").str() == assets_.stage) {
            stage = e;
        }
    }
    if (!stage.has("AtmosphereSeName")) {
        stage["AtmosphereSeName"] = "";
    }
    // BGM: BgmStageInfoList's stage PlayInfo names the stream.
    const auto& bgmDb = in_.archive("SoundData/BgmDataBase.szs");
    Value bgm = Value::object();
    Value streams = Value::array();
    const Value bgmStages = byml(member(bgmDb, "BgmStageInfoList.byml"));
    for (const auto& e : bgmStages.at("StageInfoList").items()) {
        if (e.at("Name").str() != assets_.stage) {
            continue;
        }
        for (const auto& p : e.at("StagePlayInfo").items()) {
            const std::string stream = p.at("ResourceName").str();
            bgm[p.at("PlayInfoName").str()] = stream;
            const std::string file = "stream/" + stream + ".dspadpcm.bcstm";
            if (!fs::exists(common / file) && Inputs::has("SoundData/" + file)) {
                writeFile(common / file, in_.romfs("SoundData/" + file));
            }
            streams.push(stream);
        }
    }
    // BgmPlayInfoList entries a course can request besides its stage BGM, by their default
    // resource: "Invincible" (the pickaxe's music, line Star) and "KinotanClear" (the goal demo's
    // jingle, GoalPose 0x1df400).
    const Value bgmPlays = byml(member(bgmDb, "BgmPlayInfoList.byml"));
    for (const auto& p : bgmPlays.at("PlayInfoList").items()) {
        const std::string name = p.at("Name").str();
        if ((name != "Invincible" && name != "KinotanClear") || !p.has("DefaultResourceName")) {
            continue;
        }
        const std::string stream = p.at("DefaultResourceName").str();
        bgm[name] = stream;
        // The file's spelling may differ in case from the resource name (WS_Kp_bgm_Pick); the
        // sound archive names the file the runtime opens.
        auto lower = [](std::string x) {
            for (auto& c : x) {
                c = char(std::tolower(static_cast<unsigned char>(c)));
            }
            return x;
        };
        const std::string wanted = lower("SoundData/stream/" + stream + ".dspadpcm.bcstm");
        for (const std::string& path : romfsPaths()) {
            if (lower(path) == wanted) {
                const std::string file = path.substr(std::string("SoundData/").size());
                if (!fs::exists(common / file)) {
                    writeFile(common / file, in_.romfs(path));
                }
            }
        }
        streams.push(stream);
    }
    Value sedb = Value::object();
    sedb["data"] = "../../../Sound/Course";   // relative to this sound directory
    sedb["owners"] = ownerDocs;
    sedb["properties"] = props;
    sedb["stage"] = stage;
    sedb["bgm"] = bgm;
    sedb["streams"] = streams;
    writeFile(out_ / "sound" / "sedb.json", json::dump(sedb, 1));
}

void StageBuilder::run() {
    fs::create_directories(out_ / "textures");
    fs::create_directories(out_ / "actors");
    step(0.05, "Stage data");
    load();
    step(0.10, "Lights");
    presentation();
    step(0.15, "Sky");
    sky();
    step(0.20, "Lighting");
    lighting();
    step(0.25, "Objects");
    placements();
    step(0.70, "Protagonist");
    character(0);
    if (Inputs::has(kProtagonistMiniArchive)) {
        character(1);
    }
    player();
    step(0.85, "Shadows and camera");
    shadows();
    camera();
    step(0.90, "Effects and sound");
    effects();
    sound();
    step(0.95, "Writing");
    finish();
}

}   // namespace

StageAssets stageAssets(Inputs& in, const std::string& stage) {
    StageAssets a;
    a.stage = stage;
    a.assetStage = stage;
    const std::string design = "StageData/" + stage + "Design1.szs";
    if (Inputs::has(design)) {
        for (const auto& m : in.archive(design)) {
            if (m.name == "InitSharedAssets.byml") {
                const Value v = byml(m.data);
                if (const Value* s = v.isObject() ? v.find("SharedStage") : nullptr; s && s->isString()) {
                    a.assetStage = s->str();
                }
            }
        }
    }
    a.base = withoutStage(a.assetStage);
    return a;
}

void exportStage(Inputs& in, const std::string& stage, const fs::path& out, const ProgressFn& progress) {
    StageBuilder(in, stage, out, progress).run();
}

}   // namespace captain_toad
