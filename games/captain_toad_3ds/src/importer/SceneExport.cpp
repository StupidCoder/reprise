// Scene package export: everything the viewer loads, built from the verified ROM inputs.
// A C++ port of the research exporters (tools/export_*.py), producing the identical package
// (tools/compare_packages.py checks this).
//
// Archive and member names in quotes are the ROM's own file names (data literals); they must
// match the ROM exactly and are not descriptive names.
#include "SceneExport.hpp"

#include "Animation.hpp"
#include "ExportCommon.hpp"
#include "Layout.hpp"
#include "ObservedConstants.hpp"
#include "Shaders.hpp"
#include "Texture.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace captain_toad {

namespace fs = std::filesystem;
using json::Value;
using namespace exporter;

namespace {

// ROM archives (RomFS paths). The protagonist's and the companion's archives carry the
// characters' internal file names.
const char* const kTerrain = "ObjectData/Season1OpeningStepA.szs";
const char* const kTextures = "ObjectData/Season1OpeningTextures.szs";
const char* const kMap = "StageData/Season1OpeningStageMap1.szs";
const char* const kDesign = "StageData/Season1OpeningStageDesign1.szs";
const char* const kSky = "ObjectData/SkySeason1Opening.szs";
const char* const kScene = "ObjectData/Season1OpeningScene.szs";
const char* const kLuts = "ObjectData/LutCurveData.szs";
const char* const kProtagonistArchive = "ObjectData/Kinopio.szs";
const char* const kProtagonistAnimArchive = "ObjectData/KinopioAnimation.szs";
const char* const kOpeningAnim = "ObjectData/KinopioAnimationSeason1OpeningStage.szs";
const char* const kCubeMap = "ObjectData/CubeMapSeason1OpeningStage.szs";
const char* const kHeadlight = "ObjectData/PlayerHeadlight.szs";
const char* const kGoalItem = "ObjectData/GoalItem.szs";
const char* const kCompanionArchive = "ObjectData/KinopicoNpc.szs";
const char* const kBird = "ObjectData/Bird.szs";
const char* const kGrass = "ObjectData/OpeningGrass.szs";
const char* const kBossBird = "ObjectData/BossBird.szs";
const char* const kBossBirdTextures = "ObjectData/BossBirdBOSSTextures.szs";
const char* const kBossBirdStage = "ObjectData/BossBirdSeason1OpeningStage.szs";
const char* const kDemoCamera = "ObjectData/DemoCamera.szs";
const char* const kEffectData = "EffectData/AllEffectData.szs";
const char* const kEffectYaml = "EffectData/EffectYaml.szs";
const char* const kIntroLayouts = "UIX2/Eu/GameplaySeasonIntro.arc";
const char* const kWipeLayouts = "UIX2/Eu/Wipes.arc";
constexpr size_t kCodeBase = 0x100000;

// ---- small helpers --------------------------------------------------------------------------

// ---- the package ----------------------------------------------------------------------------

class Builder {
public:
    Builder(Inputs& in, fs::path out, ProgressFn progress)
        : in_(in), out_(std::move(out)), progress_(std::move(progress)) {}
    void run();

private:
    void step(double f, const char* what) {
        if (progress_) {
            progress_(f, what);
        }
    }
    void writeTexture(size_t index, const Bytes& rgba) {
        writeFile(out_ / "textures" / (std::to_string(index) + ".rgba"), rgba);
    }
    size_t textureCount() const { return scene_.at("textures").size(); }
    const Value& map() {
        if (map_.isNull()) {
            map_ = in_.byml(kMap, "Season1OpeningStageMap.byml");
        }
        return map_;
    }
    const std::vector<LutRecord>& luts() {
        if (luts_.empty()) {
            luts_ = lutRecords(in_.bch(kLuts, "LutCurveData.bch"));
        }
        return luts_;
    }

    void terrain();
    void presentation();
    void shadows();
    void character();
    void lighting();
    void effects();
    void actors();
    void player();
    void opening();
    void layouts();
    void particleData();
    void sound();
    void finish();

    // Actor helpers (tools/actor_export.py).
    std::map<std::string, size_t> addTextures(const Bch& b, const std::string& prefix);
    size_t addMaterials(const Bch& b, const Model& model, const std::string& kind, const Value& locals,
                        const std::function<std::string(size_t, const Material&, std::string)>& post,
                        const std::set<std::string>& noBump);
    std::vector<Value> addMeshes(const Model& model, size_t base, const std::map<std::string, size_t>& textureIds,
                                 const std::string& actor, Bytes& skin, bool shadowOnly,
                                 const std::set<std::string>& rawUv);
    struct ActorSpec {
        std::string archive, member, model;
        std::optional<std::string> clip;
        std::vector<std::string> visClips;
        Value placement;
        Value locals;
        std::string prefix;
        std::optional<std::string> shadowProxy;
        double frameOffset = 0.0;
        Value extra = Value::object();
        std::function<std::string(size_t, const Material&, std::string)> post;
        std::set<std::string> rawUv;
        std::map<std::string, size_t> extraTextures;
        bool environment = false;
        std::set<std::string> noBump;
    };
    Value& addActor(const ActorSpec& spec);
    size_t cubeAtlas(const std::string& prefix);
    void addStar(const std::string& prefix, const std::vector<std::string>& clips, bool demo);
    Value starMaterials(const Bch& b, const Model& model, size_t base, const std::string& names);

    Inputs& in_;
    fs::path out_;
    ProgressFn progress_;
    Value scene_;
    Value map_;
    std::vector<LutRecord> luts_;
    Bytes vertices_, tangents_, characterSkin_, casters_, collision_, lutAtlas_;
    std::map<size_t, std::string> shaders_;             // shaders/N.frag
    std::map<std::string, std::string> namedShaders_;   // shaders/NAME
    std::string vertexShader_;
    Value character_, player_;
    std::vector<std::pair<std::string, Value>> actors_;
    std::map<std::string, Bytes> actorSkins_;
    Value& actorJson(const std::string& name) {
        for (auto& a : actors_) {
            if (a.first == name) {
                return a.second;
            }
        }
        throw ImportError("Missing actor " + name);
    }
};

// ---- export_scene.py ------------------------------------------------------------------------

void Builder::terrain() {
    const Bch source = in_.bch(kTerrain, "Season1OpeningStepA.bch");
    const Bch tex = in_.bch(kTextures, "Season1OpeningTextures.bch");
    scene_ = Value::object();
    const auto records = tex.textureRecords();
    std::map<std::string, size_t> textureIndex;
    Value textures = Value::array();
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& t = records[i];
        textureIndex[t.name] = i;
        writeTexture(i, decodeEtc(slice(tex.bytes(), t.offset, picaSize(t.width, t.height, t.format)), t.width,
                                  t.height, t.format));
        textures.push(toJson(t));
    }
    const auto models = source.models();
    const Model& model = models.at(0);
    vertexShader_ = kTerrainVertexShader;
    if (!model.bones.empty()) {
        throw ImportError("Skinned terrain model");
    }
    const double identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    for (int k = 0; k < 12; ++k) {
        if (model.transform[size_t(k)] != identity[k]) {
            throw ImportError("Nonidentity terrain transform");
        }
    }
    const Value& placement =
        findBy(map().at("ObjectList"), [&](const Value& x) { return x.at("UnitConfigName").str() == model.name; });
    for (const char* k : {"X", "Y", "Z"}) {
        if (placement.at("Rotate").at(k).num() != 0 || placement.at("Scale").at(k).num() != 1) {
            throw ImportError("Unhandled terrain placement");
        }
    }
    const Value translation = xyz(placement.at("Translate"));
    const double tr[3] = {translation[0].num(), translation[1].num(), translation[2].num()};
    double lo[3], hi[3];
    for (int j = 0; j < 3; ++j) {
        lo[j] = std::numeric_limits<double>::infinity();
        hi[j] = -lo[j];
    }
    Value meshes = Value::array();
    for (const Mesh& mesh : model.meshes) {
        if (!mesh.visible || (mesh.flags & 1)) {
            throw ImportError("Unexpected hidden terrain mesh");
        }
        const Material& mat = model.materials.at(mesh.material);
        const auto& names = mat.textures;
        const long textureId = names[1] ? long(textureIndex.at(*names[1])) : -1;
        if (names[0] != std::optional<std::string>("shadowmap")) {
            throw ImportError("Unexpected terrain material binding");
        }
        const size_t start = vertices_.size() / 48;
        const Vec4d& scales = mesh.uniform(7);
        const double uvScale = mesh.uniform(8)[0];
        const Vec4d& po = mesh.uniform(6);
        const TexTransform& transform = mat.transforms[1];
        if (textureId >= 0 && transform.v[2] != 0) {
            throw ImportError("Unhandled texture rotation");
        }
        for (const Face& face : mesh.faces) {
            if (face.skin != 0 || !face.palette.empty()) {
                throw ImportError("Unexpected terrain skinning");
            }
            auto it = face.registers.find(0x25e);
            if (it != face.registers.end() && ((it->second >> 8) & 3)) {
                throw ImportError("Non-triangle primitive");
            }
            for (uint32_t index : face.indices) {
                const Vertex& v = mesh.vertices.at(index);
                double pos[3], normal[3], uv[2], color[4];
                for (int j = 0; j < 3; ++j) {
                    pos[j] = v.v[0][size_t(j)] * scales[0] + po[size_t(j)] + tr[j];
                }
                const double defaultNormal[3] = {0, 1, 0};
                for (int j = 0; j < 3; ++j) {
                    normal[j] = (v.has(1) ? v.v[1][size_t(j)] : defaultNormal[j]) * scales[1];
                }
                for (int j = 0; j < 2; ++j) {
                    uv[j] = (v.has(4) ? v.v[4][size_t(j)] : 0.0) * uvScale;
                }
                if (textureId >= 0) {
                    for (int j = 0; j < 2; ++j) {
                        uv[j] = uv[j] * transform.v[size_t(j)] + (-transform.v[size_t(j)] * transform.v[size_t(3 + j)]);
                    }
                }
                for (int j = 0; j < 4; ++j) {
                    color[j] = v.has(3) ? v.v[3][size_t(j)] * scales[3] : 1.0;
                }
                for (double x : {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], uv[0], uv[1], color[0],
                                 color[1], color[2], color[3]}) {
                    if (!std::isfinite(x)) {
                        throw ImportError("Nonfinite vertex");
                    }
                }
                pack(vertices_, {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], uv[0], uv[1], color[0],
                                 color[1], color[2], color[3]});
                for (int j = 0; j < 3; ++j) {
                    lo[j] = std::min(lo[j], pos[j]);
                    hi[j] = std::max(hi[j], pos[j]);
                }
            }
        }
        const uint32_t reg = mat.registers.count(0x93) ? mat.registers.at(0x93) : 0;
        Value m = Value::object();
        m["name"] = mat.name;
        m["first"] = start;
        m["count"] = vertices_.size() / 48 - start;
        m["texture"] = textureId;
        m["detail_texture"] = names[2] ? long(textureIndex.at(*names[2])) : -1L;
        m["wrap_s"] = (reg >> 12) & 15;
        m["wrap_t"] = (reg >> 8) & 15;
        m["source_command_offset"] = mesh.commandOffset;
        m["material_index"] = mesh.material;
        meshes.push(m);
    }
    std::vector<std::array<std::array<double, 3>, 3>> triangles;
    const Value kcl = kclGeometry(in_.file(kTerrain, "Season1OpeningStepA.kcl"), triangles);
    for (const auto& t : triangles) {
        for (const auto& v : t) {
            pack(collision_, {v[0] + tr[0], v[1] + tr[1], v[2] + tr[2]});
        }
    }
    Value materials = Value::array();
    for (const auto& m : model.materials) {
        materials.push(toJson(m));
    }
    scene_["schema"] = 1;
    scene_["name"] = model.name;
    scene_["translation"] = translation;
    scene_["bounds"] = Value::array();
    scene_["bounds"].push(doubles({lo[0], lo[1], lo[2]}));
    scene_["bounds"].push(doubles({hi[0], hi[1], hi[2]}));
    scene_["vertex_layout"] = "little-endian float32 position3 normal3 uv2 color4, 48 bytes";
    scene_["meshes"] = meshes;
    scene_["textures"] = textures;
    scene_["materials"] = materials;
    scene_["collision"] = kcl;
    scene_["collision_vertices"] = collision_.size() / 12;
    scene_["triangle_count"] = vertices_.size() / 144;
    Value tracks = Value::array();
    for (int i = 8; i < 14; ++i) {
        tracks.push(source.table(i).size());
    }
    scene_["animation_tracks"] = tracks;
    Value omitted = Value::array();
    for (size_t i = 1; i < models.size(); ++i) {
        omitted.push(models[i].name);
    }
    scene_["omitted_models"] = omitted;
    scene_["limitations"] = strings({"Static terrain only: no player, other actors, particles, sky or UI.",
                                     "Stored texture combiners and alpha tests translated; fragment lighting inputs "
                                     "approximate, no original normal-map lighting or shadow pass.",
                                     "Collision triangle geometry only; no gameplay or collision solver.",
                                     "Native orbit camera is a viewer service, not recovered camera behavior."});
}

// ---- export_presentation.py -----------------------------------------------------------------

void Builder::presentation() {
    const Bch sky = in_.bch(kSky, "SkySeason1Opening.bch");
    const Bch lights = in_.bch(kScene, "Season1OpeningScene.bch");
    Value records = Value::array();
    for (uint32_t p : lights.table(5)) {
        const uint32_t kind = lights.u32(p + 40) & 15;
        if (kind != 2 && kind != 9) {
            throw ImportError("Unsupported light");
        }
        Value rec = Value::object();
        rec["name"] = lights.nameOf(p);
        rec["source_offset"] = p;
        rec["flags"] = lights.u32(p + 40);
        rec["kind"] = kind;
        if (kind == 2) {
            rec["ambient"] = doubles({lights.u8(p + 48) / 255.0, lights.u8(p + 49) / 255.0, lights.u8(p + 50) / 255.0});
        } else {
            rec["diffuse"] = doubles({lights.u8(p + 52) / 255.0, lights.u8(p + 53) / 255.0, lights.u8(p + 54) / 255.0});
            rec["direction"] = doubles({lights.f32(p + 64), lights.f32(p + 68), lights.f32(p + 72)});
        }
        records.push(rec);
    }
    const Value areas = in_.byml(kDesign, "LightAreas.byml").at("LightAreas");
    Value active = Value::array();
    for (const auto& l :
         findBy(areas, [](const Value& x) { return x.at("areaName").str() == "obj645"; }).at("Lights").items()) {
        active.push(l.at("lightName"));
    }
    Value lighting = Value::object();
    lighting["lights"] = records;
    lighting["active"] = active;
    lighting["light_areas"] = areas;
    scene_["lighting"] = lighting;
    std::map<std::string, size_t> texids;
    for (const auto& t : sky.textureRecords()) {
        const size_t i = textureCount();
        texids[t.name] = i;
        scene_["textures"].push(toJson(t));
        writeTexture(
            i, decodeEtc(slice(sky.bytes(), t.offset, size_t(t.width) * t.height / 2), t.width, t.height, t.format));
    }
    const Model model = sky.models().at(0);
    if (!model.bones.empty()) {
        throw ImportError("Skinned sky model");
    }
    const Value& skyPlacement = map().at("SkyList")[0];
    const Value translation = xyz(skyPlacement.at("Translate"));
    for (const char* k : {"X", "Y", "Z"}) {
        if (skyPlacement.at("Scale").at(k).num() != 1 || skyPlacement.at("Rotate").at(k).num() != 0) {
            throw ImportError("Unhandled sky placement");
        }
    }
    const size_t materialBase = scene_.at("materials").size();
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const Material& m = model.materials[i];
        TevOptions o;
        o.colors = &m.colors;
        o.sky = true;
        o.transforms = &m.transforms;
        shaders_[materialBase + i] = generateTev(m.fragment, o);
        scene_["materials"].push(toJson(m));
    }
    const double tr[3] = {translation[0].num(), translation[1].num(), translation[2].num()};
    Value skyMeshes = Value::array();
    size_t skyVertices = 0;
    for (const Mesh& mesh : model.meshes) {
        const Material& m = model.materials.at(mesh.material);
        const size_t start = vertices_.size() / 48;
        for (const Face& face : mesh.faces) {
            if (face.skin != 0 || !face.palette.empty()) {
                throw ImportError("Unexpected sky skinning");
            }
            for (uint32_t index : face.indices) {
                const Vertex& v = mesh.vertices.at(index);
                const Vec4d& sc = mesh.uniform(7);
                const Vec4d& po = mesh.uniform(6);
                if (!v.has(4) || !v.has(3)) {
                    throw ImportError("Sky vertex lacks attributes");
                }
                const double u8s = mesh.uniform(8)[0];
                pack(vertices_, {v.v[0][0] * sc[0] + po[0] + tr[0], v.v[0][1] * sc[0] + po[1] + tr[1],
                                 v.v[0][2] * sc[0] + po[2] + tr[2], 0, 1, 0, v.v[4][0] * u8s, v.v[4][1] * u8s,
                                 v.v[3][0] * sc[3], v.v[3][1] * sc[3], v.v[3][2] * sc[3], v.v[3][3] * sc[3]});
            }
        }
        Value x = Value::object();
        x["name"] = m.name;
        x["first"] = start;
        x["count"] = vertices_.size() / 48 - start;
        skyVertices += vertices_.size() / 48 - start;
        x["texture"] = texids.at(*m.textures[1]);
        x["texture0"] = texids.at(*m.textures[0]);
        x["detail_texture"] = -1;
        x["wrap_s"] = 2;
        x["wrap_t"] = 2;
        x["material_index"] = materialBase + mesh.material;
        x["sky"] = true;
        x["samplers"] = samplersJson(m.samplers);   // ROM samplers (SkyMat clamps T)
        skyMeshes.push(x);
    }
    for (const auto& m : scene_.at("meshes").items()) {
        skyMeshes.push(m);
    }
    scene_["meshes"] = skyMeshes;
    // Camera preset: CameraParam.byml tickets and the camera constructor's literal-pool floats (0x24de64).
    const Value camera = in_.byml(kMap, "CameraParam.byml");
    const Value& start =
        findBy(camera.at("StartTickets"), [](const Value& x) { return x.at("Name").str() == "Start"; }).at("Param");
    const Value& def =
        findBy(camera.at("DefaultTickets"), [](const Value& x) { return x.at("Name").str() == "Default"; }).at("Param");
    const Bytes& code = in_.code();
    Value cam = Value::object();
    cam["azimuth"] = start.at("AngleH");
    cam["elevation"] = start.at("AngleV");
    cam["distance"] = start.at("Distance");
    cam["target"] = xyz(start.at("LookAtPos"));
    cam["fovy"] = def.at("ZoomFovyFar");
    cam["near"] = double(f32(code, 0x14df80));
    cam["far"] = double(f32(code, 0x14df84));
    cam["constructor_fovy"] = double(f32(code, 0x14df88));
    cam["aspect"] = 400.0 / 240;
    cam["status"] = "Opening preset from CameraParam.byml and ROM camera constructor constants, confirmed by runtime "
                    "matrices. Camera transitions and zoom logic are not recovered.";
    cam["evidence"] = "evidence/runs/presentation-a/memory/33100000.bin";
    cam["view_address"] = "0x3310b40c";
    cam["projection_address"] = "0x3310b468";
    scene_["camera"] = cam;
    Value skyInfo = Value::object();
    skyInfo["model"] = model.name;
    skyInfo["translation"] = translation;
    skyInfo["triangles"] = skyVertices / 3;
    skyInfo["status"] = "Static ROM mesh, vertex colours, texture transforms and six-stage combiners; no sky animation "
                        "or depth-of-field reconstruction.";
    scene_["sky"] = skyInfo;
    scene_["limitations"] = strings({"Static scene: no characters, gameplay, particles or UI.", "", "", ""});
    scene_["limitations"][2] = cam.at("status");
    scene_["limitations"][3] = skyInfo.at("status");
}

// ---- export_shadows.py ----------------------------------------------------------------------

void Builder::shadows() {
    const Value raw = in_.byml(kDesign, "DepthShadow.byml");
    const Value params = raw.at("DepthShadowParam").at("DefaultDepthShadowParam");
    const Value light = raw.at("DirLitParam").at("DefaultDirLitParam");
    const double x = light.at("DirectionParamX").num(), y = light.at("DirectionParamY").num();
    const double direction[3] = {-std::sin(x) * std::cos(y), -std::sin(y), -std::cos(x) * std::cos(y)};
    const Bch terrain = in_.bch(kTerrain, "Season1OpeningStepA.bch");
    const auto models = terrain.models();
    const Model& shd = modelNamed(models, "Season1OpeningStepA_shd");
    if (!shd.bones.empty()) {
        throw ImportError("Skinned shadow proxy");
    }
    const Value& translation = scene_.at("translation");
    for (const Mesh& mesh : shd.meshes) {
        for (const Face& face : mesh.faces) {
            if (face.skin != 0 || !face.palette.empty()) {
                throw ImportError("Unexpected proxy skinning");
            }
            for (uint32_t index : face.indices) {
                const auto& v = mesh.vertices.at(index).v[0];
                const double scale = mesh.uniform(7)[0];
                const Vec4d& offset = mesh.uniform(6);
                pack(casters_,
                     {v[0] * scale + offset[0] + translation[0].num(), v[1] * scale + offset[1] + translation[1].num(),
                      v[2] * scale + offset[2] + translation[2].num()});
            }
        }
    }
    Value& lighting = scene_["lighting"];
    Value override = Value::object();
    override["enabled"] = false;
    override["rom_enabled"] = light.at("IsEnableDefaultParam");
    override["diffuse"] = Value::array();
    for (const char* k : {"ColorR", "ColorG", "ColorB"}) {
        override["diffuse"].push(light.at(k));
    }
    override["direction"] = doubles({direction[0], direction[1], direction[2]});
    override["status"] =
        "DepthShadow default directional override, runtime values confirmed; submission path under investigation.";
    lighting["directional_override"] = override;
    Value shadow = Value::object();
    shadow["parameters"] = params;
    shadow["directional_parameters"] = light;
    shadow["direction"] = override.at("direction");
    shadow["tint"] = Value::array();
    for (const char* k : {"ColorR", "ColorG", "ColorB", "ColorA"}) {
        shadow["tint"].push(params.at(k).num() / 255);
    }
    shadow["original_resolution"] = raw.at("GlobalParam").at("ShadowMapWidth");
    shadow["native_resolution"] = 2048;
    shadow["filter"] = "5x5 bilinear PCF";
    shadow["caster_model"] = shd.name;
    shadow["caster_triangles"] = casters_.size() / 36;
    shadow["status"] = "ROM tint and direction; native visible-terrain alpha-tested casters, world-space fitting, bias "
                       "and PCF. Original proxy retained for research. Dynamic actors absent.";
    shadow["native_caster_triangles"] = 7552;
    scene_["shadow"] = shadow;
    // Scene light-set references select the terrain lights.
    const Bch sceneBch = in_.bch(kScene, "Season1OpeningScene.bch");
    Value sets = Value::object();
    for (uint32_t q : sceneBch.table(14)) {
        const uint32_t entries = sceneBch.u32(q + 12), count = sceneBch.u32(q + 16);
        for (uint32_t j = 0; j < count; ++j) {
            const size_t e = entries + j * 12;
            const uint32_t index = sceneBch.u32(e), names = sceneBch.u32(e + 4), n = sceneBch.u32(e + 8);
            Value list = Value::array();
            for (uint32_t k = 0; k < n; ++k) {
                list.push(sceneBch.nameOf(names + k * 4));
            }
            sets[std::to_string(index)] = list;
        }
    }
    lighting["scene_sets"] = sets;
    lighting["active"] = sets.at("1");
    Value lutList = Value::array();
    for (const auto& l : luts()) {
        lutList.push(toJson(l));
    }
    lighting["luts"] = lutList;
    lighting["lut_rows"] = luts().size();
    std::string v = shadowVertex(vertexShader_);
    v = replaceAll(v, "void main()", "varying vec3 eyePosition; varying vec3 surfaceTangent;\nvoid main()");
    v = replaceAll(v, "vertexColor=gl_Color;",
                   "eyePosition=(gl_ModelViewMatrix*gl_Vertex).xyz;surfaceTangent=gl_NormalMatrix*gl_MultiTexCoord1."
                   "xyz;vertexColor=gl_Color;");
    vertexShader_ = v;
    for (const Mesh& mesh : models.at(0).meshes) {
        for (const Face& face : mesh.faces) {
            for (uint32_t index : face.indices) {
                const Vertex& vx = mesh.vertices.at(index);
                const double scale = mesh.uniform(7)[2];
                const double t[3] = {vx.has(2) ? vx.v[2][0] : 1.0, vx.has(2) ? vx.v[2][1] : 0.0,
                                     vx.has(2) ? vx.v[2][2] : 0.0};
                pack(tangents_, {t[0] * scale, t[1] * scale, t[2] * scale});
            }
        }
    }
    for (int64_t k = 0; k < scene_.at("sky").at("triangles").integer() * 3; ++k) {
        pack(tangents_, {1, 0, 0});
    }
    scene_["limitations"][1] =
        "ROM-based lighting and PCF shadows; original full shading pipeline remains under investigation.";
}

// ---- export_character.py --------------------------------------------------------------------

void Builder::character() {
    const Bch b = in_.bch(kProtagonistArchive, "Kinopio.bch");
    const Model model = b.models().at(0);
    const Bch ab = in_.bch(kProtagonistAnimArchive, "KinopioAnimation.bch");
    const Value anim = characterAnimation(ab, "Wait");
    const Value vis = visibility(ab, {"WaitEye", "WaitFace", "WaitHandL", "WaitHandR"});
    const Value names = nodeNames(b);
    const Bch eb = in_.bch(kCubeMap, "CubeMapSeason1OpeningStage.bch");
    std::map<std::string, size_t> texids;
    std::vector<std::pair<const Bch*, TextureRecord>> list;
    for (const auto& t : b.textureRecords()) {
        list.push_back({&b, t});
    }
    for (const auto& t : eb.textureRecords()) {
        if (t.name == "SphereMap") {
            list.push_back({&eb, t});
        }
    }
    for (const auto& [src, t] : list) {
        const size_t i = textureCount();
        texids[t.name] = i;
        scene_["textures"].push(toJson(t));
        if (t.format != 12 && t.format != 13 && t.format != 6) {
            throw ImportError("Unsupported character texture");
        }
        writeTexture(i, decodePica(slice(src->bytes(), t.offset, picaSize(t.width, t.height, t.format)), t.width,
                                   t.height, t.format));
    }
    const size_t materialBase = scene_.at("materials").size();
    for (const auto& m : model.materials) {
        scene_["materials"].push(toJson(m));
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
            if (f.skin != 1 && f.skin != 2) {
                throw ImportError("Unsupported skin type");
            }
            for (uint32_t idx : f.indices) {
                const Vertex& v = mesh.vertices.at(idx);
                double pos[3], normal[3], t[3], uv[2], c[4];
                for (int k = 0; k < 3; ++k) {
                    pos[k] = v.v[0][size_t(k)] * sc[0] + po[size_t(k)];
                    if (!v.has(1)) {
                        throw ImportError("Character vertex lacks a normal");
                    }
                    normal[k] = v.v[1][size_t(k)] * sc[1];
                    t[k] = (v.has(2) ? v.v[2][size_t(k)] * sc[2] : (k == 0 ? 1.0 : 0.0));
                }
                for (int k = 0; k < 2; ++k) {
                    uv[k] = (v.has(4) ? v.v[4][size_t(k)] : 0.0) * u[0] * uvs[k] + uvo[k];
                }
                for (int k = 0; k < 4; ++k) {
                    c[k] = v.has(3) ? v.v[3][size_t(k)] * sc[3] : 1.0;
                }
                const Skin s = skinOf(v, f, u[3], 1e-5, false);
                packSkin(characterSkin_, pos, normal, t, s, f.skin);
                pack(vertices_,
                     {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], uv[0], uv[1], c[0], c[1], c[2], c[3]});
                pack(tangents_, {t[0], t[1], t[2]});
            }
        }
        for (const auto& n : mat.textures) {
            if (n && *n != "shadowmap" && !texids.count(*n)) {
                throw ImportError("Unknown character texture " + *n);
            }
        }
        auto id = [&](const std::optional<std::string>& n) {
            return n && texids.count(*n) ? long(texids.at(*n)) : -1L;
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
        scene_["meshes"].push(m);
        Value entry = Value::object();
        entry["mesh_index"] = scene_.at("meshes").size() - 1;
        entry["node"] = names[mesh.node];
        characterMeshes.push(entry);
    }
    const Value& placement = map().at("PlayerList")[0];
    // Retargeting (0x294d7c): the protagonist's clips are retargeted at scale 1 from the
    // "Kinopio" lengths of PlayerRetargettingInfo (observed in the opening, gpu-trace-opening-a).
    const Value retarget =
        in_.byml("ObjectData/PlayerRetargettingInfo.szs", "PlayerRetargettingInfo.byml").at("Kinopio");
    Value bones = Value::array();
    for (const auto& bone : model.bones) {
        Value j = toJson(bone);
        if (retarget.has(bone.name)) {
            j["retarget_source_length"] = retarget.at(bone.name).num();
        }
        bones.push(j);
    }
    character_ = Value::object();
    character_["model"] = model.name;
    character_["bones"] = bones;
    character_["animation"] = anim;
    character_["visibility"] = vis;
    character_["meshes"] = characterMeshes;
    character_["first_vertex"] = first;
    character_["vertex_count"] = characterSkin_.size() / 72;
    character_["translation"] = xyz(placement.at("Translate"));
    character_["rotation"] = xyz(placement.at("Rotate"));
    character_["frames_per_second"] = 60;
    character_["status"] =
        "ROM Wait clip preview; original action selection, blend state and timing verification pending.";
    Value summary = Value::object();
    summary["model"] = model.name;
    summary["bones"] = model.bones.size();
    summary["clip"] = "Wait";
    summary["frames"] = anim.at("frames");
    scene_["character"] = summary;
    scene_["limitations"][0] =
        "Static level with ROM character idle animation preview; no movement, gameplay or other actors.";
}

// ---- export_lighting.py ---------------------------------------------------------------------

void Builder::lighting() {
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
            const uint32_t cp = hb.u32(q + 4), cn = hb.u32(q + 8);
            if (cn != 260 || hb.u32(cp + 4) != 0x0fff01c8) {
                throw ImportError("Unsupported LUT command stream");
            }
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
    Value extraJson = Value::array();
    for (const auto& e : extra) {
        Value x = Value::object();
        x["name"] = e.name;
        x["table"] = e.table;
        x["absolute"] = e.absolute;
        extraJson.push(x);
    }
    lighting["extra_luts"] = extraJson;
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
    auto local = [&](const Bch& b, const std::string& name, Value attach, const std::string& archive,
                     const std::string& member, const char* status) {
        Value rec;
        for (uint32_t q : b.table(5)) {
            if (b.name(q) == name) {
                rec = lightRecord(b, q);
                break;
            }
        }
        if (rec.isNull()) {
            throw ImportError("Missing light " + name);
        }
        const Value& s = rec.at("samplers");
        rec["distance_row"] = rowOf(s[0], s[1]);
        rec["spot_row"] = rowOf(s[2], s[3]);
        bool spotAbsolute = false;
        if (s[3].truthy()) {
            for (const auto& e : extra) {
                if (e.name == s[3].str()) {
                    spotAbsolute = e.absolute != 0;
                    break;
                }
            }
        }
        rec["spot_absolute"] = spotAbsolute;
        rec["attach"] = attach;
        rec["source"] = "rom:" + archive + "/" + member;
        rec["status"] = status;
        return rec;
    };
    const Value initLight = in_.byml(kGoalItem, "InitLightActor.byml").at("Lights")[0];
    const Value& goal = map().at("GoalList")[0].at("Links").at("GoalItem")[0];
    if (goal.at("UnitConfigName").str() != "GoalItem") {
        throw ImportError("Unexpected goal item");
    }
    const Bch gb = in_.bch(kGoalItem, "GoalItem.bch");
    const Model gmodel = gb.models().at(0);
    const Bone* joint = nullptr;
    for (const auto& bone : gmodel.bones) {
        if (bone.name == initLight.at("ActorJointName").str()) {
            joint = &bone;
        }
    }
    if (!joint) {
        throw ImportError("Missing goal light joint");
    }
    Value headAttach = Value::object();
    headAttach["kind"] = "joint";
    headAttach["joint"] = "HeadLight";
    const Value headlamp =
        local(hb, "spotLight", headAttach, kHeadlight, "PlayerHeadlight.bch",
              "PlayerHeadlight spotLight on the player HeadLight joint (traced colour/LUT/position match). "
              "Runtime beam length comes from an unrecovered collision check; native ray-cast approximation.");
    const Value& gt = goal.at("Translate");
    Value starAttach = Value::object();
    starAttach["kind"] = "world";
    starAttach["position"] = doubles({gt.at("X").num() + joint->translation[0],
                                      gt.at("Y").num() + joint->translation[1] + initLight.at("ty").num(),
                                      gt.at("Z").num() + joint->translation[2]});
    const Value star = local(
        gb, initLight.at("lightName").str(), starAttach, kGoalItem, "GoalItem.bch",
        "GoalItem light at actor + joint + InitLightActor ty; static (no bob animation). Star model not rendered yet.");
    Value locals = Value::object();
    locals["headlamp"] = headlamp;
    locals["star"] = star;
    lighting["local_lights"] = locals;
    const Bch sb = in_.bch(kScene, "Season1OpeningScene.bch");
    Value sceneLights = Value::object();
    for (uint32_t q : sb.table(5)) {
        sceneLights[sb.nameOf(q)] = lightRecord(sb, q);
    }
    lighting["scene_lights"] = sceneLights;
    Value lists = Value::object();
    lists["terrain"] = strings({"mainLight", "secondLight", "headlamp", "star"});
    lists["character"] = strings({"mainLight", "headlamp"});
    lighting["light_lists"] = lists;
    lighting["status"] = "Generic PICA fragment lighting from ROM-derived material registers (trace-checked), "
                         "runtime light lists observed in GPU traces; native PCF shadows with original zero intensity.";
    // Material lighting state and shaders.
    const Bch terrain = in_.bch(kTerrain, "Season1OpeningStepA.bch");
    const Bch protagonistBch = in_.bch(kProtagonistArchive, "Kinopio.bch");
    const auto terrainMaterials = terrain.models().at(0).materials;
    const auto protagonistModels = protagonistBch.models();
    const auto& characterMaterials = protagonistModels.at(0).materials;
    Value& materials = scene_["materials"];
    size_t characterBase = 0;
    bool found = false;
    for (size_t i = terrainMaterials.size(); i < materials.size() && !found; ++i) {
        if (materials[i].at("name").str() == characterMaterials.at(0).name) {
            characterBase = i;
            found = true;
        }
    }
    if (!found) {
        throw ImportError("Character materials not found");
    }
    struct Assignment {
        size_t index;
        const Bch* b;
        const Material* m;
        bool character;
    };
    std::vector<Assignment> assignments;
    for (size_t i = 0; i < terrainMaterials.size(); ++i) {
        assignments.push_back({i, &terrain, &terrainMaterials[i], false});
    }
    for (size_t i = 0; i < characterMaterials.size(); ++i) {
        assignments.push_back({characterBase + i, &protagonistBch, &characterMaterials[i], true});
    }
    for (const auto& a : assignments) {
        const Material& m = *a.m;
        if (materials[a.index].at("name").str() != m.name) {
            throw ImportError("Material order mismatch");
        }
        const PicaState st = picaState(*a.b, m, luts());
        TevOptions o;
        o.colors = &m.colors;
        o.texture0 = a.character;
        std::string source = generateTev(m.fragment, o);
        if (a.character && ((m.transforms[1].flags >> 16) & 255) == 2) {
            // Original reflection coordinates (fitted to traced vertex outputs): uv = 0.5 * world reflection.xy + 0.5.
            source = replaceAll(source, "vec4 tex1=texture2D(albedo,gl_TexCoord[0].xy);",
                                "vec3 reflected=viewToWorld*reflect(normalize(eyePosition),normalize(normal));vec4 "
                                "tex1=texture2D(albedo,reflected.xy*0.5+0.5);");
            source = replaceFirst(source, "void main()", "uniform mat3 viewToWorld;\nvoid main()");
        }
        shaders_[a.index] = picaLightingFragment(source, m, st);
        Value pica = Value::object();
        pica["kind"] = a.character ? "character" : "terrain";
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
        pica["locals"] = a.character ? strings({"headlamp"}) : strings({"headlamp", "star"});
        materials[a.index]["pica"] = pica;
    }
    // Original shadow-pass casters (traced): the player's skinned shadow proxy (_shd model) as a shadow-only mesh.
    const Model& shd = modelNamed(protagonistModels, "Kinopio_shd");
    const Model& main = protagonistModels.at(0);
    if (shd.bones.size() != main.bones.size()) {
        throw ImportError("Shadow proxy skeleton differs");
    }
    for (size_t i = 0; i < shd.bones.size(); ++i) {
        if (shd.bones[i].name != main.bones[i].name) {
            throw ImportError("Shadow proxy skeleton differs");
        }
    }
    if (size_t(character_.at("first_vertex").integer()) + characterSkin_.size() / 72 != vertices_.size() / 48) {
        throw ImportError("Character skin must end the vertex buffer");
    }
    const size_t start = vertices_.size() / 48;
    for (const Mesh& mesh : shd.meshes) {
        const Vec4d &sc = mesh.uniform(7), &u = mesh.uniform(8), &po = mesh.uniform(6);
        for (const Face& f : mesh.faces) {
            if (f.skin != 1 && f.skin != 2) {
                throw ImportError("Unsupported shadow proxy skin type");
            }
            for (uint32_t idx : f.indices) {
                const Vertex& v = mesh.vertices.at(idx);
                const double pos[3] = {v.v[0][0] * sc[0] + po[0], v.v[0][1] * sc[0] + po[1], v.v[0][2] * sc[0] + po[2]};
                const double normal[3] = {0, 1, 0}, tangent[3] = {1, 0, 0};
                packSkin(characterSkin_, pos, normal, tangent, skinOf(v, f, u[3], 1e-5, false), f.skin);
                pack(vertices_, {pos[0], pos[1], pos[2], 0, 1, 0, 0, 0, 1, 1, 1, 1});
                pack(tangents_, {1, 0, 0});
            }
        }
    }
    Value proxy = Value::object();
    proxy["name"] = "Kinopio_shd";
    proxy["first"] = start;
    proxy["count"] = vertices_.size() / 48 - start;
    proxy["texture"] = -1;
    proxy["texture0"] = -1;
    proxy["detail_texture"] = -1;
    proxy["wrap_s"] = 2;
    proxy["wrap_t"] = 2;
    proxy["material_index"] = characterBase;
    proxy["character"] = true;
    proxy["shadow_only"] = true;
    scene_["meshes"].push(proxy);
    character_["vertex_count"] = characterSkin_.size() / 72;
    // Original clip volume: ShadowBounds box; bias = BiasFactor x projected depth span.
    const Value& area = findBy(map().at("AreaList"), [](const Value& a) { return unitIs(a, "ShadowBounds"); });
    double t[3], s[3];
    for (int k = 0; k < 3; ++k) {
        const char* axis = k == 0 ? "X" : k == 1 ? "Y" : "Z";
        t[k] = area.at("Translate").at(axis).num();
        s[k] = area.at("Scale").at(axis).num();
        if (std::abs(area.at("Rotate").at(axis).num()) >= 1e-6) {
            throw ImportError("Rotated shadow bounds");
        }
    }
    const double lo[3] = {t[0] - 500 * s[0], t[1], t[2] - 500 * s[2]};
    const double hi[3] = {t[0] + 500 * s[0], t[1] + 1000 * s[1], t[2] + 500 * s[2]};
    Value& shadow = scene_["shadow"];
    double toward[3];
    for (int k = 0; k < 3; ++k) {
        toward[k] = -shadow.at("direction")[size_t(k)].num();
    }
    const double n = std::sqrt(((0 + toward[0] * toward[0]) + toward[1] * toward[1]) + toward[2] * toward[2]);
    for (double& x : toward) {
        x /= n;
    }
    Value corners = Value::array();
    double minDepth = std::numeric_limits<double>::infinity(), maxDepth = -minDepth;
    for (int c = 0; c < 8; ++c) {
        const double p[3] = {(c & 4) ? hi[0] : lo[0], (c & 2) ? hi[1] : lo[1], (c & 1) ? hi[2] : lo[2]};
        corners.push(doubles({p[0], p[1], p[2]}));
        const double depth = dot3(p, toward);
        minDepth = std::min(minDepth, depth);
        maxDepth = std::max(maxDepth, depth);
    }
    const double span = maxDepth - minDepth;
    Value bounds = Value::object();
    bounds["area"] = area.has("Id") ? area.at("Id") : Value("ShadowBounds");
    bounds["corners"] = corners;
    bounds["depth_span"] = span;
    bounds["bias_world"] = shadow.at("parameters").at("BiasFactor").num() * span;
    bounds["status"] = "ROM ShadowBounds box and BiasFactor; matches traced projection.";
    shadow["bounds"] = bounds;
    Value casters = Value::object();
    casters["mode"] = "rom_proxies";
    casters["terrain"] = "Season1OpeningStepA_shd";
    casters["character"] = "Kinopio_shd";
    casters["status"] = "Original shadow-pass proxies (traced draws); native 2048 map with PCF.";
    shadow["casters"] = casters;
}

// ---- export_effects.py ----------------------------------------------------------------------

void Builder::effects() {
    const Bytes& raw = in_.file(kEffectData, "EffectData.ptcl");
    const uint32_t texTable = u32(raw, 24), texSize = u32(raw, 28);
    Value items = Value::array();
    auto registersOf = [](const observed::TracedDraw& d) {
        Registers r;
        for (uint32_t k = 0; k < 64; ++k) {
            r[0xc0 + k] = d.registers[k];
        }
        r[0x104] = d.registers[64];
        return r;
    };
    struct Halo {
        const char* name;
        uint32_t offset, format;
        const observed::TracedDraw* draw;
    };
    for (const Halo& h :
         {Halo{"glow", 0x342f80, 13, &observed::kHeadlampGlow}, Halo{"beam", 0x343080, 12, &observed::kHeadlampBeam}}) {
        if (!(texTable <= h.offset && h.offset < texTable + texSize)) {
            throw ImportError("Effect texture outside the PTCL texture table");
        }
        const size_t size = h.format == 13 ? 256 : 128;
        const size_t index = textureCount();
        Value t = Value::object();
        t["name"] = std::string("ptcl_") + h.name;
        t["width"] = 16;
        t["height"] = 16;
        t["format"] = h.format;
        t["source"] = "EffectData.ptcl";
        t["offset"] = h.offset;
        scene_["textures"].push(t);
        writeTexture(index, decodeEtc(slice(raw, h.offset, size), 16, 16, h.format));
        const Registers regs = registersOf(*h.draw);
        Value e = Value::object();
        e["name"] = h.name;
        e["texture"] = index;
        e["tev_registers"] = registersJson(regs);
        e["blend"] = h.draw->blend;
        e["depth"] = h.draw->depth;
        e["shader"] = std::string("effect-") + h.name + ".frag";
        TevOptions o;
        o.texture0 = true;
        namedShaders_[e.at("shader").str()] = generateTev(regs, o);
        items.push(e);
    }
    // PTCL emitter fields with laws checked against traced per-particle uniforms (see export_effects.py).
    Value& glow = items[0];
    glow["kind"] = "glow_particles";
    glow["joint"] = "HeadLight";
    glow["size"] = 170.0;
    glow["color"] = doubles({0.34375, 0.31651, 0.20947});
    glow["interval"] = 31;
    glow["life"] = 75;
    glow["alpha_peak"] = 0.5;
    glow["alpha_in"] = 17;
    glow["alpha_out"] = 59;
    glow["joint_offset"] = doubles({0.0, 0.0, 50.0});
    glow["camera_offset"] = 100.0;
    glow["phase"] = 16;
    glow["status"] = "PTCL Glow fields with traced emission/alpha law (exact per-particle alpha sequence).";
    Value& beam = items[1];
    beam["kind"] = "beam_shaft";
    beam["joint"] = "HeadLight";
    beam["size"] = doubles({40.0, 300.0});
    beam["color"] = doubles({0.0703, 0.0679, 0.0322});
    beam["intervals"] = Value::list(std::vector<int>{3, 6});
    beam["life"] = 30;
    beam["alpha_peak"] = 1.0;
    beam["alpha_in"] = 6;
    beam["alpha_out"] = 24;
    beam["spawn_radius"] = 15.0;
    beam["cone_degrees"] = 20.0;
    beam["speed"] = 0.02;
    beam["status"] = "PTCL Emitter1 size/colour/life/alpha with traced emission pattern and spawn/direction spread.";
    // Sky texture-coordinate animation (SkySeason1Opening.cmtsa): coordinate-1 U scroll.
    const Bch sb = in_.bch(kSky, "SkySeason1Opening.bch");
    const uint32_t anim = sb.table(9).at(0);
    const double duration = sb.f32(anim + 8);
    const uint32_t a = sb.u32(anim + 12), n = sb.u32(anim + 16);
    std::map<std::string, Value> curves;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t q = sb.u32(a + i * 4), cp = sb.u32(q + 12);
        const uint32_t flags = sb.u32(cp + 8), seg = sb.u32(cp + 12), dataPtr = sb.u32(cp + 32);
        const double vscale = sb.f32(cp + 16), voffset = sb.f32(cp + 20), fscale = sb.f32(cp + 24);
        if (seg != 0x20701) {
            throw ImportError("Unsupported sky curve encoding");
        }
        Value keys = Value::array();
        for (uint32_t k = 0; k < (seg >> 16); ++k) {
            const uint32_t w = sb.u32(dataPtr + 4 * k);
            keys.push(doubles({double(w & 0xfff) * fscale, double(w >> 12) * vscale + voffset}));
        }
        Value c = Value::object();
        c["target"] = "texcoord1_translate_u";
        c["keys"] = keys;
        c["repeat"] = bool(flags & 0x10000);
        c["source_offset"] = cp;
        curves[sb.nameOf(q)] = c;
    }
    Value& materials = scene_["materials"];
    for (size_t i = 0; i < materials.size(); ++i) {
        auto it = curves.find(materials[i].at("name").str());
        if (it == curves.end()) {
            continue;
        }
        const std::string scaleU = json::formatNumber(materials[i].at("texture_transforms")[1][1]);
        std::string& text = shaders_.at(i);
        std::string line;
        for (const auto& l : splitLines(text)) {
            if (startsWith(l, "vec4 tex1=texture2D(albedo,")) {
                line = l;
                break;
            }
        }
        const size_t split = line.find("+vec2(");
        if (line.empty() || split == std::string::npos) {
            throw ImportError("Sky shader lacks a texture transform");
        }
        const std::string after = line.substr(split + 6);
        std::string staticV = after.substr(after.find(',') + 1);
        staticV = staticV.substr(0, staticV.find(','));
        while (!staticV.empty() && (staticV.back() == ')' || staticV.back() == ';')) {
            staticV.pop_back();
        }
        const std::string replacement =
            line.substr(0, split) + "+vec2(-" + scaleU + "*texTranslate1U," + staticV + "));";
        text = replaceFirst(replaceAll(text, line, replacement), "void main()",
                            "uniform float texTranslate1U;\nvoid main()");
        materials[i]["uv_animation"] = it->second;
    }
    Value& skyInfo = scene_["sky"];
    Value skyAnim = Value::object();
    skyAnim["duration"] = duration;
    skyAnim["start_frame"] = 775;
    skyAnim["frames_per_second"] = 60;
    skyAnim["status"] = "ROM cmtsa coordinate-1 U scroll; start phase from traced opening (frame ~775).";
    skyInfo["animation"] = skyAnim;
    // Wind-blown leaves (EffectObjWindGrass): PTCL textures and the traced combiner of draw 272.
    std::vector<size_t> leafTextures;
    for (const auto& [offset, size] : std::vector<std::pair<uint32_t, uint32_t>>{{0x344800, 16}, {0x343560, 32}}) {
        const size_t index = textureCount();
        char name[32];
        std::snprintf(name, sizeof name, "ptcl_leaf_%x", offset);
        Value t = Value::object();
        t["name"] = name;
        t["width"] = size;
        t["height"] = size;
        t["format"] = 12;
        t["source"] = "EffectData.ptcl";
        t["offset"] = offset;
        scene_["textures"].push(t);
        writeTexture(index, decodeEtc(slice(raw, offset, size * size / 2), size, size, 12));
        leafTextures.push_back(index);
    }
    const Registers leafRegs = registersOf(observed::kWindLeaves);
    TevOptions o;
    o.texture0 = true;
    namedShaders_["effect-leaves.frag"] =
        replaceAll(generateTev(leafRegs, o), "vec4 tex1=texture2D(albedo,gl_TexCoord[0].xy);",
                   "vec4 tex1=texture2D(albedo,gl_TexCoord[1].xy);");
    const Value& emitter =
        findBy(map().at("ObjectList"), [](const Value& x) { return unitIs(x, "EffectObjWindGrass"); });
    Value leaves = Value::object();
    leaves["name"] = "leaves";
    leaves["kind"] = "leaf_particles";
    leaves["texture"] = leafTextures[0];
    leaves["texture1"] = leafTextures[1];
    leaves["shader"] = "effect-leaves.frag";
    leaves["tev_registers"] = registersJson(leafRegs);
    leaves["blend"] = observed::kWindLeaves.blend;
    leaves["depth"] = observed::kWindLeaves.depth;
    leaves["position"] = xyz(emitter.at("Translate"));
    leaves["yaw"] = emitter.at("Rotate").at("Y");
    leaves["scale"] = emitter.at("Scale").at("X");
    leaves["life"] = 300.0;
    leaves["interval"] = 3;
    leaves["box"] = doubles({300.0, 300.0, 300.0});
    leaves["local_velocity_min"] = doubles({-1.35, -1.57, 2.1});
    leaves["local_velocity_max"] = doubles({1.35, 1.57, 3.2});
    leaves["spin_base"] = -0.05236;
    leaves["spin_range"] = 0.1047;
    leaves["scale_base"] = doubles({14.38, 74.79});
    leaves["scale_random_min"] = 0.53;
    leaves["scale_rate"] = doubles({1.0008, 0.9978});
    leaves["fade_in"] = 29.0;
    Value colors = Value::array();
    colors.push(doubles({0.6324, 0.9302, 0.5322}));
    colors.push(doubles({0.426, 0.8326, 0.426}));
    colors.push(doubles({0.1682, 0.4651, 0.1622}));
    leaves["colors"] = colors;
    leaves["color"] = Value::list(std::vector<int>{1, 1, 1});
    leaves["status"] = "Laws measured from traced per-particle state + PTCL fields; random seeding and velocity-spread "
                       "field mapping not recovered.";
    items.push(leaves);
    Value effects = Value::object();
    effects["items"] = items;
    effects["status"] =
        "Headlamp halo: PTCL textures, traced combiners; see export_effects.py for interpretation limits.";
    scene_["effects"] = effects;
}

// ---- export_actors.py / actor_export.py -----------------------------------------------------

std::map<std::string, size_t> Builder::addTextures(const Bch& b, const std::string& prefix) {
    std::map<std::string, size_t> ids;
    for (const auto& t : b.textureRecords()) {
        const size_t i = textureCount();
        ids[t.name] = i;
        TextureRecord renamed = t;
        renamed.name = prefix + ":" + t.name;
        scene_["textures"].push(toJson(renamed, false));
        writeTexture(i, decodePica(slice(b.bytes(), t.offset, picaSize(t.width, t.height, t.format)), t.width, t.height,
                                   t.format));
    }
    return ids;
}

size_t Builder::addMaterials(const Bch& b, const Model& model, const std::string& kind, const Value& locals,
                             const std::function<std::string(size_t, const Material&, std::string)>& post,
                             const std::set<std::string>& noBump) {
    const size_t base = scene_.at("materials").size();
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const Material& m = model.materials[i];
        const PicaState st = picaState(b, m, luts(), !noBump.count(m.name));
        TevOptions o;
        o.texture0 = true;
        if (!usesLighting(m)) {
            // The combiner never reads fragment lighting (e.g. GoalItem glow quads).
            std::string source = generateTev(m.fragment, o);
            if (post) {
                source = post(base + i, m, source);
            }
            shaders_[base + i] = source;
            scene_["materials"].push(toJson(m));
            continue;
        }
        o.colors = &m.colors;
        std::string source = generateTev(m.fragment, o);
        if (((m.transforms[1].flags >> 16) & 255) == 2) {
            source = replaceAll(source, "vec4 tex1=texture2D(albedo,gl_TexCoord[0].xy);",
                                "vec3 reflected=viewToWorld*reflect(normalize(eyePosition),normalize(normal));vec4 "
                                "tex1=texture2D(albedo,reflected.xy*0.5+0.5);");
            source = replaceFirst(source, "void main()", "uniform mat3 viewToWorld;\nvoid main()");
        }
        source = picaLightingFragment(source, m, st);
        if (post) {
            source = post(base + i, m, source);
        }
        shaders_[base + i] = source;
        Value entry = toJson(m);
        Value pica = Value::object();
        pica["kind"] = kind;
        pica["config0"] = st.config0;
        pica["config1"] = st.config1;
        pica["lut_names"] = st.lutNamesJson();
        pica["colors"] = st.colorsJson();
        pica["flags"] = st.flags;
        pica["light_set"] = st.lightSet;
        pica["locals"] = locals;
        entry["pica"] = pica;
        scene_["materials"].push(entry);
    }
    return base;
}

std::vector<Value> Builder::addMeshes(const Model& model, size_t base, const std::map<std::string, size_t>& textureIds,
                                      const std::string& actor, Bytes& skin, bool shadowOnly,
                                      const std::set<std::string>& rawUv) {
    std::vector<Value> meshes;
    for (const Mesh& mesh : model.meshes) {
        const Material& mat = model.materials.at(mesh.material);
        const size_t start = vertices_.size() / 48;
        const Vec4d &sc = mesh.uniform(7), &u = mesh.uniform(8), &po = mesh.uniform(6);
        double uvs[2] = {1, 1}, uvo[2] = {0, 0};   // raw coordinates: transforms evaluated in the shader
        if (!(shadowOnly || rawUv.count(mat.name) || !(mat.transforms[1].flags & 255))) {
            scaleOffset(mat.transforms[1], uvs, uvo);
        }
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
                    uv[k] = (v.has(4) ? v.v[4][size_t(k)] : 0.0) * u[0] * uvs[k] + uvo[k];
                }
                for (int k = 0; k < 4; ++k) {
                    c[k] = v.has(3) ? v.v[3][size_t(k)] * sc[3] : 1.0;
                }
                // Some vertices carry more weights than bone indices; only paired weights contribute.
                const Skin s = skinOf(v, f, u[3], 1e-4, true);
                packSkin(skin, pos, normal, t, s, (f.skin == 1 || f.skin == 2) ? f.skin : 0);
                pack(vertices_,
                     {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], uv[0], uv[1], c[0], c[1], c[2], c[3]});
                pack(tangents_, {t[0], t[1], t[2]});
            }
        }
        auto id = [&](const std::optional<std::string>& n) {
            if (!n) {
                return -1L;
            }
            auto it = textureIds.find(*n);
            return it == textureIds.end() ? -1L : long(it->second);
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
        m["material_index"] = base + mesh.material;
        m["samplers"] = samplersJson(mat.samplers);
        m["actor"] = actor;
        m["node"] = mesh.node;
        m["shadow_only"] = shadowOnly;
        meshes.push_back(m);
    }
    return meshes;
}

Value& Builder::addActor(const ActorSpec& spec) {
    const Bch b = in_.bch(spec.archive, spec.member);
    const auto models = b.models();
    const Model& model = modelNamed(models, spec.model);
    auto textureIds = addTextures(b, spec.prefix);
    for (const auto& [k, v] : spec.extraTextures) {
        textureIds[k] = v;
    }
    const size_t base = addMaterials(b, model, "actor", spec.locals, spec.post, spec.noBump);
    Bytes skin;
    const size_t first = vertices_.size() / 48;
    auto meshes = addMeshes(model, base, textureIds, spec.prefix, skin, false, spec.rawUv);
    if (spec.shadowProxy) {
        const Model& shd = modelNamed(models, *spec.shadowProxy);
        if (shd.bones.size() != model.bones.size()) {
            throw ImportError("Shadow proxy skeleton differs");
        }
        for (size_t i = 0; i < shd.bones.size(); ++i) {
            if (shd.bones[i].name != model.bones[i].name) {
                throw ImportError("Shadow proxy skeleton differs");
            }
        }
        for (auto& m : addMeshes(shd, base, {}, spec.prefix, skin, true, {})) {
            meshes.push_back(m);
        }
    }
    const Value names = nodeNames(b);
    Value entries = Value::array();
    for (auto& m : meshes) {
        if (spec.environment) {   // drawn with the scenery, before drop-shadow volumes (traced order)
            m.erase("actor");
        }
        const int64_t node = m.at("node").integer();
        scene_["meshes"].push(m);
        Value e = Value::object();
        e["mesh_index"] = scene_.at("meshes").size() - 1;
        e["node"] = names[size_t(node)];
        entries.push(e);
    }
    Value anim;
    if (spec.clip) {
        anim = actorAnimation(b, *spec.clip);
    } else {
        anim = Value::object();
        anim["name"] = nullptr;
        anim["frames"] = 1.0;
        anim["loop"] = true;
        anim["tracks"] = Value::object();
        anim["matrices"] = Value::object();
    }
    size_t modelIndex = 0;
    while (models[modelIndex].name != spec.model) {
        ++modelIndex;
    }
    const auto flags = boneFlags(b, modelIndex);
    Value bones = Value::array();
    for (size_t i = 0; i < model.bones.size(); ++i) {
        Value bone = toJson(model.bones[i]);
        bone["billboard"] = (flags.at(i) >> 16) & 7;
        bones.push(bone);
    }
    Value actor = Value::object();
    actor["name"] = spec.prefix;
    actor["model"] = spec.model;
    actor["bones"] = bones;
    actor["animation"] = anim;
    actor["material_base"] = base;
    actor["visibility"] = spec.visClips.empty() ? Value::object() : visibility(b, spec.visClips);
    actor["meshes"] = entries;
    actor["first_vertex"] = first;
    actor["vertex_count"] = skin.size() / 72;
    actor["translation"] = xyz(spec.placement.at("Translate"));
    actor["rotation"] = xyz(spec.placement.at("Rotate"));
    actor["scale"] = xyz(spec.placement.at("Scale"));
    actor["frames_per_second"] = 60;
    actor["frame_offset"] = spec.frameOffset;
    actor["source"] = "rom:" + spec.archive + "/" + spec.member;
    actor["clip"] = spec.clip ? Value(*spec.clip) : Value();
    for (const auto& [k, v] : spec.extra.members()) {
        actor[k] = v;
    }
    Value ref = Value::object();
    ref["name"] = spec.prefix;
    ref["file"] = "actors/" + spec.prefix + ".json";
    ref["skin"] = "actors/" + spec.prefix + "-skin.bin";
    if (!scene_.has("actors")) {
        scene_["actors"] = Value::array();
    }
    scene_["actors"].push(ref);
    actorSkins_[spec.prefix] = std::move(skin);
    actors_.emplace_back(spec.prefix, actor);
    return actors_.back().second;
}

size_t Builder::cubeAtlas(const std::string& prefix) {
    // Stage cube map faces (+X,-X,+Y,-Y,+Z,-Z top to bottom) stacked in one texture.
    const Bch cb = in_.bch(kCubeMap, "CubeMapSeason1OpeningStage.bch");
    for (const auto& rec : cb.textureRecords()) {
        if (rec.name != "CubeMap") {
            continue;
        }
        if (rec.format != 12) {
            throw ImportError("Unexpected cube map format");
        }
        Bytes atlas;
        for (uint32_t reg = 0x85; reg < 0x8b; ++reg) {
            const Bytes face = decodeEtc(slice(cb.bytes(), rec.registers.at(reg), size_t(rec.width) * rec.height / 2),
                                         rec.width, rec.height, 12);
            atlas.insert(atlas.end(), face.begin(), face.end());
        }
        const size_t cube = textureCount();
        Value t = Value::object();
        t["name"] = prefix + ":CubeMapAtlas";
        t["width"] = rec.width;
        t["height"] = rec.height * 6;
        t["format"] = 12;
        t["source"] = "CubeMapSeason1OpeningStage.bch faces 0x85-0x8a";
        scene_["textures"].push(t);
        writeTexture(cube, atlas);
        return cube;
    }
    throw ImportError("Missing stage cube map");
}

// GoalItem glow/sparkle TEV stage -> constant colour index, as regenerated at run time
// (evidence/runs/goal-gpu-700-c draws 121-123): stage 1 carries constant 2, the other stages constant 0.
const std::vector<std::pair<int, std::vector<int>>> kStarConstantStages = {{0, {0, 2, 3, 4, 5}}, {2, {1}}};

Value Builder::starMaterials(const Bch& b, const Model& model, size_t base, const std::string& names) {
    const Value srt = materialAnimation(b, names + ".cmtsa"), col = materialAnimation(b, names + ".cmcla");
    Value mats = Value::array();
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const Material& m = model.materials[i];
        Value entry = Value::object();
        entry["material_index"] = base + i;
        entry["slots"] = Value::object();
        entry["constants"] = Value::array();
        for (int k = 0; k < 2; ++k) {
            const TexTransform& t = m.transforms[size_t(k)];
            if (m.name == "Eyes" || !((t.flags & 255) && m.textures[size_t(k)])) {
                continue;
            }
            Value curves = Value::object();
            const std::string prefix = "tc" + std::to_string(k) + "_";
            for (const auto& e : srt.at("elements").items()) {
                if (e.at("material").str() == m.name && startsWith(e.at("target").str(), prefix)) {
                    curves[e.at("target").str().substr(e.at("target").str().find('_') + 1)] = e.at("axes");
                }
            }
            Value slot = Value::object();
            slot["convention"] = (t.flags >> 8) & 255;
            slot["scale"] = doubles({t.v[0], t.v[1]});
            slot["rotation"] = t.v[2];
            slot["translation"] = doubles({t.v[3], t.v[4]});
            slot["curves"] = curves;
            slot["frames"] = srt.at("frames");
            entry["slots"][std::to_string(k)] = slot;
        }
        if (m.name != "Eyes") {
            for (const auto& [idx, stages] : kStarConstantStages) {
                (void)stages;
                Value baseColor = Value::array();
                for (int k = 0; k < 4; ++k) {
                    baseColor.push(m.colors[size_t(20 + 4 * idx + k)] / 255.0);
                }
                Value axes = Value::array();
                for (int k = 0; k < 4; ++k) {
                    axes.push(nullptr);
                }
                for (const auto& e : col.at("elements").items()) {
                    if (e.at("material").str() == m.name && e.at("target").str() == "constant" + std::to_string(idx)) {
                        axes = e.at("axes");
                        break;
                    }
                }
                Value c = Value::object();
                c["base"] = baseColor;
                c["axes"] = axes;
                c["frames"] = col.at("frames");
                c["uniform"] = "animatedConstant" + std::to_string(idx);
                entry["constants"].push(c);
            }
        }
        if (entry.at("slots").size() || entry.at("constants").size()) {
            mats.push(entry);
        }
    }
    return mats;
}

void Builder::addStar(const std::string& prefix, const std::vector<std::string>& clips, bool demo) {
    // GoalItem star: root = map Translate + UnitConfig DisplayTranslate; the demo star stands at the goal object.
    const Value& goal = map().at("GoalList")[0].at("Links").at("GoalItem")[0];
    const Value& display = goal.at("UnitConfig").at("DisplayTranslate");
    Value placement = goal;
    for (const char* k : {"X", "Y", "Z"}) {
        placement["Translate"][k] = goal.at("Translate").at(k).num() + display.at(k).num();
    }
    if (demo) {
        placement = map().at("GoalList")[0];
    }
    const Bch b = in_.bch(kGoalItem, "GoalItem.bch");
    const size_t cube = cubeAtlas(prefix);
    const Model model = b.models().at(0);
    ActorSpec spec;
    spec.archive = kGoalItem;
    spec.member = "GoalItem.bch";
    spec.model = "GoalItem";
    spec.clip = "Wait";
    spec.placement = placement;
    spec.locals = strings({"star"});
    spec.prefix = prefix;
    if (!demo) {
        spec.shadowProxy = "GoalItem_shd";
    }
    spec.frameOffset = 558.0;
    spec.post = [](size_t, const Material& m, std::string source) {
        if (m.name == "Eyes") {
            source = cubeShader(source);
        }
        std::vector<int> slots;
        for (int k = 0; k < 2; ++k) {
            if ((m.transforms[size_t(k)].flags & 255) && m.textures[size_t(k)] && m.name != "Eyes") {
                slots.push_back(k);
            }
        }
        if (!slots.empty()) {
            source = animatedUvShader(source, slots);
        }
        if (m.name != "Eyes") {
            for (const auto& [idx, stages] : kStarConstantStages) {
                source = animatedConstantShader(source, stages, "animatedConstant" + std::to_string(idx));
            }
        }
        return source;
    };
    for (const auto& m : model.materials) {
        if (m.name != "Eyes") {
            spec.rawUv.insert(m.name);
        }
    }
    spec.extraTextures["CubeMap"] = cube;
    Value curveClips = Value::object();
    for (const auto& c : clips) {
        if (c != "Wait") {
            curveClips[c] = actorAnimation(b, c);
        }
    }
    spec.extra["curve_clips"] = curveClips;
    spec.extra["hidden"] = demo;
    if (!demo) {
        Value drop = Value::object();
        drop["joint"] = "GoalItem";
        drop["radius"] = 35.0;
        drop["height"] = 100.0;
        drop["top_offset"] = -12.0;
        drop["status"] =
            "InitShadowMask ellipsoid projection Scale 70, DropLength 100; traced cylinder radius 35, top joint-12";
        spec.extra["drop_shadow"] = drop;
    }
    Value& actor = addActor(spec);
    const size_t base = size_t(actor.at("material_base").integer());
    static const std::map<std::string, std::string> clipMaterials = {{"Wait", "GoalItem"},
                                                                     {"GoalPoseSign", "GoalPoseSign"},
                                                                     {"GoalPosePair", "GoalPosePair"},
                                                                     {"DemoOpeningSeason1", "DemoOpeningSeason1"}};
    actor["material_animation"] = starMaterials(b, model, base, "GoalItem");
    Value clipAnims = Value::object();
    for (const auto& c : clips) {
        if (c != "Wait") {
            clipAnims[c] = starMaterials(b, model, base, clipMaterials.at(c));
        }
    }
    actor["clip_material_animation"] = clipAnims;
}

void Builder::actors() {
    // The companion waits at her rail's first point facing the player spawn (checked against traced bone matrices).
    Value npc = map().at("GoalList")[0].at("Links").at("PlayerNpc")[0];
    if (npc.at("ModelName").str() != "KinopicoNpc") {
        throw ImportError("Unexpected NPC model");
    }
    const Value rail0 = npc.at("Links").at("Rail")[0].at("RailPoints")[0].at("Translate");
    const Value& player = map().at("PlayerList")[0].at("Translate");
    const double yaw =
        std::atan2(player.at("X").num() - rail0.at("X").num(), player.at("Z").num() - rail0.at("Z").num()) *
        (180.0 / M_PI);
    npc["Translate"] = rail0;
    Value rotate = Value::object();
    rotate["X"] = 0.0;
    rotate["Y"] = yaw;
    rotate["Z"] = 0.0;
    npc["Rotate"] = rotate;
    {
        ActorSpec spec;
        spec.archive = kCompanionArchive;
        spec.member = "KinopicoNpc.bch";
        spec.model = "KinopicoNpc";
        spec.clip = "Wait";
        spec.visClips = {"Wait"};
        spec.placement = npc;
        spec.locals = strings({"headlamp"});
        spec.prefix = "companion";
        spec.shadowProxy = "KinopicoNpc_shd";
        spec.frameOffset = 130.0;
        // Traced config0 bit 28 clear on these materials (the head keeps bump); cause not recovered.
        spec.noBump = {"KinopicoBodyMat00", "KinopicoBodyMat01", "KinopicoRuckMat00"};
        Value drop = Value::object();
        drop["joint"] = "JointRoot";
        drop["radius"] = 37.5;
        drop["height"] = 100.0;
        drop["status"] = "InitShadowMask cylinder Radius 75 (radius = Radius/2 as interpreted for the player)";
        spec.extra["drop_shadow"] = drop;
        addActor(spec);
    }
    // Birds: recovered idle state machine (src/Bird.hpp). Their reference state at the
    // checkpoint (clip frames 9/10, traced yaw) depends on random draws before it and is observed.
    const Bch birdBch = in_.bch(kBird, "Bird.bch");
    Value clips = Value::object();
    for (const char* c : {"GroundWaitA", "GroundWaitB", "Turn", "FlyWait"}) {
        const Value a = actorAnimation(birdBch, c);
        Value x = Value::object();
        x["frames"] = a.at("frames");
        x["matrices"] = a.at("matrices");
        clips[c] = x;
    }
    const double initialYaw[2] = {3.7122161654427557, 84.52473966028553};
    size_t birdIndex = 0;
    for (const auto& bird : map().at("ObjectList").items()) {
        if (!unitIs(bird, "Bird")) {
            continue;
        }
        if (birdIndex >= 2) {
            throw ImportError("Unexpected bird count");
        }
        ActorSpec spec;
        spec.archive = kBird;
        spec.member = "Bird.bch";
        spec.model = "Bird";
        spec.clip = "GroundWaitB";
        spec.placement = bird;
        spec.locals = strings({"star"});
        spec.prefix = "bird" + std::to_string(birdIndex);
        spec.shadowProxy = "Bird_shd";
        spec.extra["clips"] = clips;
        Value behaviour = Value::object();
        behaviour["clip"] = "GroundWaitB";
        behaviour["frame"] = 9 + int(birdIndex);
        behaviour["step"] = 9 + int(birdIndex);
        behaviour["yaw"] = initialYaw[birdIndex];
        behaviour["status"] = "Recovered idle states (0x4075b4/0x38e4b0); reference state traced; random draws from a "
                              "native xorshift128 with a fixed seed.";
        spec.extra["bird_behaviour"] = behaviour;
        addActor(spec);
        ++birdIndex;
    }
    addStar("star", {"Wait", "GoalPoseSign"}, false);
    addStar("demostar", {"GoalPosePair", "DemoOpeningSeason1"}, true);
    // BossBird for the after-goal demo: model, separate texture archive and the stage's demo clip.
    {
        const Bch tb = in_.bch(kBossBirdTextures, "BossBirdBOSSTextures.bch");
        const Bch sb = in_.bch(kBossBirdStage, "BossBirdSeason1OpeningStage.bch");
        auto textures = addTextures(tb, "bossbird");
        textures["CubeMap"] = cubeAtlas("bossbird");
        const Model model = in_.bch(kBossBird, "BossBird.bch").models().at(0);
        std::set<std::string> cubeMaterials;
        for (const auto& m : model.materials) {
            if (m.textures[0] == std::optional<std::string>("CubeMap")) {
                cubeMaterials.insert(m.name);
            }
        }
        ActorSpec spec;
        spec.archive = kBossBird;
        spec.member = "BossBird.bch";
        spec.model = "BossBird";
        spec.placement = map().at("GoalList")[0];
        spec.locals = strings({"star"});
        spec.prefix = "bossbird";
        spec.post = [cubeMaterials](size_t, const Material& m, std::string source) {
            return cubeMaterials.count(m.name) ? cubeShader(source) : source;
        };
        spec.extraTextures = textures;
        Value curveClips = Value::object(), curveVis = Value::object();
        curveClips["DemoOpeningSeason1"] = actorAnimation(sb, "DemoOpeningSeason1");
        curveVis["DemoOpeningSeason1"] = visibility(sb, {"DemoOpeningSeason1"});
        spec.extra["curve_clips"] = curveClips;
        spec.extra["curve_clip_visibility"] = curveVis;
        spec.extra["hidden"] = true;
        addActor(spec);
    }
    // OpeningGrass linked from GrassControl (wind recovered in src/GrassWind.hpp).
    const Value& control = findBy(map().at("ObjectList"), [](const Value& x) { return unitIs(x, "GrassControl"); });
    {
        ActorSpec spec;
        spec.archive = kGrass;
        spec.member = "OpeningGrass.bch";
        spec.model = "OpeningGrass";
        spec.placement = control.at("Links").at("GrassObj")[0];
        spec.locals = strings({"headlamp", "star"});
        spec.prefix = "grass";
        spec.environment = true;
        Value gc = Value::object();
        gc["chain"] = strings({"Grass00Root", "Grass00Tip01", "Grass00Tip02"});
        gc["bend_rate"] = control.at("BendRate");
        gc["slide_max"] = control.at("SlideMax");
        gc["wind_x"] = control.at("BaseWindX");
        gc["wind_z"] = control.at("BaseWindZ");
        gc["time_shift"] = control.at("TimeShift");
        gc["frames"] = 90;
        gc["start_index"] = 84;
        gc["status"] = "Recovered GrassControl wind (single Grass*Root chain).";
        spec.extra["grass_control"] = gc;
        addActor(spec);
    }
    scene_["limitations"][0] = "ROM actors with preview clips (no behaviour/state machines).";
}

// ---- export_player.py -----------------------------------------------------------------------

const char* const kPlayerClips[] = {
    "Wait",     "WalkSlowly", "Walk",     "WalkSpeedy",  "Run",           "TurnAround", "RunTurn",
    "Fall",     "FallPanic",  "Land",     "LandPanic",   "PullOutSign",   "RunSign",    "Turn",
    "WaitRuck", "WaitRove",   "WaitYawn", "WaitSitDown", "WaitSleepLoop", "Shout",      "Sneeze"};

Value clipWithVisibility(const Bch& b, const std::string& name) {
    Value c = Value::object();
    c["animation"] = characterAnimation(b, name);
    c["visibility"] = visibility(b, {name + "Eye", name + "Face", name + "HandL", name + "HandR"});
    return c;
}

void Builder::player() {
    const Bch ab = in_.bch(kProtagonistAnimArchive, "KinopioAnimation.bch");
    Value clips = Value::object();
    for (const char* name : kPlayerClips) {
        clips[name] = clipWithVisibility(ab, name);
    }
    character_["clips_curves"] = clips;
    // Action -> clip table: every clip plays under its own name, ActionAnimCtrl.byml overrides
    // (Move and similar actions select among sub-clips; the first is the action's clip).
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
        if (info == clipInfo.end()) {   // clip missing from the archive: action not exported
            continue;
        }
        Value a = Value::object();
        a["clip"] = subs[0];
        a["frames"] = info->second.first;
        a["loop"] = info->second.second;
        a["clips"] = subs;
        actions[e.at("ActionName").str()] = a;
    }
    const Value& start = map().at("PlayerList")[0];
    const Value& terrain = findBy(map().at("ObjectList"), [](const Value& o) {
        const Value* u = o.find("UnitConfigName");
        return u && u->isString() && u->str().find("Season1OpeningStep") != std::string::npos;
    });
    Value areas = Value::object();
    areas["death"] = Value::array();
    areas["player_anim"] = Value::array();
    areas["water"] = Value::array();
    for (const auto& a : map().at("AreaList").items()) {
        const std::string kind = a.at("UnitConfigName").str();
        if (kind != "DeathArea" && kind != "PlayerAnimArea") {
            continue;
        }
        for (const char* k : {"X", "Y", "Z"}) {
            if (std::abs(a.at("Rotate").at(k).num()) > 1e-6) {
                throw ImportError("Rotated area (only the unrotated path is ported)");
            }
        }
        Value box = Value::object();
        box["translate"] = xyz(a.at("Translate"));
        box["scale"] = xyz(a.at("Scale"));
        if (kind == "PlayerAnimArea") {
            box["anim"] = a.at("PlayerAnimName");
            box["start_frame"] = a.at("AnimStartFrame");
            box["interval"] = a.at("IntervalFrame");
            areas["player_anim"].push(box);
        } else {
            areas["death"].push(box);
        }
    }
    writeFile(out_ / "player-collision.kcl", in_.file(kTerrain, "Season1OpeningStepA.kcl"));
    player_ = Value::object();
    player_["status"] =
        "Recovered player movement (stage 5 work): pad, action machine (11 states), walk, collider, animation timing. "
        "Unported states are blocked and reported.";
    Value s = Value::object();
    s["translate"] = xyz(start.at("Translate"));
    s["rotate"] = xyz(start.at("Rotate"));
    player_["start"] = s;
    Value col = Value::object();
    col["file"] = "player-collision.kcl";
    col["translate"] = xyz(terrain.at("Translate"));
    player_["collision"] = col;
    player_["areas"] = areas;
    player_["actions"] = actions;
    player_["anim_interp"] = in_.byml(kProtagonistAnimArchive, "AnimInterp.byml");
    Value clipNames = Value::array();
    for (const char* c : kPlayerClips) {
        clipNames.push(c);
    }
    player_["clips"] = clipNames;
    Value ref = Value::object();
    ref["file"] = "player.json";
    ref["clips"] = clipNames.size();
    scene_["player"] = ref;
}

// ---- export_opening.py ----------------------------------------------------------------------

void Builder::opening() {
    const Value cam = in_.byml(kMap, "CameraParam.byml");
    const Value& start = cam.at("StartTickets")[0].at("Param");
    if (!(start.find("IsSetLookAt") && start.at("IsSetLookAt").truthy())) {
        throw ImportError("Start ticket without LookAtPos is not covered by the opening port");
    }
    const Value& ticket = findBy(cam.at("Tickets"), [](const Value& t) {
                              const Value* id = t.find("Id");
                              const Value* obj = id ? id->find("ObjId") : nullptr;
                              return obj && obj->isString() && obj->str() == "obj543";
                          }).at("Param");
    const Value& area = findBy(map().at("AreaList"), [](const Value& a) { return a.at("Id").str() == "obj543"; });
    for (const char* key : {"IsSetDistanceDetail", "IsUpSideDown", "IsFollowLookAtPlayerX", "IsFollowLookAtPlayerY",
                            "IsFollowLookAtPlayerZ", "IsUseMoveLimitMinX", "IsUseMoveLimitMaxX", "IsUseMoveLimitMinY",
                            "IsUseMoveLimitMaxY", "IsUseMoveLimitMinZ", "IsUseMoveLimitMaxZ"}) {
        if (ticket.find(key) && ticket.at(key).truthy()) {
            throw ImportError(std::string(key) + " is set; the package format does not carry it yet");
        }
    }
    Value gc = Value::object();
    gc["status"] =
        "Recovered game camera (Start ticket camera 0x4ccf44, player camera 0x4ccbc0, director and interpolator); "
        "validated per tick against traced runs (evidence/camera).";
    Value s = Value::object();
    s["angle_v"] = start.at("AngleV");
    s["angle_h"] = start.at("AngleH");
    s["distance"] = start.at("Distance");
    s["look_at"] = xyz(start.at("LookAtPos"));
    s["look_at_offset"] = xyz(start.at("LookAtOffset"));
    s["fovy"] = 35.0;
    s["interpole_step"] = 60;
    gc["start"] = s;
    Value a = Value::object();
    a["id"] = "obj543";
    a["translate"] = xyz(area.at("Translate"));
    a["scale"] = xyz(area.at("Scale"));
    a["priority"] = area.at("Priority");
    a["look_at"] = xyz(ticket.at("LookAtPos"));
    a["distance"] = ticket.at("Distance");
    a["distance_xyz"] = Value::array();
    for (const char* k : {"DistanceX", "DistanceY", "DistanceZ"}) {
        a["distance_xyz"].push(ticket.at(k));
    }
    a["angle_v"] = Value::array();
    a["angle_v"].push(ticket.at("AngleV").at("Min"));
    a["angle_v"].push(ticket.at("AngleV").at("Max"));
    a["angle_start"] = Value::array();
    a["angle_start"].push(ticket.at("AngleStart").at("Vertical"));
    a["angle_start"].push(ticket.at("AngleStart").at("Horizontal"));
    a["interpole_step"] = ticket.at("InterpoleStep");
    a["reset_force"] = ticket.at("IsResetForce");
    a["zoom_fovy"] = Value::array();
    for (const char* k : {"ZoomFovy", "ZoomFovyMedium", "ZoomFovyFar"}) {
        a["zoom_fovy"].push(ticket.at(k));
    }
    a["follow_offset"] = xyz(ticket.at("FollowLookAtPlayerOffset"));
    gc["area"] = a;
    Value projection = Value::object();
    projection["near"] = 100.0;
    projection["far"] = 115000.0;
    projection["aspect"] = Value::list(std::vector<int>{400, 240});
    gc["projection"] = projection;
    scene_["game_camera"] = gc;
    // Goal sequence and stage start (values from the goal object initialiser 0x1ec824 and the sensors).
    const Value& goalObj = map().at("GoalList")[0];
    const Value& goalItem = goalObj.at("Links").at("GoalItem")[0];
    const Value& demo = map().at("DemoObjList")[0];
    Value ss = Value::object();
    ss["status"] =
        "Recovered stage-start demo (actor 0x206204, nerves 0x433338/0x433380/0x205a38/0x433300, scene BGM delay "
        "0x147b3c, camera end interpolation 0x23e994); validated per tick against evidence/runs/director-intro-a.";
    ss["demo_kind"] = demo.at("UnitConfigName");
    ss["demo_yaw"] = demo.at("Rotate").at("Y");
    ss["start_animation"] = demo.at("StartAnimation");
    ss["player_yaw"] = map().at("PlayerList")[0].at("Rotate").at("Y");
    ss["ghost_stage"] = false;   // Season1OpeningStage is not one of the ghost stages with BGM delay 109
    scene_["stage_start"] = ss;
    Value goal = Value::object();
    goal["status"] =
        "Recovered goal sequence timing (GoalItem Get 0x3ce6a8, goal demo 0x1df400, clip lengths); validated per tick "
        "against evidence/runs/goal-star-a and goal-timeline-a (evidence/goal).";
    goal["position"] = xyz(goalObj.at("Translate"));
    Value root = Value::array();
    for (const char* k : {"X", "Y", "Z"}) {
        root.push(goalItem.at("Translate").at(k).num() + goalItem.at("UnitConfig").at("DisplayTranslate").at(k).num());
    }
    goal["star_root"] = root;
    Value body = Value::object();
    body["offset"] = 80.0;
    body["radius"] = 100.0;
    goal["star_body"] = body;
    Value head = Value::object();
    head["offset"] = 125.0;
    head["radius"] = 55.0;
    goal["player_head"] = head;
    Value gcam = Value::object();
    gcam["eye_up"] = 100.0;
    gcam["eye_front"] = 550.0;
    gcam["target_up"] = 130.0;
    gcam["fovy"] = 35.0;
    goal["camera"] = gcam;
    Value steps = Value::object();
    steps["course_clear"] = 96;
    steps["end"] = 160;
    goal["demo_steps"] = steps;
    goal["pair"] = true;
    goal["clear_after_demo"] = goalObj.at("IsClearAfterDemo");
    Value gclips = Value::object();
    gclips["sign"] = "GoalPosePairSign";
    gclips["pair"] = "GoalPosePair";
    gclips["star_sign"] = "GoalPoseSign";
    gclips["demo"] = "DemoOpeningSeason1";
    goal["clips"] = gclips;
    scene_["goal"] = goal;
    // After-goal demo camera (DemoCamera.bch DemoOpeningSeason1): position, target and fovy curves.
    const Bch cb = in_.bch(kDemoCamera, "DemoCamera.bch");
    const uint32_t cp = cb.find(12, "DemoOpeningSeason1"), ca = cb.u32(cp + 12);
    const uint32_t q0 = cb.u32(ca), q1 = cb.u32(ca + 4), q2 = cb.u32(ca + 8);
    if (cb.u16(q0 + 4) != 40 || cb.u16(q1 + 4) != 41 || cb.u16(q2 + 4) != 47) {
        throw ImportError("Unexpected demo camera layout");
    }
    auto curveOf = [&](size_t at) {
        const Value c = curve(cb, cb.u32(at));
        Value x = Value::object();
        x["interpolation"] = c.at("interpolation");
        x["keys"] = c.at("keys");
        return x;
    };
    Value dc = Value::object();
    dc["frames"] = cb.f32(cp + 8);
    dc["position"] = Value::array();
    for (int j : {6, 7, 8}) {
        dc["position"].push(curveOf(q0 + 12 + 4 * size_t(j)));
    }
    dc["target"] = Value::array();
    for (int j = 0; j < 3; ++j) {
        dc["target"].push(curveOf(q1 + 12 + 4 * size_t(j)));
    }
    dc["fovy"] = curveOf(q2 + 12);
    scene_["demo_camera"] = dc;
    const Bch kb = in_.bch(kProtagonistAnimArchive, "KinopioAnimation.bch");
    const Bch sb = in_.bch(kOpeningAnim, "KinopioAnimationSeason1OpeningStage.bch");
    for (const char* c : {"GoalPosePairSign", "GoalPosePair", "CourseInRove"}) {
        character_["clips_curves"][c] = clipWithVisibility(kb, c);
    }
    character_["clips_curves"]["DemoOpeningSeason1"] = clipWithVisibility(sb, "DemoOpeningSeason1");
    // The companion rail guide (src/Companion.hpp).
    const Bch b = in_.bch(kCompanionArchive, "KinopicoNpc.bch");
    Value& tj = actorJson("companion");
    Value curveClips = Value::object(), curveVis = Value::object();
    for (const char* c : {"Wait", "Walk", "Turn", "ByeBye", "WaitHappy", "GoalPosePairSign", "GoalPosePair"}) {
        curveClips[c] = actorAnimation(b, c);
        curveVis[c] = visibility(b, {c});
    }
    // The NPC has no DemoOpeningSeason1; the stage archive's clip for the companion is used.
    curveClips["DemoOpeningSeason1"] = actorAnimation(sb, "KinopicoDemoOpeningSeason1");
    curveVis["DemoOpeningSeason1"] =
        visibility(sb, {"KinopicoDemoOpeningSeason1Eye", "KinopicoDemoOpeningSeason1Face",
                        "KinopicoDemoOpeningSeason1HandL", "KinopicoDemoOpeningSeason1HandR"});
    tj["curve_clips"] = curveClips;
    tj["curve_clip_visibility"] = curveVis;
    const Value& npc = goalObj.at("Links").at("PlayerNpc")[0];
    const Value& rail = npc.at("Links").at("Rail")[0];
    tj["anim_interp"] = in_.byml(kCompanionArchive, "AnimInterp.byml");
    Value guide = Value::object();
    guide["status"] = "Recovered rail guide (controller 0x4c79c0 states Wait/Walk/Turn/ByeBye, rules "
                      "0x1c7f58/0x2ad70c, rail 0x32b784); "
                      "validated per tick (evidence/companion/validation.txt).";
    guide["closed"] = rail.at("IsClosed");
    Value points = Value::array();
    for (const auto& p : rail.at("RailPoints").items()) {
        Value x = Value::object();
        x["pos"] = xyz(p.at("Translate"));
        x["handle_in"] = xyz(p.at("ControlPoints")[0]);
        x["handle_out"] = xyz(p.at("ControlPoints")[1]);
        points.push(x);
    }
    guide["points"] = points;
    guide["map_rotate_y"] = npc.at("Rotate").at("Y");
    guide["wait_ticks"] = 180;
    tj["rail_guide"] = guide;
}

// ---- export_layouts.py ----------------------------------------------------------------------

void Builder::layouts() {
    const fs::path dst = out_ / "layouts";
    std::map<std::string, Value> textures, fonts;
    auto texture = [&](const std::string& name, const std::vector<SarcMember>& files) -> Value {
        if (auto it = textures.find(name); it != textures.end()) {
            return it->second;
        }
        const Bytes& b = member(files, "timg/" + name);
        // Full stored raster; layout coordinates address it through the rotated-texture mapping (flags bit 2).
        const Flim f = decodeFlim(b);
        if (!(f.flags & 4)) {
            throw ImportError(name + ": unrotated BFLIM mapping not observed");
        }
        writeFile(dst / "textures" / (name + ".rgba"), f.rgba);
        Value t = Value::object();
        t["file"] = "layouts/textures/" + name + ".rgba";
        t["width"] = f.allocWidth;
        t["height"] = f.allocHeight;
        t["image"] = Value::list(std::vector<uint32_t>{f.width, f.height});
        t["format"] = f.format;
        t["rotated"] = true;
        t["sha256"] = sha256Hex(b);
        return textures[name] = t;
    };
    auto font = [&](const std::string& name, const std::vector<SarcMember>& files) -> Value {
        if (auto it = fonts.find(name); it != fonts.end()) {
            return it->second;
        }
        const Bytes& b = member(files, "font/" + name);
        const Font f = readFont(b);
        for (size_t i = 0; i < f.sheets.size(); ++i) {
            writeFile(dst / "fonts" / (name + "-" + std::to_string(i) + ".a8"), f.sheets[i]);
        }
        const int64_t perRow = f.sheet.at("cells_per_row").integer();
        const int64_t per = perRow * f.sheet.at("cells_per_column").integer();
        const int64_t cw = f.sheet.at("cell_width").integer(), ch = f.sheet.at("cell_height").integer();
        Value glyphs = Value::object();
        for (const auto& [code, index] : f.cmap) {
            auto w = f.widths.count(index) ? f.widths.at(index) : f.defaultWidth;
            const int64_t sheet = index / per, cell = index % per;
            Value g = Value::object();
            g["sheet"] = sheet;
            g["x"] = (cell % perRow) * (cw + 1);
            g["y"] = (cell / perRow) * (ch + 1);
            g["left"] = w.left;
            g["glyph"] = w.glyph;
            g["char"] = w.chr;
            glyphs[std::to_string(code)] = g;
        }
        Value j = Value::object();
        j["name"] = name;
        j["height"] = f.info.at("height");
        j["width"] = f.info.at("width");
        j["ascent"] = f.info.at("ascent");
        j["line_feed"] = f.info.at("line_feed");
        j["default_width"] = f.info.at("default_width");
        j["sheet"] = f.sheet;
        j["sheets"] = Value::array();
        for (size_t i = 0; i < f.sheets.size(); ++i) {
            j["sheets"].push("layouts/fonts/" + name + "-" + std::to_string(i) + ".a8");
        }
        j["glyphs"] = glyphs;
        j["sha256"] = sha256Hex(b);
        writeFile(dst / "fonts" / (name + ".json"), json::dump(j) + "\n");
        Value ref = Value::object();
        ref["file"] = "layouts/fonts/" + name + ".json";
        return fonts[name] = ref;
    };
    // Region layouts the game loads (EUen): the goal sequence and the stage-start iris.
    const auto& intro = in_.archive(kIntroLayouts);
    const auto& wipes = in_.archive(kWipeLayouts);
    struct Item {
        const char* name;
        const std::vector<SarcMember>* files;
        const char* archive;
    };
    Value exported = Value::array();
    for (const Item& item :
         {Item{"HeadCourseClear_TOP", &intro, kIntroLayouts}, Item{"WipeCircle_TOP", &intro, kIntroLayouts},
          Item{"TitleLogoDemo_TOP", &intro, kIntroLayouts}, Item{"TitleLogoParts", &intro, kIntroLayouts},
          Item{"WipeCircleWhite_TOP", &wipes, kWipeLayouts}}) {
        const std::string name = item.name;
        Value lyt = readBflyt(member(*item.files, "blyt/" + name + ".bflyt"));
        Value tex = Value::array(), fnt = Value::array();
        for (const auto& t : lyt.at("textures").items()) {
            tex.push(texture(t.str(), *item.files));
        }
        for (const auto& f : lyt.at("fonts").items()) {
            fnt.push(font(f.str(), *item.files));
        }
        lyt["texture_files"] = tex;
        lyt["font_files"] = fnt;
        Value anims = Value::object();
        const std::string prefix = "anim/" + name + "_";
        for (const auto& m : *item.files) {
            if (startsWith(m.name, prefix) && m.name.size() > 6 &&
                m.name.compare(m.name.size() - 6, 6, ".bflan") == 0) {
                Value a = readBflan(m.data);
                anims[a.at("tag").at("name").str()] = a;
            }
        }
        lyt["animations"] = anims;
        Value src = Value::object();
        src["archive"] = std::string("rom:") + item.archive;
        src["member"] = "blyt/" + name + ".bflyt";
        lyt["source"] = src;
        writeFile(dst / (name + ".json"), json::dump(lyt) + "\n");
        Value e = Value::object();
        e["name"] = name;
        e["file"] = "layouts/" + name + ".json";
        exported.push(e);
    }
    scene_["layouts"] = exported;
}

// ---- export_eft.py --------------------------------------------------------------------------

// Effect runtime heap tables, generated as the original initialisers do.
// 0x34479c: xorshift128 (seed from its literal pool 0x34493c) draws, per entry, a cube vector in
// [-1,1]^3 (table *0x4f91a4) and a unit vector (table *0x4f91a8). Returns unit table then cube table.
std::pair<Bytes, Bytes> randomTables(const Bytes& code) {
    auto word = [&](uint32_t a) {
        return u32(code, a - kCodeBase);
    };
    auto lit = [&](uint32_t a) {
        return f32(code, a - kCodeBase);
    };
    uint32_t x = word(0x34493c), z = word(0x344940), y = word(0x344944), w = word(0x344948);
    const float scale = lit(0x344950), two = lit(0x344954), one = lit(0x344958);
    auto next = [&] {
        const uint32_t t = x ^ (x << 11);
        x = y;
        y = z;
        z = w;
        w = w ^ (w >> 19) ^ t ^ (t >> 8);
        return w;
    };
    auto map = [&](uint32_t u) {
        const float f = float(u) * scale;
        return f * two - one;
    };
    Bytes unit, cube;
    for (int i = 0; i < 512; ++i) {
        uint32_t n[6];
        for (auto& v : n) {
            v = next();
        }
        appendFloats(cube, {map(n[0]), map(n[1]), map(n[2])});
        const float vx = map(n[3]), vy = map(n[4]), vz = map(n[5]);
        float len = vx * vx;
        len = len + vy * vy;
        len = len + vz * vz;
        const float inv = one / std::sqrt(len);
        appendFloats(unit, {vx * inv, vy * inv, vz * inv});
    }
    return {unit, cube};
}

// 0x347f88: 128-entry flicker table, 0.5 + 0.5 * sin(i / 128 * 2pi) through the sine table 0x499efc.
Bytes flickerTable(const Bytes& code) {
    auto lit = [&](uint32_t a) {
        return f32(code, a - kCodeBase);
    };
    const float two = lit(0x34803c), pi = lit(0x348040), toIndex = lit(0x348044), half = lit(0x34804c);
    const float inv128 = lit(0x348050), inv24 = lit(0x348054);
    Bytes out;
    for (int i = 0; i < 128; ++i) {
        float s = float(i) * inv128;
        s = s * two;
        s = s * pi;
        s = s * toIndex;
        const uint32_t r = uint32_t(int64_t(s));   // 0x2ff634: float to int64, low word
        const float frac = float(r & 0xffffff) * inv24;
        const uint32_t entry = 0x499efc + (r >> 24) * 16;
        const float v = lit(entry + 8) + lit(entry + 12) * frac;
        appendFloats(out, {half + v * half});
    }
    return out;
}

ParticleCommon exportParticleCommon(Inputs& in, const fs::path& out, const std::string& prefix) {
    const Bytes& raw = in.file(kEffectData, "EffectData.ptcl");
    const Bytes& code = in.code();
    writeFile(out / "EffectData.ptcl", raw);
    const uint32_t nameTable = u32(raw, 0x10), texTable = u32(raw, 0x14);
    auto name = [&](uint32_t off) {
        std::string s;
        for (unsigned char c : cstring(raw, nameTable + off)) {
            if (c < 0x80) {
                s += char(c);
            } else {
                s += char(0xc0 | (c >> 6));
                s += char(0x80 | (c & 63));
            }   // latin-1
        }
        return s;
    };
    // tables.bin: sin/cos (257 x 16), the eight equal-division point sets, random tables, flicker.
    Bytes tables = slice(code, 0x499efc - kCodeBase, 257 * 16);
    const int divisions[8] = {2, 3, 4, 6, 8, 12, 20, 32};
    for (int k = 0; k < 8; ++k) {
        const uint32_t ptr = u32(code, 0x4f9088 - kCodeBase + 4 * size_t(k));
        const Bytes set = slice(code, ptr - kCodeBase, 12 * size_t(divisions[k]));
        tables.insert(tables.end(), set.begin(), set.end());
    }
    const auto [unit, cube] = randomTables(code);
    const Bytes flicker = flickerTable(code);
    for (const Bytes* t : {&unit, &cube, &flicker}) {
        tables.insert(tables.end(), t->begin(), t->end());
    }
    // The second equal-division table (volume type 6, 0x348058): pointers 0x4f90a8[0..62], sets
    // of 2 to 64 points on the unit sphere.
    for (int k = 0; k < 63; ++k) {
        const uint32_t ptr = u32(code, 0x4f90a8 - kCodeBase + 4 * size_t(k));
        const Bytes set = slice(code, ptr - kCodeBase, 12 * size_t(k + 2));
        tables.insert(tables.end(), set.begin(), set.end());
    }
    writeFile(out / "tables.bin", tables);
    // Every emitter set; textures of both slots of every emitter (and child textures).
    std::vector<std::pair<std::string, std::pair<size_t, std::vector<uint32_t>>>> sets;
    for (uint32_t i = 0; i < u32(raw, 8); ++i) {
        const size_t e = 0x48 + 0x1c * size_t(i);
        std::vector<uint32_t> recs;
        for (uint32_t k = 0; k < u32(raw, e + 16); ++k) {
            recs.push_back(u32(raw, u32(raw, e + 20) + 8 * k));
        }
        const std::string n = name(u32(raw, e + 8));
        bool replaced = false;
        for (auto& s : sets) {
            if (s.first == n) {
                s.second = {i, recs};
                replaced = true;
            }
        }
        if (!replaced) {
            sets.push_back({n, {i, recs}});
        }
    }
    std::map<uint32_t, Value> textures;
    for (const auto& s : sets) {
        for (uint32_t rec : s.second.second) {
            std::vector<uint32_t> slots = {0x40, 0xbc};
            if (u32(raw, rec) == 1 && (u32(raw, rec + 0x548) & 1)) {   // child texture
                slots.push_back(0x5b0);
            }
            for (uint32_t slot : slots) {
                const uint32_t w = u16(raw, rec + slot), h = u16(raw, rec + slot + 2);
                const uint32_t fmt = u32(raw, rec + slot + 0x6c), size = u32(raw, rec + slot + 0x70),
                               off = u32(raw, rec + slot + 0x74);
                if (w == 0 || size == 0 || textures.count(off)) {
                    continue;
                }
                const Bytes payload = slice(raw, texTable + off, size);
                Bytes rgba;
                uint32_t pica;
                if (fmt == 0x12) {
                    pica = 13;
                    rgba = decodeEtc(payload, w, h, 13);
                } else if (fmt == 0x13) {
                    pica = 12;
                    rgba = decodeEtc(payload, w, h, 12);
                } else if (fmt == 0x1c) {
                    pica = 4;
                    rgba = decodeRgba4(payload, w, h);
                } else {
                    continue;
                }
                char file[32];
                std::snprintf(file, sizeof file, "tex-%x.rgba", off);
                writeFile(out / file, rgba);
                Value t = Value::object();
                t["file"] = prefix + file;
                t["width"] = w;
                t["height"] = h;
                t["format"] = pica;
                textures[off] = t;
            }
        }
    }
    // The 30 particle vertex programs (pointer table 0x4f8f54).
    Value shaders = Value::array();
    for (int k = 0; k < 30; ++k) {
        const uint32_t ptr = u32(code, 0x4f8f54 + 8 * size_t(k) - kCodeBase);
        const uint32_t next = k < 29 ? u32(code, 0x4f8f54 + 8 * size_t(k + 1) - kCodeBase) : ptr + 0x800;
        const size_t begin = ptr - kCodeBase, end = std::min<size_t>(next - kCodeBase, code.size());
        const Bytes blob = slice(code, begin, end - begin);
        if (blob.size() < 4 || std::memcmp(blob.data(), "DVLB", 4) != 0) {
            throw ImportError("Missing particle vertex program");
        }
        char file[32];
        std::snprintf(file, sizeof file, "dvlb-%02d.shbin", k);
        writeFile(out / file, blob);
        shaders.push(prefix + file);
    }
    ParticleCommon c;
    c.ptcl = prefix + "EffectData.ptcl";
    c.tables = prefix + "tables.bin";
    c.sha = sha256Hex(raw);
    Value tex = Value::object();
    for (const auto& [off, t] : textures) {
        char b[16];
        std::snprintf(b, sizeof b, "%x", off);
        tex[b] = t;
    }
    c.textures = tex;
    Value setIndex = Value::object();
    for (const auto& s : sets) {
        setIndex[s.first] = s.second.first;
    }
    c.sets = setIndex;
    c.shaders = shaders;
    return c;
}

void Builder::particleData() {
    const fs::path out = out_ / "effects";
    const ParticleCommon common = exportParticleCommon(in_, out, "effects/");
    // EffectYaml triggers of the opening's actors.
    Value triggers = Value::object();
    for (const char* actor : {"GoalItem", "GoalItemCrown", "Kinopio", "KinopioNpc", "KinopicoNpc", "UIHeadCourseClear",
                              "BossBird", "Bird", "EffectObjWindGrass"}) {
        triggers[actor] = in_.byml(kEffectYaml, std::string(actor) + ".byml").at("EffectList");
    }
    Value objects = Value::array();
    for (const auto& o : map().at("ObjectList").items()) {
        const Value* unit = o.find("UnitConfigName");
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
        objects.push(x);
    }
    // Stage effect colours (3DSEft.byml StageEffectColor; a missing colour leaves the defaults).
    const Value design = in_.byml(kDesign, "3DSEft.byml");
    const Value* section = design.find("StageEffectColor");
    Value stageColor = Value::array();
    bool defaults = false;
    for (int i = 0; i < 2; ++i) {
        Value c = Value::array();
        for (const char* ch : {"Red", "Green", "Blue", "Alpha"}) {
            const std::string key = "Color" + std::to_string(i) + "_" + ch;
            const Value* v = section ? section->find(key) : nullptr;
            c.push(v ? *v : Value(std::string(ch) == "Red" ? -1.0 : 0.0));
        }
        if (c[0].num() < 0) {
            defaults = true;
        }
        stageColor.push(c);
    }
    if (defaults) {
        stageColor = Value::array();
        for (int i = 0; i < 2; ++i) {
            stageColor.push(doubles({1.0, 1.0, 1.0, 1.0}));
        }
    }
    Value floorMaterials = Value::array();
    const Value attributes = in_.byml(kTerrain, "Season1OpeningStepAAttribute.byml");
    for (const auto& a : attributes.items()) {
        floorMaterials.push(a.has("MaterialCode") ? a.at("MaterialCode") : Value(""));
    }
    Value meta = Value::object();
    meta["ptcl"] = common.ptcl;
    meta["tables"] = common.tables;
    meta["objects"] = objects;
    meta["stage_color"] = stageColor;
    meta["floor_materials"] = floorMaterials;
    Value xs = Value::array();
    for (uint32_t w : observed::kEftCheckpointXorshift) {
        char b[16];
        std::snprintf(b, sizeof b, "%08x", w);
        xs.push(b);
    }
    meta["checkpoint_xorshift"] = xs;
    meta["ptcl_sha256"] = common.sha;
    meta["textures"] = common.textures;
    meta["sets"] = common.sets;
    meta["triggers"] = triggers;
    meta["shaders"] = common.shaders;
    meta["status"] = "Effect runtime data: ROM PTCL + code.bin tables (random and flicker tables generated as the "
                     "original initialisers 0x34479c/0x347f88 do).";
    writeFile(out / "effects.json", json::dump(meta) + "\n");
}

// ---- export_sound.py ------------------------------------------------------------------------

void Builder::sound() {
    const fs::path out = out_ / "sound";
    for (const char* f : {"sound_data.bcsar", "SNDTBLS.DAT", "SFXSCRIPTFILES.DAT", "STREAMFILEINFO.DAT",
                          "SND3DLIST.DAT", "ADSRINFO.DAT"}) {
        writeFile(out / f, in_.romfs(std::string("SoundData/") + f));
    }
    const char* const streams[] = {"Stm_Kp_bgm_Tutorial", "Ws_Kp_bgm_jg_Clear01", "Stm_Kp_bgm_Demo_Op01"};
    Value streamList = Value::array();
    for (const char* s : streams) {
        const std::string file = std::string(s) + ".dspadpcm.bcstm";
        writeFile(out / "stream" / file, in_.romfs("SoundData/stream/" + file));
        streamList.push(s);
    }
    const char* const owners[] = {"PlayerBase", "PlayerActor", "Kinopico",      "KinopicoNpc",
                                  "Bird",       "GoalItem",    "WipeClearDemo", "Camera"};
    const auto& db = in_.archive("SoundData/SeDataBase.szs");
    auto doc = [&](const std::string& n) {
        return byml(member(db, n + ".byml"));
    };
    Value ownerDocs = Value::object();
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
    for (const char* o : owners) {
        ownerDocs[o] = doc(o);
    }
    walk(ownerDocs);
    Value props = Value::object();
    const Value propertyList = doc("SePropertyList");
    for (const auto& p : propertyList.items()) {
        if (names.count(p.at("Name").str())) {
            props[p.at("Name").str()] = p;
        }
    }
    const Value stageList = doc("SeStageInfoList");
    const Value& stage = findBy(stageList, [](const Value& e) { return e.at("Name").str() == "Season1OpeningStage"; });
    Value sedb = Value::object();
    sedb["owners"] = ownerDocs;
    sedb["properties"] = props;
    sedb["stage"] = stage;
    sedb["streams"] = streamList;
    writeFile(out / "sedb.json", json::dump(sedb, 1));
    Value checkpoint = Value::object();
    checkpoint["game_rng"] =
        Value::list(std::vector<uint32_t>(observed::kGameRngCheckpoint.begin(), observed::kGameRngCheckpoint.end()));
    checkpoint["sound_lcg"] = observed::kSoundLcgCheckpoint;
    writeFile(out / "checkpoint.json", json::dump(checkpoint));
}

void Builder::finish() {
    writeFile(out_ / "vertices.bin", vertices_);
    writeFile(out_ / "tangents.bin", tangents_);
    writeFile(out_ / "collision.bin", collision_);
    writeFile(out_ / "shadow-casters.bin", casters_);
    writeFile(out_ / "lighting-luts.bin", lutAtlas_);
    writeFile(out_ / "character-skin.bin", characterSkin_);
    writeFile(out_ / "shaders" / "terrain.vert", deviceShader(vertexShader_, ShaderStage::Vertex));
    for (const auto& [i, text] : shaders_) {
        writeFile(out_ / "shaders" / (std::to_string(i) + ".frag"), deviceShader(text, ShaderStage::Fragment));
    }
    for (const auto& [n, text] : namedShaders_) {
        writeFile(out_ / "shaders" / n, deviceShader(text, ShaderStage::Fragment));
    }
    writeFile(out_ / "character.json", json::dump(character_) + "\n");
    writeFile(out_ / "player.json", json::dump(player_, 1) + "\n");
    for (const auto& [name, actor] : actors_) {
        writeFile(out_ / "actors" / (name + ".json"), json::dump(actor) + "\n");
        writeFile(out_ / "actors" / (name + "-skin.bin"), actorSkins_.at(name));
    }
    scene_["sources"] = in_.sources();
    writeFile(out_ / "scene.json", json::dump(scene_, 2) + "\n");
}

void Builder::run() {
    fs::create_directories(out_ / "textures");
    step(0.05, "Terrain");
    terrain();
    step(0.15, "Sky and camera");
    presentation();
    step(0.20, "Shadows");
    shadows();
    step(0.25, "Player model");
    character();
    step(0.35, "Lighting");
    lighting();
    step(0.40, "Effects");
    effects();
    step(0.45, "Actors");
    actors();
    step(0.60, "Player");
    player();
    step(0.65, "Opening");
    opening();
    step(0.70, "Layouts");
    layouts();
    step(0.75, "Particles");
    particleData();
    step(0.85, "Sound");
    sound();
    step(0.95, "Writing");
    finish();
}

}   // namespace

ParticleCommon writeParticleCommon(Inputs& in, const fs::path& dir, const std::string& prefix) {
    return exportParticleCommon(in, dir, prefix);
}

void exportPackage(Inputs& in, const fs::path& out, const ProgressFn& progress) {
    Builder(in, out, progress).run();
}

}   // namespace captain_toad
