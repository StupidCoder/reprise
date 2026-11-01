// =============================================================================================
// MaterialAnimation.hpp - animated textures and colours
// =============================================================================================
//
// Some materials animate: water scrolls, a glow pulses, a sign blinks. The ROM stores these as
// material animations (texture scale / rotation / translation curves per texture slot, and
// constant-colour curves), exported by tools/export_actors.py. This file evaluates them and
// uploads the results as shader uniforms.
//
// The texture matrix follows the convention of the authoring tool recorded per slot (Maya,
// Softimage or 3ds Max), as also documented by the public-domain SPICA tool; the values were
// checked exactly against traced texture coordinates and constants at frame 558.
// =============================================================================================
#pragma once
#include "gpu/Device.hpp"
#include "data/Package.hpp"
#include "render/CharacterAnimation.hpp"
#include "render/LightService.hpp"
#include <algorithm>
#include <cmath>
#include <map>

// Texture transform animation of one texture slot.
struct MaterialSlotAnimation {
    int slot = 0;
    int convention = 0;     // 0 Maya, 1 SoftImage, 2 3dsMax
    float scale[2]{1, 1};   // static values (used where no curve exists)
    float rotation = 0;
    float translation[2]{0, 0};
    double frames = 1;   // loop length
    AnimationChannel scaleCurve[2];
    AnimationChannel rotationCurve;
    AnimationChannel translationCurve[2];
};

// Constant-colour animation of one material constant.
struct MaterialConstantAnimation {
    float base[4];
    AnimationChannel axes[4];
    double frames = 1;
    std::string uniform = "animatedConstant";
};

// Lighting colour animation of one material colour (fragment lighting).
struct MaterialLightAnimation {
    int target = 0;   // 0 emission, 1 ambient, 2 diffuse, 3 specular0, 4 specular1
    float base[4];
    AnimationChannel axes[4];
    double frames = 1;
};

// Texture pattern of one texture slot: the curve picks one of the textures.
struct MaterialPatternAnimation {
    int slot = 0;
    std::vector<int> textures;   // package texture indices (-1: missing)
    AnimationChannel curve;
    double frames = 1;
};

struct MaterialAnimationSet {
    std::map<int, std::vector<MaterialSlotAnimation>> slots;           // material index -> slots
    std::map<int, std::vector<MaterialConstantAnimation>> constants;   // material index -> constants
    std::map<int, std::vector<MaterialLightAnimation>> lights;         // material index -> colours
    std::map<int, std::vector<MaterialPatternAnimation>> patterns;     // material index -> patterns

    // The clip's length in frames (its longest animation).
    double length() const {
        double n = 0;
        for (const auto& [m, v] : slots) {
            for (const auto& a : v) {
                n = std::max(n, double(a.frames));
            }
        }
        for (const auto& [m, v] : constants) {
            for (const auto& a : v) {
                n = std::max(n, double(a.frames));
            }
        }
        for (const auto& [m, v] : lights) {
            for (const auto& a : v) {
                n = std::max(n, double(a.frames));
            }
        }
        for (const auto& [m, v] : patterns) {
            for (const auto& a : v) {
                n = std::max(n, double(a.frames));
            }
        }
        return n;
    }
    static float clipFrame(double frame, double frames, bool loop) {
        return loop ? (float)fmod(frame, frames) : (float)std::min(std::max(frame, 0.0), frames);
    }
    // The lighting colours of `material` at `frame` (8-bit, rounded).
    void lightColors(int material, double frame, bool loop, LightService::ColorOverride& out) const {
        auto l = lights.find(material);
        if (l == lights.end()) {
            return;
        }
        for (const auto& a : l->second) {
            const float f = clipFrame(frame, a.frames, loop);
            out.set[a.target] = true;
            for (int k = 0; k < 3; k++) {
                const float v = a.axes[k].evaluate(f, a.base[k]);
                out.color[a.target][k] = std::clamp(int(std::lround(v * 255.0f)), 0, 255);
            }
        }
    }
    // The texture a pattern shows in `slot` of `material` at `frame` (-1: none).
    int patternTexture(int material, int slot, double frame, bool loop) const {
        auto p = patterns.find(material);
        if (p == patterns.end()) {
            return -1;
        }
        for (const auto& a : p->second) {
            if (a.slot == slot && !a.textures.empty()) {
                const float f = clipFrame(frame, a.frames, loop);
                const int i = int(std::lround(a.curve.evaluate(f, 0.0f)));
                return a.textures[size_t(std::clamp(i, 0, int(a.textures.size()) - 1))];
            }
        }
        return -1;
    }

    // A material curve (as readChannel, but a curve without keys is an error).
    static AnimationChannel channel(captain_toad::data::JsonView c) {
        AnimationChannel a = readChannel(c);
        if (a.present && !a.constant && a.keys.empty()) {
            throw std::runtime_error("Empty material curve");
        }
        return a;
    }

    void load(captain_toad::data::JsonView list) {
        for (captain_toad::data::JsonView m : list.items()) {
            int index = m["material_index"].i();
            for (const std::string& key : m["slots"].keys()) {
                captain_toad::data::JsonView s = m["slots"][key];
                MaterialSlotAnimation a;
                a.slot = std::atoi(key.c_str());
                a.convention = s["convention"].i();
                for (int k = 0; k < 2; k++) {
                    a.scale[k] = s["scale"][k].f();
                    a.translation[k] = s["translation"][k].f();
                }
                a.rotation = s["rotation"].f();
                a.frames = s["frames"].d();
                captain_toad::data::JsonView curves = s["curves"];
                if (curves["scale"]) {
                    for (int k = 0; k < 2; k++) {
                        a.scaleCurve[k] = channel(curves["scale"][k]);
                    }
                }
                if (curves["rot"]) {
                    a.rotationCurve = channel(curves["rot"][0]);
                }
                if (curves["trans"]) {
                    for (int k = 0; k < 2; k++) {
                        a.translationCurve[k] = channel(curves["trans"][k]);
                    }
                }
                slots[index].push_back(a);
            }
            for (captain_toad::data::JsonView c : m["constants"].items()) {
                MaterialConstantAnimation a;
                for (int k = 0; k < 4; k++) {
                    a.base[k] = c["base"][k].f();
                    a.axes[k] = channel(c["axes"][k]);
                }
                a.frames = c["frames"].d();
                if (c["uniform"]) {
                    a.uniform = c["uniform"].str();
                }
                constants[index].push_back(a);
            }
            for (captain_toad::data::JsonView c : m["lights"].items()) {
                MaterialLightAnimation a;
                a.target = c["target"].i();
                for (int k = 0; k < 4; k++) {
                    a.base[k] = c["base"][k].f();
                    a.axes[k] = channel(c["axes"][k]);
                }
                a.frames = c["frames"].d();
                lights[index].push_back(a);
            }
            for (captain_toad::data::JsonView c : m["patterns"].items()) {
                MaterialPatternAnimation a;
                a.slot = c["slot"].i();
                for (captain_toad::data::JsonView t : c["textures"].items()) {
                    a.textures.push_back(t.i());
                }
                a.curve = channel(c["curve"]);
                a.frames = c["frames"].d();
                patterns[index].push_back(a);
            }
        }
    }

    // Upload the animated values of `material` at `frame` (looping, or held at the end) to
    // `program`.
    void apply(captain_toad::gpu::Program& program, int material, double frame, bool loop = true) const {
        auto at = [&](double frames) {
            return loop ? (float)fmod(frame, frames) : (float)std::min(std::max(frame, 0.0), frames);
        };
        auto s = slots.find(material);
        if (s != slots.end()) {
            for (const auto& a : s->second) {
                float f = at(a.frames);
                float SX = a.scaleCurve[0].evaluate(f, a.scale[0]);
                float SY = a.scaleCurve[1].evaluate(f, a.scale[1]);
                float R = a.rotationCurve.evaluate(f, a.rotation);
                float TX = a.translationCurve[0].evaluate(f, a.translation[0]);
                float TY = a.translationCurve[1].evaluate(f, a.translation[1]);
                float CA = cosf(R);
                float SA = sinf(R);

                // Translation column of the 2x3 texture matrix, per authoring tool convention.
                float M41, M42;
                if (a.convention == 0) {
                    M41 = SX * ((0.5f * SA - 0.5f * CA) + 0.5f - TX);
                    M42 = SY * ((0.5f * -SA - 0.5f * CA) + 0.5f - TY);
                } else if (a.convention == 1) {
                    M41 = SX * (-CA * TX - SA * TY);
                    M42 = SY * (SA * TX - CA * TY);
                } else {
                    M41 = SX * CA * (-TX - 0.5f) - SX * SA * (TY - 0.5f) + 0.5f;
                    M42 = SY * SA * (-TX - 0.5f) + SY * CA * (TY - 0.5f) + 0.5f;
                }
                char u[16], v[16];
                snprintf(u, sizeof(u), "texU%d", a.slot);
                snprintf(v, sizeof(v), "texV%d", a.slot);
                program.set(u, SX * CA, SX * -SA, M41);
                program.set(v, SY * SA, SY * CA, M42);
            }
        }
        auto c = constants.find(material);
        if (c != constants.end()) {
            for (const auto& a : c->second) {
                float f = at(a.frames);
                float v[4];
                for (int k = 0; k < 4; k++) {
                    v[k] = a.axes[k].evaluate(f, a.base[k]);
                }
                program.setFloats(a.uniform, v, 4);
            }
        }
    }
};
