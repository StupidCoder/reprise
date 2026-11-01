// =============================================================================================
// LightService.hpp - scene lights for the fragment-lighting shaders
// =============================================================================================
//
// The console's GPU (PICA200) has fixed-function per-fragment lighting with up to eight lights.
// Each material lists which lights affect it; this service resolves those lists, multiplies
// material and light colours the way the game does, and uploads everything as shader uniforms
// (the PICA lighting itself is emulated in the shaders, see PicaShader.hpp).
//
// The lights of the opening: a main and a second directional light (sun and fill), an ambient
// term, the headlamp on the protagonist's head (a spot light that only reaches as far as the
// next wall), and the glow of the goal star.
//
// Recovered / interpreted parts (see docs/lighting-fidelity.md):
//   * material x light colour products truncate with >> 8 (ARM 0x4017d4);
//   * global ambient = emission + material ambient x AmbientLight;
//   * reflection materials (flag 0x10) use the light's raw specular1 colour;
//   * runtime light lists observed in GPU traces: terrain = main, second, headlamp, star;
//     character = main, headlamp. Shadows apply to the two directional lights;
//   * distance attenuation maps distance d to clamp((d - start) / (end - start));
//   * stage light sets (generic packages): a set's ambient lights (type 2) sum to the scene
//     ambient of the materials using the set; its point lights (type 10, torches) are positional
//     lights with distance attenuation. Hypothesis (not traced): the set order is the PICA slot
//     order and at most four lights apply.
//   * light areas (LightAreas.byml + the map's LightArea boxes; manager 0x22d77c, entry 0x22d248,
//     masks 0x22bc60): each area gets a light set from its Lights (named lights, or the scene set
//     of a lightSetIndex entry, 0x236ddc) and a mask of scene light sets (affectedLightIndex;
//     all sets when absent, 0x22bea0). An actor is lit by the set of the area at its position
//     (0x23612c -> 0x22bec0; the scene's set otherwise). A dynamic light (the headlamp) joins the
//     scene light sets named by the masks of the areas holding its check points (0x22c628:
//     affectedLightIndex, plus extraAffectedLightIndex with useExtraLights; 0x293698 adds it to
//     each set). The area at a point is the enabled one of highest Priority containing it, the
//     later one on a tie (0x2d8540); the box test ignores the area's rotation (0x297768).
// Native approximations: the headlamp beam length (a ray cast instead of the unrecovered
// collision check) and the static star light (no bob animation).
// =============================================================================================
#pragma once
#include "data/Package.hpp"
#include "gpu/Device.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <map>

// One light as exported (colours are bytes, 0..255).
struct LightRecord {
    std::array<int, 3> ambient{};
    std::array<int, 3> diffuse{};
    std::array<int, 3> specular0{};
    std::array<int, 3> specular1{};
    std::array<double, 3> toLight{0, 1, 0};   // world direction toward a directional light
    bool directional = true;
    bool twoSided = false;
    bool distance = false;   // distance attenuation
    bool spot = false;
    bool spotAbsolute = false;
    double start = 0;   // attenuation start / end distance
    double end = 1;
    double distanceRow = 0;   // lookup-table rows in the LUT texture
    double spotRow = 0;
    double spotScale = 1;
};

// The lighting part of one material.
struct MaterialLighting {
    int kind = 0;   // 0 unlit / other, 1 terrain, 2 character
    int flags = 0;
    std::array<int, 3> emission{};
    std::array<int, 3> ambient{};
    std::array<int, 3> diffuse{};
    std::array<int, 3> specular0{};
    std::array<int, 3> specular1{};
    std::vector<std::string> lights;   // directional scene-set lights then local lights, in PICA slot order
    std::vector<std::string> locals;   // the local lights (attached to actors) of the material
    int lightSet = 0;                  // the material's scene light set
    bool setAmbient = false;           // the light set has ambient lights (type 2): their sum is the scene ambient
    std::array<int, 3> ambientLight{};
};

// A light as placed for the current frame.
struct ActiveLight {
    const LightRecord* record;
    std::array<double, 3> world;   // position (local lights)
    std::array<double, 3> beam;    // spot direction
    double end;                    // attenuation end
};

static std::array<int, 3> colorBytes(captain_toad::data::JsonView a) {
    return {a[0].i(), a[1].i(), a[2].i()};
}

// Material x light colour, truncated as the game does (ARM 0x4017d4).
static std::array<int, 3> lightProduct(const std::array<int, 3>& a, const std::array<int, 3>& b) {
    return {(a[0] * b[0]) >> 8, (a[1] * b[1]) >> 8, (a[2] * b[2]) >> 8};
}

// A light area: its box (world, axis-aligned), priority, light set and affected scene sets.
struct LightArea {
    std::string id;
    std::array<double, 3> min{}, max{};
    int priority = 0;
    uint32_t affected = ~0u;   // affectedLightIndex as a set mask (all sets when absent)
    uint32_t extra = 0;        // extraAffectedLightIndex
    std::vector<std::string> lights;
    bool setAmbient = false;
    std::array<int, 3> ambientLight{};
    bool contains(const std::array<double, 3>& p) const {
        for (int k = 0; k < 3; k++) {
            if (p[k] <= min[k] || p[k] >= max[k]) {
                return false;
            }
        }
        return true;
    }
};

struct LightService {
    std::map<std::string, LightRecord> records;
    std::map<std::string, int> types;                              // scene lights: PICA light type
    std::map<std::string, std::array<double, 3>> pointPositions;   // scene point lights: world position
    std::vector<MaterialLighting> materials;
    std::array<int, 3> sceneAmbient{};
    std::array<double, 3> starPosition{};
    std::array<double, 3> headlampPosition{};
    std::array<double, 3> headlampBeam{0, 0, 1};
    double headlampEnd = 1500;
    bool headlampEnabled = true;
    bool starEnabled = true;
    std::vector<float> collision;   // world triangles for the beam-length approximation
    bool generic = false;           // a stage package's lighting
    std::vector<LightArea> lightAreas;
    uint32_t headlampSets = ~0u;                         // the scene light sets the headlamp joins (stage packages)
    std::vector<std::array<double, 3>> headlampPoints;   // its check points in the joint's frame

    // The light area at a point (0x2d8540), or null.
    const LightArea* areaAt(const std::array<double, 3>& p) const {
        const LightArea* best = nullptr;
        for (const LightArea& a : lightAreas) {
            if ((!best || best->priority <= a.priority) && a.contains(p)) {
                best = &a;
            }
        }
        return best;
    }

    // The headlamp's scene light sets from its check points (world positions; 0x22c628 with
    // useExtraLights). Without light areas the headlamp joins every set.
    void placeHeadlamp(const std::vector<std::array<double, 3>>& points) {
        if (lightAreas.empty()) {
            headlampSets = ~0u;
            return;
        }
        headlampSets = 0;
        for (const auto& p : points) {
            if (const LightArea* a = areaAt(p)) {
                headlampSets |= a->affected | a->extra;
            }
        }
    }

    // The lights of a set's names: directional and point lights take slots, an ambient light
    // (type 2) is the set's scene ambient.
    void addSetLights(captain_toad::data::JsonView names, std::vector<std::string>& lights, bool& setAmbient,
                      std::array<int, 3>& ambientLight) const {
        for (captain_toad::data::JsonView n : names.items()) {
            if (records.count(n.str()) && records.at(n.str()).directional && !n.is("AmbientLight")) {
                lights.push_back(n.str());
            } else if (pointPositions.count(n.str())) {
                lights.push_back(n.str());
            } else if (generic && types.count(n.str()) && types.at(n.str()) == 2) {
                setAmbient = true;
                const auto& a = records.at(n.str()).ambient;
                for (int k = 0; k < 3; k++) {
                    ambientLight[k] = std::min(255, ambientLight[k] + a[k]);
                }
            }
        }
    }

    // The course's light areas: the map's LightArea boxes (AreaCubeBase: x, z in +-500 scale, y in
    // [0, 1000 scale] from the translation) joined with LightAreas.byml by areaName.
    void loadAreas(captain_toad::data::JsonView lighting, captain_toad::data::JsonView stageAreas) {
        lightAreas.clear();
        auto mask = [](captain_toad::data::JsonView v, uint32_t none) {
            if (!v) {
                return none;
            }
            if (!v.isArray()) {
                return uint32_t(1) << (v.i() & 31);
            }
            uint32_t m = 0;
            for (captain_toad::data::JsonView e : v.items()) {
                if (e["id"]) {
                    m |= uint32_t(1) << (e["id"].i() & 31);
                }
            }
            return m;
        };
        for (captain_toad::data::JsonView a : stageAreas.items()) {
            if (!a["kind"].is("LightArea")) {
                continue;
            }
            LightArea la;
            la.id = a["id"].str();
            la.priority = a["params"]["Priority"].i();
            captain_toad::data::JsonView m = a["matrix"];
            const double t[3] = {m[3].d(), m[7].d(), m[11].d()};
            double scale[3];
            for (int k = 0; k < 3; k++) {
                scale[k] = std::sqrt(m[k].d() * m[k].d() + m[4 + k].d() * m[4 + k].d() + m[8 + k].d() * m[8 + k].d());
            }
            la.min = {t[0] - 500 * scale[0], t[1], t[2] - 500 * scale[2]};
            la.max = {t[0] + 500 * scale[0], t[1] + 1000 * scale[1], t[2] + 500 * scale[2]};
            for (captain_toad::data::JsonView e : lighting["light_areas"]["LightAreas"].items()) {
                if (!e["areaName"].isString() || e["areaName"].str() != la.id) {
                    continue;
                }
                la.affected = mask(e["affectedLightIndex"], ~0u);
                la.extra = mask(e["extraAffectedLightIndex"], 0);
                for (captain_toad::data::JsonView l : e["Lights"].items()) {
                    if (l["lightName"].isString()) {
                        captain_toad::data::JsonView one = l["lightName"];
                        if (records.count(one.str()) && records.at(one.str()).directional) {
                            la.lights.push_back(one.str());
                        } else if (pointPositions.count(one.str())) {
                            la.lights.push_back(one.str());
                        } else if (types.count(one.str()) && types.at(one.str()) == 2) {
                            la.setAmbient = true;
                            const auto& c = records.at(one.str()).ambient;
                            for (int k = 0; k < 3; k++) {
                                la.ambientLight[k] = std::min(255, la.ambientLight[k] + c[k]);
                            }
                        }
                    } else if (l["lightSetIndex"]) {
                        addSetLights(lighting["scene_sets"][std::to_string(l["lightSetIndex"].i())], la.lights,
                                     la.setAmbient, la.ambientLight);
                    }
                }
            }
            lightAreas.push_back(la);
        }
    }

    void load(captain_toad::data::JsonView lighting, captain_toad::data::JsonView sceneMaterials) {
        // Scene (directional and ambient) lights.
        captain_toad::data::JsonView scene = lighting["scene_lights"];
        for (const std::string& name : scene.keys()) {
            captain_toad::data::JsonView l = scene[name];
            LightRecord r;
            r.ambient = colorBytes(l["ambient"]);
            r.diffuse = colorBytes(l["diffuse"]);
            r.specular0 = colorBytes(l["specular0"]);
            r.specular1 = colorBytes(l["specular1"]);
            r.directional = l["type"].i() == 9;
            // Records store the light's travel direction; PICA wants the vector toward the light.
            for (int k = 0; k < 3; k++) {
                r.toLight[k] = -l["direction"][k].d();
            }
            // Point lights (type 10, e.g. RevolverRuinsStage's torches): placed at their translation,
            // with distance attenuation through the shared set's LUT named by sampler 1.
            if (l["type"].i() == 10) {
                r.twoSided = l["two_sided"].b();
                r.distance = l["distance"].b();
                r.start = l["attenuation_start"].d();
                r.end = l["attenuation_end"].d();
                const std::string lut = l["samplers"][1].isString() ? l["samplers"][1].str() : std::string();
                size_t row = 0;
                for (captain_toad::data::JsonView e : lighting["luts"].items()) {
                    if (e["name"].isString() && e["name"].str() == lut) {
                        r.distanceRow = double(row);
                        break;
                    }
                    row++;
                }
                for (int k = 0; k < 3; k++) {
                    pointPositions[name][k] = l["translation"][k].d();
                }
            }
            types[name] = l["type"].i();
            records[name] = r;
        }
        // The opening's scene has a separate ambient light; other stages give each directional
        // light its own ambient colour (package "generic" lighting).
        if (!lighting["generic"].b() &&
            (!records.count("AmbientLight") || !records.count("mainLight") || !records.count("secondLight"))) {
            throw std::runtime_error("Missing scene lights");
        }
        if (records.count("AmbientLight")) {
            sceneAmbient = records["AmbientLight"].ambient;
        }

        // Local lights (point and spot lights attached to actors).
        captain_toad::data::JsonView locals = lighting["local_lights"];
        for (const std::string& name : locals.keys()) {
            captain_toad::data::JsonView l = locals[name];
            LightRecord r;
            r.ambient = colorBytes(l["ambient"]);
            r.diffuse = colorBytes(l["diffuse"]);
            r.specular0 = colorBytes(l["specular0"]);
            r.specular1 = colorBytes(l["specular1"]);
            r.directional = false;
            r.twoSided = l["two_sided"].b();
            r.distance = l["distance"].b();
            r.start = l["attenuation_start"].d();
            r.end = l["attenuation_end"].d();
            if (!l["distance_row"].isNull()) {
                r.distanceRow = l["distance_row"].d();
            }
            r.spot = l["type"].i() == 11 && !l["spot_row"].isNull();
            if (r.spot) {
                r.spotRow = l["spot_row"].d();
                r.spotAbsolute = l["spot_absolute"].b();
                const double scales[] = {1, 2, 4, 8, 1, 1, .25, .5};
                r.spotScale = scales[l["angle_scale"].i() & 7];
                if (l["angle_input"].i() != 4) {
                    throw std::runtime_error("Unsupported spot LUT input");
                }
            }
            for (int k = 0; k < 3; k++) {
                r.toLight[k] = l["direction"][k].d();   // local beam axis (joint space)
            }
            records[name] = r;
            if (name == "headlamp") {
                headlampPoints.clear();
                for (captain_toad::data::JsonView c : l["check_points"].items()) {
                    headlampPoints.push_back({c[0].d(), c[1].d(), c[2].d()});
                }
            }
            if (name == "star") {
                for (int k = 0; k < 3; k++) {
                    starPosition[k] = l["attach"]["position"][k].d();
                }
            }
        }

        // Per-material light lists and colours.
        generic = lighting["generic"].b();
        captain_toad::data::JsonView sets = lighting["scene_sets"];
        for (captain_toad::data::JsonView m : sceneMaterials.items()) {
            MaterialLighting ml;
            captain_toad::data::JsonView p = m["pica"];
            if (p) {
                ml.kind = p["kind"].is("terrain") ? 1 : 2;
                ml.flags = p["flags"].i();
                captain_toad::data::JsonView c = p["colors"];
                ml.lightSet = p["light_set"].i();
                addSetLights(sets[std::to_string(ml.lightSet)], ml.lights, ml.setAmbient, ml.ambientLight);
                for (captain_toad::data::JsonView n : p["locals"].items()) {
                    ml.lights.push_back(n.str());
                    ml.locals.push_back(n.str());
                }
                // Stage packages (not yet traced): at most the PICA's first four slots, no lighting
                // without lights.
                if (lighting["generic"].b() && ml.lights.size() > 4) {
                    ml.lights.resize(4);
                }
                if (lighting["generic"].b() && ml.lights.empty()) {
                    ml.kind = 0;
                }
                if (ml.kind && (ml.lights.empty() || ml.lights.size() > 4)) {
                    throw std::runtime_error("Invalid material light list");
                }
                ml.emission = colorBytes(c["emission"]);
                ml.ambient = colorBytes(c["ambient"]);
                ml.diffuse = colorBytes(c["diffuse"]);
                ml.specular0 = colorBytes(c["specular0"]);
                ml.specular1 = colorBytes(c["specular1"]);
            }
            materials.push_back(ml);
        }
    }

    // Native approximation of the headlamp's collision-limited beam (InitLightActor
    // checkCollision segment z = -40..1500): the nearest collision hit along the beam
    // (Moeller-Trumbore ray / triangle test).
    double beamLength(const std::array<double, 3>& origin, const std::array<double, 3>& dir, double maximum) const {
        double best = maximum;
        for (size_t i = 0; i + 8 < collision.size(); i += 9) {
            const float* t = &collision[i];
            double e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
            double e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
            double p[3] = {dir[1] * e2[2] - dir[2] * e2[1], dir[2] * e2[0] - dir[0] * e2[2],
                           dir[0] * e2[1] - dir[1] * e2[0]};
            double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
            if (fabs(det) < 1e-9) {
                continue;
            }
            double s[3] = {origin[0] - t[0], origin[1] - t[1], origin[2] - t[2]};
            double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
            if (u < 0 || u > 1) {
                continue;
            }
            double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
            double v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) / det;
            if (v < 0 || u + v > 1) {
                continue;
            }
            double d = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
            if (d > 1 && d < best) {
                best = d;
            }
        }
        return best;
    }

    // Set the per-draw lighting uniforms for one material. `view` is the column-major 4x4
    // world -> view matrix.
    // Material colours a material animation sets this draw (emission, ambient, diffuse, specular0,
    // specular1; 8-bit), used instead of the material's own.
    struct ColorOverride {
        bool set[5] = {false, false, false, false, false};
        std::array<int, 3> color[5];
    };

    // `area`: the light area of the drawn actor (its light set replaces the scene set of the
    // actor's materials; stage packages).
    void apply(captain_toad::gpu::Program& program, int materialIndex, const double* view,
               const ColorOverride* colors = nullptr, const LightArea* area = nullptr) const {
        MaterialLighting m = materials.at(materialIndex);
        if (!m.kind) {
            return;
        }
        if (area && generic && m.kind == 2 && !area->lights.empty()) {
            // Hypothesis: the actor materials (kind character, set 0) take the area's set; the
            // local lights stay.
            m.lights = area->lights;
            m.lights.insert(m.lights.end(), m.locals.begin(), m.locals.end());
            if (m.lights.size() > 4) {
                m.lights.resize(4);
            }
            m.setAmbient = area->setAmbient;
            m.ambientLight = area->ambientLight;
        }
        if (colors) {
            std::array<int, 3>* fields[5] = {&m.emission, &m.ambient, &m.diffuse, &m.specular0, &m.specular1};
            for (int k = 0; k < 5; k++) {
                if (colors->set[k]) {
                    *fields[k] = colors->color[k];
                }
            }
        }

        // Lights of this material that are switched on.
        std::vector<ActiveLight> list;
        for (const auto& name : m.lights) {
            if (name == "headlamp") {
                // Scene materials see the headlamp only in the sets it joined (0x293698).
                const bool joined = !generic || m.kind != 1 || ((headlampSets >> (m.lightSet & 31)) & 1);
                if (headlampEnabled && joined) {
                    list.push_back({&records.at(name), headlampPosition, headlampBeam, headlampEnd});
                }
            } else if (name == "star") {
                if (starEnabled) {
                    list.push_back({&records.at(name), starPosition, {}, records.at(name).end});
                }
            } else if (auto pp = pointPositions.find(name); pp != pointPositions.end()) {
                list.push_back({&records.at(name), pp->second, {}, records.at(name).end});
            } else {
                list.push_back({&records.at(name), {}, {}, 0});
            }
        }

        float vectors[12] = {};
        float spotDirs[12] = {};
        float amb[12] = {};
        float dif[12] = {};
        float s0[12] = {};
        float s1[12] = {};
        float flags[16] = {};
        float dist[16] = {};
        float spot[16] = {};
        auto rotate = [&](const std::array<double, 3>& v, bool position) {
            std::array<double, 3> r;
            for (int i = 0; i < 3; i++) {
                r[i] = view[i] * v[0] + view[4 + i] * v[1] + view[8 + i] * v[2] + (position ? view[12 + i] : 0);
            }
            return r;
        };
        for (size_t i = 0; i < list.size(); i++) {
            const auto& a = list[i];
            const auto& r = *a.record;

            // Light vector in view space: a direction for directional lights, a position otherwise.
            std::array<double, 3> vec = r.directional ? rotate(r.toLight, false) : rotate(a.world, true);
            if (r.directional) {
                double n = sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
                for (auto& x : vec) {
                    x /= n;
                }
            }

            auto ambient = lightProduct(m.ambient, r.ambient);
            auto diffuse = lightProduct(m.diffuse, r.diffuse);
            auto spec0 = lightProduct(m.specular0, r.specular0);
            auto spec1 = (m.flags & 16) ? r.specular1 : lightProduct(m.specular1, r.specular1);
            for (int k = 0; k < 3; k++) {
                vectors[i * 3 + k] = vec[k];
                amb[i * 3 + k] = ambient[k] / 255.f;
                dif[i * 3 + k] = diffuse[k] / 255.f;
                s0[i * 3 + k] = spec0[k] / 255.f;
                s1[i * 3 + k] = spec1[k] / 255.f;
            }
            flags[i * 4] = r.directional;
            flags[i * 4 + 1] = r.twoSided;
            flags[i * 4 + 2] = r.directional;   // shadows on directional lights (traced config1)

            if (r.distance) {
                double end = a.end > r.start ? a.end : r.start + 1;
                dist[i * 4] = 1;
                dist[i * 4 + 1] = 1 / (end - r.start);
                dist[i * 4 + 2] = -r.start / (end - r.start);
                dist[i * 4 + 3] = r.distanceRow;
            }
            if (r.spot) {
                auto b = rotate(a.beam, false);
                double n = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
                for (int k = 0; k < 3; k++) {
                    spotDirs[i * 3 + k] = -b[k] / n;   // the register holds the vector opposing the beam
                }
                spot[i * 4] = 1;
                spot[i * 4 + 1] = r.spotRow;
                spot[i * 4 + 2] = r.spotAbsolute;
                spot[i * 4 + 3] = r.spotScale;
            }
        }

        std::array<int, 3> global;
        auto globalAmbient = lightProduct(m.ambient, m.setAmbient ? m.ambientLight : sceneAmbient);
        for (int k = 0; k < 3; k++) {
            global[k] = std::min(255, m.emission[k] + globalAmbient[k]);
        }

        program.set("lightCount", int(list.size()));
        program.setFloats("lightVector", vectors, 12);
        program.setFloats("lightSpotDirection", spotDirs, 12);
        program.setFloats("lightAmbient", amb, 12);
        program.setFloats("lightDiffuse", dif, 12);
        program.setFloats("lightSpecular0", s0, 12);
        program.setFloats("lightSpecular1", s1, 12);
        program.setFloats("lightFlags", flags, 16);
        program.setFloats("lightDistance", dist, 16);
        program.setFloats("lightSpot", spot, 16);
        program.set("globalAmbient", global[0] / 255.f, global[1] / 255.f, global[2] / 255.f);
    }
};
