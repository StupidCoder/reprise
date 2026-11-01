// =============================================================================================
// CharacterAnimation.hpp - skeletal animation and skinning of models
// =============================================================================================
//
// Every animated model in the opening (the protagonist, the companion, birds, grass, the goal
// star) is a skinned mesh: a skeleton of bones, each with an animated scale / rotation /
// translation, and vertices that follow one or more bones with weights. This service evaluates
// the bone transforms for a frame and writes the skinned vertices into the scene's vertex
// buffer.
//
// Animation sources, from simplest to most driven:
//   * the default clip exported for an actor (curves or baked per-frame matrices), looping;
//   * named baked clips or curve clips that a behaviour selects, with the frame chosen by the
//     behaviour (drivenFrame);
//   * a full local pose supplied by the player runtime (PlayerRuntime.hpp), bone by bone.
// No state machine, movement or camera-dependent game decision lives here: the behaviours decide,
// this file only presents.
// =============================================================================================
#pragma once
#include "render/SceneTypes.hpp"
#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include "game/Hermite.hpp"
#include "data/Package.hpp"
#include "render/Parallel.hpp"

// Row-major 3x4 affine matrix (rotation / scale and translation in [3], [7], [11]).
struct Affine {
    std::array<float, 12> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

    static Affine multiply(const Affine& a, const Affine& b) {
        Affine c;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                c.m[i * 4 + j] = j == 3 ? a.m[i * 4 + 3] : 0;
                for (int k = 0; k < 3; k++) {
                    c.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
                }
            }
        }
        return c;
    }

    // Scale, then XYZ Euler rotation (radians), then translation.
    static Affine trs(const float* s, const float* r, const float* t) {
        float x = cos(r[0]);
        float X = sin(r[0]);
        float y = cos(r[1]);
        float Y = sin(r[1]);
        float z = cos(r[2]);
        float Z = sin(r[2]);
        Affine a;
        a.m = {y * z * s[0],
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
        return a;
    }

    // Transform a point (position = true) or a direction.
    std::array<float, 3> apply(const float* p, bool position) const {
        std::array<float, 3> q{};
        for (int i = 0; i < 3; i++) {
            q[i] = position ? m[i * 4 + 3] : 0;
            for (int k = 0; k < 3; k++) {
                q[i] += m[i * 4 + k] * p[k];
            }
        }
        return q;
    }
};

// One key of an animation curve: frame, value and Hermite slopes (into and out of the key; the
// float key format 0x45b428 stores both, the quantised ones one).
struct AnimationKey {
    float frame;
    float value;
    float slope;
    float slopeOut;
};

// One animated value (for example the X rotation of a bone).
struct AnimationChannel {
    bool present = false;
    bool constant = false;
    float value = 0;
    std::vector<AnimationKey> keys;
    int interpolation = 2;   // H3D: 0 step, 1 linear, 2 hermite

    // Value at `frame`; `fallback` when the channel is absent. Clamped outside the keys.
    float evaluate(float frame, float fallback) const {
        if (!present) {
            return fallback;
        }
        if (constant) {
            return value;
        }
        if (frame <= keys.front().frame) {
            return keys.front().value;
        }
        if (frame >= keys.back().frame) {
            return keys.back().value;
        }
        for (size_t i = 1; i < keys.size(); i++) {
            if (frame < keys[i].frame) {
                auto a = keys[i - 1];
                auto b = keys[i];
                if (interpolation == 0) {
                    return a.value;
                }
                if (interpolation == 1) {
                    return a.value + (b.value - a.value) * (frame - a.frame) / (b.frame - a.frame);
                }
                // 0x45b428: the earlier key's out-slope, the later key's in-slope.
                return recoveredHermite(a.value, a.slopeOut, b.value, b.slope, frame - a.frame, b.frame - a.frame);
            }
        }
        throw std::runtime_error("Animation key interval");
    }
};

// An animation channel from the package: missing or null is an absent channel; otherwise a
// constant, or keys of (frame, value, slope) with their interpolation (Hermite unless stated).
inline AnimationChannel readChannel(captain_toad::data::JsonView c) {
    AnimationChannel a;
    if (!c || c.isNull()) {
        return a;
    }
    a.present = true;
    if (c["interpolation"]) {
        a.interpolation = c["interpolation"].i();
    }
    if (c["constant"]) {
        a.constant = true;
        a.value = c["constant"].f();
        return a;
    }
    // Key frames count from the curve's start: the evaluator subtracts it (curve +0) before the
    // key lookup (0x25794c).
    const float start = c["start"] ? c["start"].f() : 0.0f;
    for (captain_toad::data::JsonView k : c["keys"].items()) {
        const float in = k[2].f();
        a.keys.push_back({start + k[0].f(), k[1].f(), in, k.count() > 3 ? k[3].f() : in});
    }
    return a;
}

// The nine channels of one bone (scale xyz, rotation xyz, translation xyz) from its track list.
inline std::array<AnimationChannel, 9> readBoneChannels(captain_toad::data::JsonView tracks) {
    std::array<AnimationChannel, 9> channels{};
    if (tracks) {
        for (int i = 0; i < 9; i++) {
            channels[i] = readChannel(tracks[i]);
        }
    }
    return channels;
}

struct AnimationBone {
    int parent;
    bool segmentScaleCompensate = false;        // H3D bone flag bit 22: the parent's scale is not inherited
    float retargetSourceLength = -1;            // the clip skeleton's translation length (< 0: none)
    float retargetFormSourceLength = -1;        // the same for the form's own clips (kind 2)
    float defaults[9];                          // scale xyz, rotation xyz, translation xyz
    Affine inverse;                             // inverse bind matrix
    std::array<AnimationChannel, 9> channels;   // same order as defaults
    int billboard = 0;                          // H3D billboard mode
};

// Camera rotation (world axes of the view's x / y / z) for billboard bones; set before update().
inline std::array<float, 9> billboardCamera{1, 0, 0, 0, 1, 0, 0, 0, 1};

// A skinned vertex as exported: bind-pose position, normal and tangent, up to four bones.
struct SkinVertex {
    float p[3];
    float n[3];
    float t[3];
    uint32_t bones[4];
    float weights[4];
    uint32_t mode;   // 1: smooth skinning (bone * inverse bind), otherwise rigid (bone only)
};
static_assert(sizeof(SkinVertex) == 72);

struct CharacterAnimation {
    std::vector<AnimationBone> bones;
    std::vector<SkinVertex> skin;
    std::vector<std::string> boneNames;
    std::vector<Affine> pose;                                    // world-space joint transforms of the last update
    std::vector<std::pair<int, std::vector<bool>>> visibility;   // mesh index -> visibility per frame
    // Standalone visibility animations (stage objects): the meshes they cover follow the active one.
    std::map<std::string, std::vector<std::pair<int, std::vector<bool>>>> overlayVisibility;
    std::string overlay;
    float overlayFrame = 0;
    void setOverlay(const std::string& name, float frame) {
        if (name != overlay || frame != overlayFrame) {
            overlay = name;
            overlayFrame = frame;
            lastFrame = -1;
        }
    }
    Affine placement;   // model -> world
    int first = 0;      // first vertex in the scene buffer
    float frames = 180;
    float fps = 60;
    float lastFrame = -1;
    float frameOffset = 0;
    std::vector<std::vector<Affine>> matrixFrames;   // per bone: baked model-space matrices (MtxTransform clips)
    bool hasBillboard = false;

    // Behaviour-driven actors (e.g. the birds): named baked clips, explicit frame and yaw.
    std::map<std::string, std::vector<std::vector<Affine>>> clipMatrices;
    std::string activeClip;
    float drivenFrame = -1;
    float placementScale[3] = {1, 1, 1};
    float placementTranslation[3] = {0, 0, 0};
    float placementRotation[3] = {0, 0, 0};

    std::map<std::string, int> meshOfNode;   // the model's meshes by node name (scene mesh index)
    // Player: local bone transforms and mesh visibility set by PlayerRuntime (empty: clip playback).
    std::vector<Affine> localPose;
    std::vector<std::pair<int, std::vector<bool>>> visibilityOverride;

    // Recovered GrassControl output: the chain joints' x / z = chain root x / z + offset (set
    // before update()).
    std::vector<int> windChain;
    std::vector<std::pair<float, float>> windOffsets;

    // Behaviour-driven curve clips (e.g. the companion): per clip, per-bone channels and length.
    std::map<std::string, std::vector<std::array<AnimationChannel, 9>>> curveClips;
    std::map<std::string, float> curveClipFrames;
    // Per curve clip and bone: baked model-space matrices (MtxTransform segments; empty for bones
    // with curves). Most object archives store their clips this way.
    std::map<std::string, std::vector<std::vector<Affine>>> curveClipMatrices;
    std::vector<std::vector<Affine>> curveMatrices;   // the active curve clip's
    std::map<std::string, float> curveClipStep;       // 60 Hz frames per sample (baked clips 2)
    float curveStep = 1;
    std::map<std::string, std::vector<std::pair<int, std::vector<bool>>>> curveClipVisibility;
    std::string activeCurveClip;

    // Behaviour-controlled visibility (e.g. the goal star after Get, the goal demo star): all meshes.
    std::vector<int> meshIndices;
    bool hidden = false;

    // Curve-clip cross-fade (actor json anim_interp, from the companion NPC's AnimInterp.byml): on
    // a clip switch the local channels blend from the last pose with weight frame / (n - 1) of the
    // new clip (fitted to the companion's traced sensor at Wait -> Walk,
    // OpeningGame::updateCompanionSensor).
    std::map<std::string, int> interp;
    std::vector<std::array<float, 9>> lastLocal;
    std::vector<std::array<float, 9>> fadeFrom;
    int fadeN = 0;

    void setHidden(bool h) {
        if (h != hidden) {
            hidden = h;
            lastFrame = -1;
        }
    }

    // Read one actor's skeleton, clips and skin from its exported json and skin buffer.
    void load(captain_toad::data::JsonView json, const std::vector<uint8_t>& bytes) {
        first = json["first_vertex"].i();
        frames = json["animation"]["frames"].f();
        fps = json["frames_per_second"].f();
        if (bytes.size() % sizeof(SkinVertex)) {
            throw std::runtime_error("Invalid skin buffer");
        }
        skin.resize(bytes.size() / sizeof(SkinVertex));
        memcpy(skin.data(), bytes.data(), bytes.size());

        // Placement in the map.
        float s[] = {1, 1, 1};
        float r[3];
        float t[3];
        for (int i = 0; i < 3; i++) {
            r[i] = json["rotation"][i].f() * M_PI / 180;
            t[i] = json["translation"][i].f();
            if (json["scale"]) {
                s[i] = json["scale"][i].f();
            }
        }
        placement = Affine::trs(s, r, t);
        for (int i = 0; i < 3; i++) {
            placementScale[i] = s[i];
            placementTranslation[i] = t[i];
            placementRotation[i] = r[i];
        }
        if (json["frame_offset"]) {
            frameOffset = json["frame_offset"].f();
        }

        // Grass wind chain.
        if (captain_toad::data::JsonView g = json["grass_control"]) {
            for (captain_toad::data::JsonView name : g["chain"].items()) {
                int index = 0;
                int found = -1;
                for (captain_toad::data::JsonView item : json["bones"].items()) {
                    if (item["name"].is(name.str())) {
                        found = index;
                    }
                    index++;
                }
                if (found < 0) {
                    throw std::runtime_error("Grass chain joint missing");
                }
                windChain.push_back(found);
            }
        }

        // Bones with the default clip.
        for (captain_toad::data::JsonView item : json["bones"].items()) {
            AnimationBone bone{};
            bone.parent = item["parent"].i();
            bone.segmentScaleCompensate = item["segment_scale_compensate"] && item["segment_scale_compensate"].b();
            if (item["retarget_source_length"] && !item["retarget_source_length"].isNull()) {
                bone.retargetSourceLength = item["retarget_source_length"].f();
            }
            if (item["retarget_form_source_length"] && !item["retarget_form_source_length"].isNull()) {
                bone.retargetFormSourceLength = item["retarget_form_source_length"].f();
            }
            bone.billboard = item["billboard"].i();
            if (bone.billboard) {
                hasBillboard = true;
            }
            boneNames.push_back(item["name"].str());
            if (bone.parent >= int(bones.size())) {
                throw std::runtime_error("Bone hierarchy order");
            }
            for (int i = 0; i < 3; i++) {
                bone.defaults[i] = item["scale"][i].f();
                bone.defaults[i + 3] = item["rotation"][i].f();
                bone.defaults[i + 6] = item["translation"][i].f();
            }
            for (int i = 0; i < 12; i++) {
                bone.inverse.m[i] = item["inverse_bind"][i].f();
            }
            bone.channels = readBoneChannels(json["animation"]["tracks"][item["name"].str()]);
            for (const auto& dest : bone.channels) {
                if (dest.present && !dest.constant && dest.keys.empty()) {
                    throw std::runtime_error("Empty animation curve");
                }
            }
            bones.push_back(bone);

            captain_toad::data::JsonView baked = json["animation"]["matrices"][item["name"].str()];
            std::vector<Affine> frameList;
            for (captain_toad::data::JsonView m : baked.items()) {
                Affine a;
                for (int k = 0; k < 12; k++) {
                    a.m[k] = m[k].f();
                }
                frameList.push_back(a);
            }
            matrixFrames.push_back(frameList);
        }

        // Named baked clips.
        if (captain_toad::data::JsonView clips = json["clips"]) {
            for (const std::string& name : clips.keys()) {
                std::vector<std::vector<Affine>> perBone;
                for (captain_toad::data::JsonView item : json["bones"].items()) {
                    std::vector<Affine> frameList;
                    for (captain_toad::data::JsonView m : clips[name]["matrices"][item["name"].str()].items()) {
                        Affine a;
                        for (int k = 0; k < 12; k++) {
                            a.m[k] = m[k].f();
                        }
                        frameList.push_back(a);
                    }
                    if (frameList.empty()) {
                        throw std::runtime_error("Clip lacks a baked bone");
                    }
                    perBone.push_back(frameList);
                }
                clipMatrices[name] = perBone;
            }
        }

        // Named curve clips, with their mesh visibility tracks.
        if (captain_toad::data::JsonView curveClipTable = json["curve_clips"]) {
            for (const std::string& name : curveClipTable.keys()) {
                std::vector<std::array<AnimationChannel, 9>> perBone;
                std::vector<std::vector<Affine>> perBoneMatrices;
                for (captain_toad::data::JsonView item : json["bones"].items()) {
                    perBone.push_back(readBoneChannels(curveClipTable[name]["tracks"][item["name"].str()]));
                    std::vector<Affine> frameList;
                    for (captain_toad::data::JsonView m :
                         curveClipTable[name]["matrices"][item["name"].str()].items()) {
                        Affine a;
                        for (int k = 0; k < 12; k++) {
                            a.m[k] = m[k].f();
                        }
                        frameList.push_back(a);
                    }
                    perBoneMatrices.push_back(std::move(frameList));
                }
                curveClips[name] = perBone;
                // Clip rate (anim player start 0x294efc): the step is 2.0 frames per 30 Hz update
                // (literal 0x295044), i.e. one frame per 60 Hz frame; a clip whose first segment is
                // a baked MtxTransform (segment type 9, bind 0x2948b0) sets flag 4, which forces the
                // step to 1.0 (0x294fe4) and makes the frame getter report twice the frame
                // (0x294764). So a baked clip holds one sample per two 60 Hz frames and its keepers
                // count 60 Hz frames (run sfxA: the enemy's 40-sample Walk repeats every 80
                // frames; sound and effect StartFrames of baked clips reach twice the stored length).
                bool baked = false;
                for (const auto& b : perBoneMatrices) {
                    baked |= !b.empty();
                }
                curveClipStep[name] = baked ? 2.0f : 1.0f;
                curveClipMatrices[name] = std::move(perBoneMatrices);
                curveClipFrames[name] = curveClipTable[name]["frames"].f() * curveClipStep[name];

                std::vector<std::pair<int, std::vector<bool>>> vis;
                captain_toad::data::JsonView clipVisibility = json["curve_clip_visibility"][name];
                for (captain_toad::data::JsonView mesh : json["meshes"].items()) {
                    captain_toad::data::JsonView bits = clipVisibility[mesh["node"].str()];
                    if (bits) {
                        std::vector<bool> values;
                        for (captain_toad::data::JsonView bit : bits.items()) {
                            values.push_back(bit.b());
                        }
                        vis.push_back({mesh["mesh_index"].i(), values});
                    }
                }
                curveClipVisibility[name] = vis;
            }
        }

        for (captain_toad::data::JsonView mesh : json["meshes"].items()) {
            meshIndices.push_back(mesh["mesh_index"].i());
            meshOfNode[mesh["node"].str()] = mesh["mesh_index"].i();
        }
        for (const std::string& name : json["visibility_overlays"].keys()) {
            auto& vis = overlayVisibility[name];
            for (captain_toad::data::JsonView mesh : json["meshes"].items()) {
                captain_toad::data::JsonView bits = json["visibility_overlays"][name]["bits"][mesh["node"].str()];
                if (bits) {
                    std::vector<bool> values;
                    for (captain_toad::data::JsonView bit : bits.items()) {
                        values.push_back(bit.b());
                    }
                    if (!values.empty()) {
                        vis.push_back({mesh["mesh_index"].i(), values});
                    }
                }
            }
        }
        hidden = json["hidden"].b();
        for (const std::string& k : json["anim_interp"].keys()) {
            interp[k] = json["anim_interp"][k].i();
        }
        for (captain_toad::data::JsonView mesh : json["meshes"].items()) {
            captain_toad::data::JsonView bits = json["visibility"][mesh["node"].str()];
            if (bits) {
                std::vector<bool> values;
                for (captain_toad::data::JsonView bit : bits.items()) {
                    values.push_back(bit.b());
                }
                visibility.push_back({mesh["mesh_index"].i(), values});
            }
        }
        for (auto v : skin) {
            for (int j = 0; j < 4; j++) {
                if (v.weights[j] != 0 && v.bones[j] >= bones.size()) {
                    throw std::runtime_error("Skin bone index");
                }
            }
        }
    }

    // Switch to a named baked clip (the caller chooses the frame through drivenFrame).
    void selectClip(const std::string& name) {
        if (name == activeClip) {
            return;
        }
        auto it = clipMatrices.find(name);
        if (it == clipMatrices.end()) {
            throw std::runtime_error("Unknown clip " + name);
        }
        matrixFrames = it->second;
        frames = float(matrixFrames.at(0).size());
        activeClip = name;
        lastFrame = -1;
    }

    // Switch to a named curve clip (the caller chooses the frame through drivenFrame). Starts a
    // cross-fade from the last pose when the actor has interpolation settings.
    void selectCurveClip(const std::string& name) {
        if (name == activeCurveClip) {
            return;
        }
        auto it = curveClips.find(name);
        if (it == curveClips.end()) {
            throw std::runtime_error("Unknown clip " + name);
        }
        for (size_t bi = 0; bi < bones.size(); bi++) {
            bones[bi].channels = it->second.at(bi);
        }
        curveMatrices = curveClipMatrices.at(name);
        curveStep = curveClipStep.at(name);
        if (!activeCurveClip.empty() && !interp.empty() && lastLocal.size() == bones.size()) {
            auto f = interp.find(name);
            fadeN = f != interp.end() ? f->second : (interp.count("Default") ? interp["Default"] : 3);
            fadeFrom = lastLocal;
        }
        frames = curveClipFrames.at(name);
        visibility = curveClipVisibility.at(name);
        activeCurveClip = name;
        lastFrame = -1;
        frameOffset = 0;
    }

    // Placement from a behaviour (row-major 3x4 rotation and translation).
    void setPlacement(const Affine& p) {
        if (p.m != placement.m) {
            placement = p;
            lastFrame = -1;
        }
    }

    // Placement yaw about +Y in degrees (replaces the map rotation's Y component).
    void setYaw(float degrees, const float* translation = nullptr) {
        float r[3] = {placementRotation[0], float(degrees * M_PI / 180), placementRotation[2]};
        Affine p = Affine::trs(placementScale, r, translation ? translation : placementTranslation);
        if (p.m != placement.m) {
            placement = p;
            lastFrame = -1;
        }
    }

    // World transform of a named joint after the last update: findJoint() is empty when the
    // skeleton has no such joint or is not posed yet, joint() throws.
    std::optional<Affine> findJoint(const std::string& name) const {
        for (size_t i = 0; i < boneNames.size() && i < pose.size(); i++) {
            if (boneNames[i] == name) {
                return pose.at(i);
            }
        }
        return std::nullopt;
    }
    // World transform of a named joint for a curve clip at `frame` under `placementNow`, computed
    // now (game logic: a joint-following sensor reads the pose of this tick, not the last drawn
    // one). Without the render's cross-fade between clips. Empty without such a joint.
    // cameraAxes: the camera's x, y and z axes in world space (9 floats) for billboard joints.
    std::optional<Affine> jointAt(const std::string& name, const std::string& clip, float frame,
                                  const Affine& placementNow, const float* cameraAxes = nullptr) const {
        size_t target = boneNames.size();
        for (size_t i = 0; i < boneNames.size(); i++) {
            if (boneNames[i] == name) {
                target = i;
            }
        }
        if (target >= bones.size()) {
            return std::nullopt;
        }
        const auto channels = curveClips.find(clip);
        const auto matrices = curveClipMatrices.find(clip);
        const float step = curveClipStep.count(clip) ? curveClipStep.at(clip) : 1.0f;
        std::vector<Affine> global(bones.size());
        std::vector<std::array<float, 3>> scales(bones.size(), {1.0f, 1.0f, 1.0f});
        for (size_t bi = 0; bi <= target; bi++) {
            const auto& b = bones[bi];
            if (matrices != curveClipMatrices.end() && bi < matrices->second.size() && !matrices->second[bi].empty()) {
                const auto& list = matrices->second[bi];
                global[bi] = list.at(std::min(size_t(std::max(0.0f, frame) / step), list.size() - 1));
                continue;
            }
            float v[9];
            for (int k = 0; k < 9; k++) {
                v[k] = channels != curveClips.end() && bi < channels->second.size()
                           ? channels->second[bi][size_t(k)].evaluate(frame, b.defaults[k])
                           : b.defaults[k];
            }
            Affine local = Affine::trs(v, v + 3, v + 6);
            scales[bi] = {v[0], v[1], v[2]};
            if (b.segmentScaleCompensate && b.parent >= 0) {
                const auto& ps = scales[size_t(b.parent)];
                for (int r = 0; r < 3; r++) {
                    if (ps[size_t(r)] != 0.0f) {
                        for (int c = 0; c < 3; c++) {
                            local.m[r * 4 + c] /= ps[size_t(r)];
                        }
                    }
                }
            }
            global[bi] = b.parent < 0 ? local : Affine::multiply(global[size_t(b.parent)], local);
        }
        Affine w = Affine::multiply(placementNow, global[target]);
        // A Screen / World billboard (modes 2-5) takes the camera's axes, keeping its translation
        // and scale (traced: the pickaxe item's joint, mode 4, faces the camera while its parent
        // spins). The Y-axial modes are not needed by the game logic so far.
        const int mode = bones[target].billboard;
        if (mode >= 2 && mode <= 5 && cameraAxes) {
            for (int c = 0; c < 3; c++) {
                const float sc = std::sqrt(w.m[c] * w.m[c] + w.m[4 + c] * w.m[4 + c] + w.m[8 + c] * w.m[8 + c]);
                for (int r = 0; r < 3; r++) {
                    w.m[r * 4 + c] = cameraAxes[c * 3 + r] * sc;
                }
            }
        } else if (mode >= 6) {
            throw std::runtime_error("jointAt: Y-axial billboard joint not supported");
        }
        return w;
    }
    Affine joint(const std::string& name) const {
        if (auto j = findJoint(name)) {
            return *j;
        }
        throw std::runtime_error("Unknown joint " + name);
    }

    // Pose the skeleton at `frame` and write the skinned vertices. Returns false when the
    // vertices did not change since the last call. Only the joints are posed
    // (pose, mesh visibility; what game logic reads) with `writeVertices` false: the vertices
    // follow with the next call that writes them (skinPending).
    bool update(float frame, std::vector<Vertex>& vertices, std::vector<float>& tangents, std::vector<Mesh>& meshes,
                bool writeVertices = true) {
        // Default clips loop; curve clips driven by a behaviour clamp. A pose set from outside
        // (localPose) does not depend on the frame.
        frame = !localPose.empty()        ? 0.0f
                : activeCurveClip.empty() ? fmod(std::max(0.f, frame + frameOffset), frames)
                                          : std::min(std::max(0.f, frame), frames);
        justPosed = false;
        if (frame == lastFrame && !hasBillboard && windChain.empty()) {
            if (writeVertices && skinPending) {
                skinVertices(vertices, tangents);
                return true;
            }
            return false;
        }
        lastFrame = frame;
        justPosed = true;

        // Bone transforms in model space (global) and skinning matrices (global * inverse bind).
        std::vector<Affine> global;
        std::vector<Affine> skinned;
        std::vector<std::array<float, 3>> localScale(bones.size(), {1.0f, 1.0f, 1.0f});
        // Segment scale compensation: world = parent * T * S(parent)^-1 * R * S, so a child keeps
        // its size when its parent scales (its position still follows the scaled parent).
        auto compensate = [&](const AnimationBone& b, Affine local) {
            if (!b.segmentScaleCompensate || b.parent < 0) {
                return local;
            }
            const auto& ps = localScale[size_t(b.parent)];
            for (int r = 0; r < 3; r++) {
                if (ps[size_t(r)] != 0.0f) {
                    for (int c = 0; c < 3; c++) {
                        local.m[r * 4 + c] /= ps[size_t(r)];
                    }
                }
            }
            return local;
        };
        for (size_t bi = 0; bi < bones.size(); bi++) {
            auto& b = bones[bi];
            Affine world;
            if (!localPose.empty()) {
                const Affine& L = localPose.at(bi);
                for (int c = 0; c < 3; c++) {   // the local scale: the rotation-scale columns' lengths
                    localScale[bi][size_t(c)] =
                        std::sqrt(L.m[c] * L.m[c] + L.m[4 + c] * L.m[4 + c] + L.m[8 + c] * L.m[8 + c]);
                }
                world = b.parent < 0 ? L : Affine::multiply(global.at(b.parent), compensate(b, L));
            } else if (!activeCurveClip.empty() && bi < curveMatrices.size() && !curveMatrices[bi].empty()) {
                // A curve clip's baked bone: the model-space matrix of the integer frame.
                world = sampleBaked(curveMatrices[bi], frame / curveStep);
            } else if (!matrixFrames[bi].empty()) {
                // Baked model-space matrix of the integer frame (SPICA GetTransform convention).
                world = sampleBaked(matrixFrames[bi], frame);
            } else {
                float v[9];
                for (int k = 0; k < 9; k++) {
                    v[k] = b.channels[k].evaluate(frame, b.defaults[k]);
                }
                if (fadeN > 1 && fadeFrom.size() == bones.size()) {
                    const float w = std::min(1.0f, frame / float(fadeN - 1));
                    for (int k = 0; k < 9; k++) {
                        v[k] = fadeFrom[bi][k] + (v[k] - fadeFrom[bi][k]) * w;
                    }
                }
                if (!activeCurveClip.empty()) {
                    if (lastLocal.size() != bones.size()) {
                        lastLocal.resize(bones.size());
                    }
                    for (int k = 0; k < 9; k++) {
                        lastLocal[bi][k] = v[k];
                    }
                }
                Affine local = Affine::trs(v, v + 3, v + 6);
                localScale[bi] = {v[0], v[1], v[2]};
                world = b.parent < 0 ? local : Affine::multiply(global.at(b.parent), compensate(b, local));
            }
            global.push_back(world);
            skinned.push_back(Affine::multiply(world, b.inverse));
        }
        if (fadeN > 1 && localPose.empty() && frame >= float(fadeN - 1)) {
            fadeN = 0;   // fade complete (looping clips wrap later)
        }

        // GrassControl writes absolute joint translations (root x / z + offset, own y); the grass
        // placement is a pure translation, so model space receives the same offsets.
        for (size_t j = 1; j < windChain.size() && j <= windOffsets.size(); j++) {
            int bi = windChain[j];
            global[bi].m[3] = global[windChain[0]].m[3] + windOffsets[j - 1].first;
            global[bi].m[11] = global[windChain[0]].m[11] + windOffsets[j - 1].second;
            skinned[bi] = Affine::multiply(global[bi], bones[bi].inverse);
        }

        pose.clear();
        for (auto& g : global) {
            pose.push_back(Affine::multiply(placement, g));
        }

        // H3D billboard modes (bone flags bits 16-18): 2 World, 3 WorldViewpoint, 4 Screen,
        // 5 ScreenViewpoint: the bone axes follow the camera axes (the viewpoint variants, which
        // face the eye position instead, are drawn as World: an approximation); 6 YAxial,
        // 7 YAxialViewpoint: the bone keeps its Y axis and turns about it to face the camera.
        // Translation and scale are kept.
        for (size_t bi = 0; bi < bones.size(); bi++) {
            if (!bones[bi].billboard) {
                continue;
            }
            const int mode = bones[bi].billboard;
            if (mode < 2 || mode > 7) {
                throw std::runtime_error("Unsupported billboard mode");
            }
            auto& w = pose[bi];
            float sc[3];
            for (int c = 0; c < 3; c++) {
                sc[c] = sqrtf(w.m[c] * w.m[c] + w.m[4 + c] * w.m[4 + c] + w.m[8 + c] * w.m[8 + c]);
            }
            if (mode >= 6) {
                float y[3] = {w.m[1] / sc[1], w.m[5] / sc[1], w.m[9] / sc[1]};
                const float* cz = billboardCamera.data() + 6;
                const float d = cz[0] * y[0] + cz[1] * y[1] + cz[2] * y[2];
                float z[3] = {cz[0] - d * y[0], cz[1] - d * y[1], cz[2] - d * y[2]};
                const float zl = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
                if (zl > 1e-6f) {
                    for (float& v : z) {
                        v /= zl;
                    }
                    const float x[3] = {y[1] * z[2] - y[2] * z[1], y[2] * z[0] - y[0] * z[2],
                                        y[0] * z[1] - y[1] * z[0]};
                    for (int r = 0; r < 3; r++) {
                        w.m[r * 4 + 0] = x[r] * sc[0];
                        w.m[r * 4 + 2] = z[r] * sc[2];
                    }
                }
            } else {
                for (int c = 0; c < 3; c++) {
                    for (int r = 0; r < 3; r++) {
                        w.m[r * 4 + c] = billboardCamera[c * 3 + r] * sc[c];
                    }
                }
            }
            skinned[bi] = Affine::multiply(w, bones[bi].inverse);

            // Express the world billboard in model space for the shared skinning path below.
            Affine inv;
            const auto& P = placement.m;
            float det = P[0] * (P[5] * P[10] - P[6] * P[9]) - P[1] * (P[4] * P[10] - P[6] * P[8]) +
                        P[2] * (P[4] * P[9] - P[5] * P[8]);
            float a[9] = {(P[5] * P[10] - P[6] * P[9]) / det, (P[2] * P[9] - P[1] * P[10]) / det,
                          (P[1] * P[6] - P[2] * P[5]) / det,  (P[6] * P[8] - P[4] * P[10]) / det,
                          (P[0] * P[10] - P[2] * P[8]) / det, (P[2] * P[4] - P[0] * P[6]) / det,
                          (P[4] * P[9] - P[5] * P[8]) / det,  (P[1] * P[8] - P[0] * P[9]) / det,
                          (P[0] * P[5] - P[1] * P[4]) / det};
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    inv.m[r * 4 + c] = a[r * 3 + c];
                }
                inv.m[r * 4 + 3] = -(a[r * 3] * P[3] + a[r * 3 + 1] * P[7] + a[r * 3 + 2] * P[11]);
            }
            global[bi] = Affine::multiply(inv, w);
            skinned[bi] = Affine::multiply(global[bi], bones[bi].inverse);
        }

        // Mesh visibility.
        if (!localPose.empty()) {
            for (auto& entry : visibilityOverride) {
                meshes.at(entry.first).visible = entry.second.at(0);
            }
        } else {
            for (int m : meshIndices) {
                meshes.at(m).visible = true;
            }
            const float visibilityFrame = activeCurveClip.empty() ? frame : frame / curveStep;
            for (auto& entry : visibility) {
                meshes.at(entry.first).visible =
                    entry.second.at(std::min(size_t(visibilityFrame), entry.second.size() - 1));
            }
            auto o = overlay.empty() ? overlayVisibility.end() : overlayVisibility.find(overlay);
            if (o != overlayVisibility.end()) {
                for (auto& entry : o->second) {
                    meshes.at(entry.first).visible =
                        entry.second.at(std::min(size_t(std::max(0.0f, overlayFrame)), entry.second.size() - 1));
                }
            }
        }
        if (hidden) {
            for (int m : meshIndices) {
                meshes.at(m).visible = false;
            }
        }
        skinGlobal_ = std::move(global);
        skinMatrices_ = std::move(skinned);
        poseVersion++;
        skinPending = true;
        if (!writeVertices) {
            return false;
        }
        skinVertices(vertices, tangents);
        return true;
    }

    // Enhancement (drawing between ticks): baked clips blend their two neighbouring samples at a
    // frame between them instead of showing the earlier one (the session sets it with the
    // presentation; off, the integer sample as the original).
    bool smoothSamples = false;
    Affine sampleBaked(const std::vector<Affine>& samples, float at) const {
        const size_t last = samples.size() - 1;
        const size_t i = std::min(size_t(std::max(0.0f, at)), last);
        const float f = at - float(i);
        if (!smoothSamples || i >= last || f <= 0.0f) {
            return samples[i];
        }
        Affine out;   // neighbouring samples: a small step, blended entry by entry
        for (int k = 0; k < 12; k++) {
            out.m[k] = samples[i].m[k] + (samples[i + 1].m[k] - samples[i].m[k]) * f;
        }
        return out;
    }
    bool skinPending = false;   // posed since the vertices were last written
    bool justPosed = false;     // the last update posed the joints anew
    // GPU skinning (render/GpuSkin.hpp): the vertex buffer keeps the bind data and the session
    // draws from the pose's matrices; skinVertices then writes nothing. poseVersion counts poses.
    bool gpuSkinned = false;
    uint32_t poseVersion = 0;
    const std::vector<Affine>& skinMatrices() const { return skinMatrices_; }   // bone x inverse bind
    const std::vector<Affine>& skinGlobals() const { return skinGlobal_; }      // bone (rigid vertices)
    // The bind data of the skin (as stored, before any bone) into the scene's vertices.
    void writeBindData(std::vector<Vertex>& vertices, std::vector<float>& tangents) const {
        for (size_t i = 0; i < skin.size(); i++) {
            for (int k = 0; k < 3; k++) {
                vertices.at(first + i).p[k] = skin[i].p[k];
                vertices.at(first + i).n[k] = skin[i].n[k];
                tangents.at((first + i) * 3 + k) = skin[i].t[k];
            }
        }
    }

    // Writes the skinned vertices of the last pose.
    // `spread`: over the cores (false when the caller already runs models in parallel).
    void skinVertices(std::vector<Vertex>& vertices, std::vector<float>& tangents, bool spread = true) {
        skinPending = false;
        if (gpuSkinned) {
            return;
        }
        const auto& global = skinGlobal_;
        const auto& skinned = skinMatrices_;
        // Skinning: weighted sum of the bone transforms, then the placement (vertex by vertex, so
        // spread over the CPU's cores; render/Parallel.hpp).
        // Bounds are checked once per vertex (not per access): this loop runs for every animated
        // vertex at the display's rate (the headset's main cost after drawing). The arithmetic and
        // its order are unchanged.
        Vertex* const out = vertices.data() + first;
        float* const outTangents = tangents.data() + size_t(first) * 3;
        if (size_t(first) + skin.size() > vertices.size() || (size_t(first) + skin.size()) * 3 > tangents.size()) {
            throw std::out_of_range("skinned vertices beyond the vertex buffer");
        }
        auto slice = [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; i++) {
                const SkinVertex& v = skin[i];
                float p[3] = {};
                float n[3] = {};
                float t[3] = {};
                for (int j = 0; j < 4; j++) {
                    if (v.weights[j] == 0) {
                        continue;
                    }
                    const auto& table = v.mode == 1 ? skinned : global;
                    if (v.bones[j] >= table.size()) {
                        throw std::out_of_range("skin bone index");
                    }
                    const Affine& a = table[v.bones[j]];
                    auto pp = a.apply(v.p, true);
                    auto nn = a.apply(v.n, false);
                    auto tt = a.apply(v.t, false);
                    for (int k = 0; k < 3; k++) {
                        p[k] += pp[k] * v.weights[j];
                        n[k] += nn[k] * v.weights[j];
                        t[k] += tt[k] * v.weights[j];
                    }
                }
                auto pp = placement.apply(p, true);
                auto nn = placement.apply(n, false);
                auto tt = placement.apply(t, false);
                for (int k = 0; k < 3; k++) {
                    out[i].p[k] = pp[k];
                    out[i].n[k] = nn[k];
                    outTangents[i * 3 + k] = tt[k];
                }
            }
        };
        if (spread) {
            captain_toad::parallel::Pool::shared().forRange(skin.size(), 2048, slice);
        } else {
            slice(0, skin.size());
        }
    }

private:
    std::vector<Affine> skinGlobal_, skinMatrices_;   // the last pose's bone and skinning matrices
};
