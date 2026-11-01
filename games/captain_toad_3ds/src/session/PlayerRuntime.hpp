// =============================================================================================
// PlayerRuntime.hpp - connecting the recovered player to the controls and the screen
// =============================================================================================
//
// The protagonist's game logic is recovered in separate modules: the pad (Pad.hpp), the action
// machine (PlayerActions.hpp with the generated PlayerGraph.inc), walking (PlayerWalk.hpp),
// collision (PlayerCollider.hpp) and animation timing (PlayerAnim.hpp). This file drives them
// from the platform's controls and turns their state into a skinned pose. It is presentation and
// platform glue only.
//
// Platform choices and hypotheses (documented in docs/player-movement.md):
//   * the controls map to raw circle pad counts (radius 156), which then pass through the
//     recovered SDK conversion;
//   * the pad's camera matrix comes from the recovered game camera (OpeningGame.hpp);
//   * Move blends its four clips (slow walk .. run) in local bone space (quaternion rotation),
//     and an action change cross-fades from the previous pose over its AnimInterp frames
//     (2 per tick);
//   * the visible pose follows the controller frame once per 30 Hz tick, like the original.
// =============================================================================================
#pragma once
#include <array>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include "game/PlayerActions.hpp"
#include "game/PlayerCarry.hpp"
#include "game/PlayerGraph.inc"

// One animation clip of the protagonist.
struct PlayerClip {
    float frames = 0;
    bool loop = false;
    std::vector<std::array<AnimationChannel, 9>> bones;   // per skeleton bone
    // Per mesh node, per frame. By node name: the Super and Mini models share the clips and the
    // node names (hands, faces, eyes), each with its own meshes.
    std::vector<std::pair<std::string, std::vector<bool>>> visibility;
};

struct PlayerRuntime {
    bool enabled = false;
    captain_toad::RecoveredKcl::Model kcl;
    std::vector<std::unique_ptr<captain_toad::RecoveredKcl::Model>> partModels;   // stage packages: one per part
    std::filesystem::path packageDirectory;                                       // set before load()
    captain_toad::RecoveredPlayerCollider::Body body;
    captain_toad::RecoveredPlayerCollider::Collider collider;
    captain_toad::RecoveredPad::Pad pad;
    captain_toad::RecoveredPad::CirclePadState sdk;
    captain_toad::RecoveredPlayerAnim::Player anim;
    captain_toad::RecoveredPlayerActions::Areas areas;
    captain_toad::RecoveredPlayerActions::Machine machine;
    std::map<std::string, PlayerClip> clips;
    std::map<std::string, std::vector<std::string>> actionClips;   // action -> its clips (Move has four)
    std::map<std::string, int> interp;                             // action -> cross-fade frames
    float stickX = 0;
    float stickY = 0;
    bool dashHeld = false;
    bool prevDash = false;
    long long frames60 = 0;
    std::string lastError;

    // Pose state: a bone's local scale, rotation (quaternion) and translation.
    struct Local {
        float s[3];
        float q[4];
        float t[3];
    };
    // Carrying (game/PlayerCarry.hpp): the controller and the partial slot's joints (Spine1 and
    // below, partial group 0).
    captain_toad::RecoveredPlayerCarry::Controller carry;
    std::vector<char> upperMask;
    // Clip length and loop flag for the carry controller.
    void clipInfo(const std::string& name, float& frames, bool& loop) const {
        auto it = clips.find(name);
        frames = it != clips.end() ? it->second.frames : 0.0f;
        loop = it != clips.end() && it->second.loop;
    }
    void carryUpdate() {
        carry.update([this](const std::string& n, float& f, bool& l) { clipInfo(n, f, l); }, anim.isEnd(),
                     anim.isPlaying("PullOutSuccessCarry"));
        machine.ctx.carryKind = carry.kind;
        machine.ctx.carryMode = carry.mode;
    }
    void carryStart(int kind) {
        carry.start(kind, [this](const std::string& n, float& f, bool& l) { clipInfo(n, f, l); });
        machine.ctx.carryKind = carry.kind;
        machine.ctx.carryMode = carry.mode;
    }
    // The override clip (0x181840): played over the action on the whole body, 2 frames a tick.
    std::string overrideClip;
    float overrideFrame = 0;
    float overrideFrames = 0;
    bool overridePlaying() const { return !overrideClip.empty(); }   // anim vtable +0x3c (0x3e29cc)
    bool overrideFresh = false;                                      // started this tick: the first advance is skipped
    void playOverride(const std::string& name) {
        bool loop = false;
        overrideClip = name;
        overrideFrame = 0;
        overrideFresh = true;
        clipInfo(name, overrideFrames, loop);
    }
    void overrideUpdate() {
        if (overrideClip.empty()) {
            return;
        }
        if (overrideFresh) {
            overrideFresh = false;
            return;
        }
        overrideFrame += 2.0f;
        if (overrideFrame >= overrideFrames) {
            overrideClip.clear();   // 0x181754
        }
    }
    void carryEnd() {   // 0x1ee6f4
        carry.end();
        machine.ctx.carryKind = carry.kind;
        machine.ctx.carryMode = carry.mode;
    }
    void carryThrow() {
        carry.throwIt([this](const std::string& n, float& f, bool& l) { clipInfo(n, f, l); });
        machine.ctx.carryKind = carry.kind;
        machine.ctx.carryMode = carry.mode;
    }
    std::vector<Local> current;
    std::vector<Local> fadeFrom;
    std::string poseAction;
    int form = 0;   // the power form (0 Super, 1 Mini; player +0xd0 -> +0x44)
    // The model the shared clips were first bound to (the Super Kinopio while Mini is shown):
    // channels a clip does not key take that model's rest values (0x375a00 resolves them once,
    // from the first model). Null: the posed model's own.
    const CharacterAnimation* restSource = nullptr;
    std::vector<int> restMap;   // posed bone -> restSource bone (-1: none)
    bool formClip = false;      // the current action resolved to the form's own clip ("Mini" + clip)
    float fadeTotal = 0;
    float fadeLeft = 0;
    bool poseDirty = true;

    static captain_toad::RecoveredVec::V3 v3(captain_toad::data::JsonView a) { return {a[0].f(), a[1].f(), a[2].f()}; }

    // Inverse of a row-major 3x4 affine matrix (a collision part's world -> model transform).
    static captain_toad::RecoveredVec::M34 inverseAffine(const captain_toad::RecoveredVec::M34& w) {
        return captain_toad::RecoveredVec::inverseAffine(w);
    }

    // Load the player package (collision, areas, actions, clips) and place the protagonist.
    void load(captain_toad::data::JsonView player, captain_toad::data::JsonView character,
              const std::vector<uint8_t>& kclBytes, const CharacterAnimation& ch) {
        using captain_toad::RecoveredVec::V3;

        // Collision: the opening's terrain KCL as one part at its translation, or (stage packages)
        // every placed object's KCL with its placement matrix.
        if (captain_toad::data::JsonView parts = player["collision"]["parts"]) {
            collider.parts.clear();
            collider.partMoving.clear();
            partModels.clear();
            for (captain_toad::data::JsonView p : parts.items()) {
                partModels.push_back(std::make_unique<captain_toad::RecoveredKcl::Model>());
                partModels.back()->load(captain_toad::data::readBytes(packageDirectory / p["file"].str()));
                captain_toad::RecoveredCollision::Part part;
                part.kcl = partModels.back().get();
                float w[12];
                for (int k = 0; k < 12; k++) {
                    w[k] = p["matrix"][k].f();
                }
                memcpy(part.world.m, w, 48);
                part.inverse = inverseAffine(part.world);
                part.scale = p["scale"].f();
                if (p["depth_scale"]) {
                    part.depthScale = p["depth_scale"].f();
                }
                collider.parts.push_back(part);
                collider.partMoving.push_back(true);
            }
        } else {
            kcl.load(kclBytes);
            captain_toad::RecoveredCollision::Part part;
            part.kcl = &kcl;
            V3 t = v3(player["collision"]["translate"]);
            float w[12] = {1, 0, 0, t.x, 0, 1, 0, t.y, 0, 0, 1, t.z};
            float inv[12] = {1, 0, 0, -t.x, 0, 1, 0, -t.y, 0, 0, 1, -t.z};
            memcpy(part.world.m, w, 48);
            memcpy(part.inverse.m, inv, 48);
            collider.parts = {part};
            collider.partMoving = {true};
        }
        collider.body = &body;

        // Areas: death zones and areas that play an animation.
        for (captain_toad::data::JsonView a : (player["areas"]["death"]).items()) {
            areas.death.push_back(box(a));
        }
        for (captain_toad::data::JsonView a : (player["areas"]["water"]).items()) {
            areas.water.push_back(box(a));
        }
        for (captain_toad::data::JsonView a : (player["areas"]["player_anim"]).items()) {
            auto b = box(a);
            b.animName = a["anim"].str();
            b.startFrame = a["start_frame"].i();
            b.interval = a["interval"].i();
            areas.playerAnim.push_back(b);
        }

        // Actions and their clips.
        for (const std::string& act : player["actions"].keys()) {
            captain_toad::data::JsonView e = player["actions"][act];
            anim.actions[act] = {e["frames"].f(), e["loop"].b()};
            std::vector<std::string> subs;
            for (captain_toad::data::JsonView s : e["clips"].items()) {
                subs.push_back(s.str());
            }
            actionClips[act] = subs;
        }
        for (const std::string& k : player["anim_interp"].keys()) {
            interp[k] = player["anim_interp"][k].i();
        }

        // Clips: curves per bone, visibility per mesh node.
        captain_toad::data::JsonView curves = character["clips_curves"];
        for (const std::string& name : curves.keys()) {
            captain_toad::data::JsonView a = curves[name]["animation"];
            PlayerClip c;
            c.frames = a["frames"].f();
            c.loop = a["loop"].b();
            for (size_t bi = 0; bi < ch.boneNames.size(); bi++) {
                c.bones.push_back(readBoneChannels(a["tracks"][ch.boneNames[bi]]));
            }
            captain_toad::data::JsonView vis = curves[name]["visibility"];
            for (const std::string& node : vis.keys()) {
                std::vector<bool> bits;
                for (captain_toad::data::JsonView b : (vis[node]).items()) {
                    bits.push_back(b.b());
                }
                c.visibility.push_back({node, bits});
            }
            clips[name] = c;
        }

        // Start: the map placement; one collider tick establishes the ground contact before the
        // action machine runs.
        body.pos = v3(player["start"]["translate"]);
        float ry = player["start"]["rotate"][1].f() * float(M_PI / 180.0);
        body.front = {std::sin(ry), 0, std::cos(ry)};
        body.up = {0, 1, 0};
        body.groundUp = {0, 1, 0};
        body.gravity = {0, -1, 0};
        body.vel = {0, 0, 0};
        collider.tick();
        machine.ctx.body = &body;
        machine.ctx.collider = &collider;
        machine.ctx.pad = &pad;
        machine.ctx.anim = &anim;
        machine.ctx.areas = &areas;
        machine.build();
        machine.current->action->enter();
        anim.copyMirror();
        enabled = true;
    }

    // An area box: a unit cube of 1000 (x / z centred, y from the floor) scaled by the placement.
    static captain_toad::RecoveredPlayerActions::AreaBox box(captain_toad::data::JsonView a) {
        captain_toad::RecoveredPlayerActions::AreaBox b;
        b.translate = v3(a["translate"]);
        captain_toad::RecoveredVec::V3 s = v3(a["scale"]);
        b.min = {-500.0f * s.x, 0.0f, -500.0f * s.z};
        b.max = {500.0f * s.x, 1000.0f * s.y, 500.0f * s.z};
        return b;
    }

    // One 60 Hz frame of input: the SDK circle pad conversion (0x27df28, every frame). Returns
    // true when this frame carries a 30 Hz game tick (then call tick with this tick's camera).
    bool input(float stickXIn, float stickYIn, bool dash) {
        if (!enabled) {
            return false;
        }
        // Platform stick -> raw circle pad counts (circular, radius 156).
        float m = std::sqrt(stickXIn * stickXIn + stickYIn * stickYIn);
        float k = m > 1.0f ? 1.0f / m : 1.0f;
        int16_t rx = int16_t(std::lround(stickXIn * k * 156.0f));
        int16_t ry = int16_t(std::lround(stickYIn * k * 156.0f));
        captain_toad::RecoveredPad::convertCirclePad(sdk, rx, ry, stickX, stickY);
        dashHeld = dash;
        frames60++;
        return frames60 % 2 == 0;   // the game logic runs at 30 Hz
    }

    // One 30 Hz player tick. cam: camera-to-world 3x4 (the inverted game view, 0x15c5e4 -> 0x2f2074).
    // deferMovement: the caller runs modelMovement() after its own exchanges with the player.
    void tick(const float cam[12], bool deferMovement = false) {
        if (!enabled) {
            return;
        }
        machine.deferAdvance = deferMovement;
        // The pad sees the collider's wall contacts (in the pad's list order).
        captain_toad::RecoveredPad::WallContact c[6];
        const int order[6] = {0, 1, 2, 3, 5, 4};
        for (int i = 0; i < 6; i++) {
            c[i].valid = collider.lists[order[i]].valid;
            c[i].normal = collider.lists[order[i]].vec;
        }
        captain_toad::RecoveredPad::update(pad, cam, c, collider.onGround(), stickX, stickY, dashHeld);
        machine.ctx.dashTrigger = dashHeld && !prevDash;
        prevDash = dashHeld;
        try {
            machine.tick();
        } catch (const std::exception& e) {
            lastError = e.what();
            enabled = false;
        }
        poseDirty = true;
    }

    // The player model's movement (0x160a8c -> 0x2d6e90): the skeleton's advance (0x294780).
    void modelMovement() {
        if (enabled) {
            anim.advance();
        }
    }

    void frame(const float cam[12], float stickXIn, float stickYIn, bool dash) {
        if (input(stickXIn, stickYIn, dash)) {
            tick(cam);
        }
    }

    // Scripted ticks (goal sequence, Goal.hpp): the action machine does not run.
    // The clip continues, without movement.
    void animationTick() {
        anim.advance();
        anim.copyMirror();
        poseDirty = true;
    }

    // The sequence sets the clip and the frame.
    void scriptedClip(const std::string& clip, float frame) {
        if (anim.action != clip) {
            anim.action = clip;
            anim.loop = false;
        }
        anim.frame = frame;
        anim.mirrorFrame = frame;
        poseDirty = true;
    }

    // A binder (ladder) drives the clip: a new clip is started like any action (0x181fc8, the
    // clip's own length and loop flag), so a walk clip ("Move") left playing at the release keeps
    // looping when the walk state takes over.
    void boundClip(const std::string& clip, float frame) {
        if (anim.action != clip) {
            anim.start(clip);
        }
        anim.frame = frame;
        anim.mirrorFrame = frame;
        poseDirty = true;
    }

    // Collider vtable +0x18 (0x2c53cc): knock-back accumulator. +0x68 keeps the per-axis minimum
    // and +0x74 the maximum of the pushes; both are added to the next tick's move and then cleared.
    void addPush(const captain_toad::RecoveredVec::V3& p) {
        auto& a = collider.extra68;
        auto& b = collider.extra74;
        a.x = std::min(a.x, p.x);
        a.y = std::min(a.y, p.y);
        a.z = std::min(a.z, p.z);
        b.x = std::max(b.x, p.x);
        b.y = std::max(b.y, p.y);
        b.z = std::max(b.z, p.z);
    }

    void place(const captain_toad::RecoveredVec::V3& pos, const captain_toad::RecoveredVec::V3& front) {
        body.pos = pos;
        body.front = front;
        body.up = {0, 1, 0};
        body.vel = {0, 0, 0};
    }

    // Euler (radians) -> quaternion, matching Affine::trs (Rz * Ry * Rx).
    static void eulerToQuat(const float r[3], float q[4]) {
        float cx = std::cos(r[0] * 0.5f);
        float sx = std::sin(r[0] * 0.5f);
        float cy = std::cos(r[1] * 0.5f);
        float sy = std::sin(r[1] * 0.5f);
        float cz = std::cos(r[2] * 0.5f);
        float sz = std::sin(r[2] * 0.5f);
        q[0] = sx * cy * cz - cx * sy * sz;
        q[1] = cx * sy * cz + sx * cy * sz;
        q[2] = cx * cy * sz - sx * sy * cz;
        q[3] = cx * cy * cz + sx * sy * sz;
    }

    static Affine localAffine(const Local& l) {
        float x = l.q[0];
        float y = l.q[1];
        float z = l.q[2];
        float w = l.q[3];
        Affine a;
        a.m = {(1 - 2 * (y * y + z * z)) * l.s[0], (2 * (x * y - w * z)) * l.s[1],
               (2 * (x * z + w * y)) * l.s[2],     l.t[0],
               (2 * (x * y + w * z)) * l.s[0],     (1 - 2 * (x * x + z * z)) * l.s[1],
               (2 * (y * z - w * x)) * l.s[2],     l.t[1],
               (2 * (x * z - w * y)) * l.s[0],     (2 * (y * z + w * x)) * l.s[1],
               (1 - 2 * (x * x + y * y)) * l.s[2], l.t[2]};
        return a;
    }

    // The rest values standing in for a clip's unkeyed channels of bone `bi`.
    const float* clipDefaults(const CharacterAnimation& ch, size_t bi) {
        if (!restSource || restSource == &ch) {
            return ch.bones[bi].defaults;
        }
        if (restMap.size() != ch.bones.size()) {
            restMap.assign(ch.bones.size(), -1);
            for (size_t i = 0; i < ch.bones.size(); i++) {
                for (size_t k = 0; k < restSource->boneNames.size(); k++) {
                    if (restSource->boneNames[k] == ch.boneNames[i]) {
                        restMap[i] = int(k);
                    }
                }
            }
        }
        return restMap[bi] >= 0 ? restSource->bones[size_t(restMap[bi])].defaults : ch.bones[bi].defaults;
    }

    // The Mini form's retargeting (PlayerRetargettingInfo; applied after the blend, 0x294d7c):
    // each bone's local translation t keeps its direction; its length becomes the posed model's
    // rest length plus half the clip's deviation from the clip skeleton's length:
    //   t' = d * |rest| + (t - d * sourceLength) * scale,  d = t / |t|  (skipped for |t|^2 <= 1e-6)
    // The scale is 0.5 for the Mini form's shared clips (0x214934: DAT_00214c38 when the form is
    // 1); the Super form at scale 1 (see apply).
    static void retarget(const CharacterAnimation& ch, std::vector<Local>& pose, float scale, bool formClip) {
        for (size_t bi = 0; bi < pose.size() && bi < ch.bones.size(); bi++) {
            const auto& b = ch.bones[bi];
            const float sourceLength = formClip ? b.retargetFormSourceLength : b.retargetSourceLength;
            if (sourceLength < 0) {
                continue;
            }
            float* t = pose[bi].t;
            const float len2 = t[0] * t[0] + t[1] * t[1] + t[2] * t[2];
            if (len2 <= 1e-6f) {
                continue;
            }
            const float len = std::sqrt(len2);
            const float* r = b.defaults + 6;
            const float rest = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            for (int k = 0; k < 3; k++) {
                const float d = t[k] / len;
                t[k] = d * rest + (t[k] - d * sourceLength) * scale;
            }
        }
    }

    // Local bone pose of the current action at the controller frame. Multi-clip actions (Move)
    // blend their clips by the animation weights; the visibility comes from the dominant clip.
    std::vector<Local> evaluate(const CharacterAnimation& ch, std::vector<std::pair<int, std::vector<bool>>>& vis,
                                float frameOverride) {
        std::vector<std::pair<const PlayerClip*, float>> parts;
        auto it = actionClips.find(anim.action);
        std::vector<std::string> names = it != actionClips.end() ? it->second : std::vector<std::string>{anim.action};
        // The clip resolver 0x2c716c: the power form's clip ("Mini" + clip) when the animation
        // archive has one, otherwise the clip itself.
        formClip = false;
        if (form == 1) {
            for (auto& n : names) {
                if (clips.count("Mini" + n)) {
                    n = "Mini" + n;
                    formClip = true;   // resolver kind 2
                }
            }
        }
        if (names.size() > 1) {
            for (size_t i = 0; i < names.size() && i < 6; i++) {
                if (anim.weights[i] > 0 && clips.count(names[i])) {
                    parts.push_back({&clips.at(names[i]), anim.weights[i]});
                }
            }
        } else if (clips.count(names[0])) {
            parts.push_back({&clips.at(names[0]), 1.0f});
        }
        const float fr = frameOverride >= 0 ? frameOverride : anim.frame;
        std::vector<Local> out(ch.bones.size());
        const PlayerClip* dominant = nullptr;
        float dominantWeight = -1;
        for (auto& p : parts) {
            if (p.second > dominantWeight) {
                dominantWeight = p.second;
                dominant = p.first;
            }
        }

        for (size_t bi = 0; bi < ch.bones.size(); bi++) {
            Local acc{};
            float ref[4] = {0, 0, 0, 0};
            float total = 0;
            if (parts.empty()) {
                // No clip: the bind pose.
                const float* d = ch.bones[bi].defaults;
                float q[4];
                eulerToQuat(d + 3, q);
                out[bi] = {{d[0], d[1], d[2]}, {q[0], q[1], q[2], q[3]}, {d[6], d[7], d[8]}};
                continue;
            }
            for (auto& p : parts) {
                float v[9];
                for (int k = 0; k < 9; k++) {
                    v[k] = p.first->bones[bi][k].evaluate(std::min(fr, p.first->frames), clipDefaults(ch, bi)[k]);
                }
                float q[4];
                eulerToQuat(v + 3, q);
                if (total == 0) {
                    for (int k = 0; k < 4; k++) {
                        ref[k] = q[k];
                    }
                }
                // Keep all quaternions in the same hemisphere as the first before summing.
                float dot = q[0] * ref[0] + q[1] * ref[1] + q[2] * ref[2] + q[3] * ref[3];
                float sgn = dot < 0 ? -1.0f : 1.0f;
                float wgt = p.second;
                for (int k = 0; k < 3; k++) {
                    acc.s[k] += v[k] * wgt;
                    acc.t[k] += v[6 + k] * wgt;
                }
                for (int k = 0; k < 4; k++) {
                    acc.q[k] += q[k] * wgt * sgn;
                }
                total += wgt;
            }
            for (int k = 0; k < 3; k++) {
                acc.s[k] /= total;
                acc.t[k] /= total;
            }
            float n = std::sqrt(acc.q[0] * acc.q[0] + acc.q[1] * acc.q[1] + acc.q[2] * acc.q[2] + acc.q[3] * acc.q[3]);
            for (int k = 0; k < 4; k++) {
                acc.q[k] /= n;
            }
            out[bi] = acc;
        }

        vis.clear();
        if (dominant) {
            // The posed model's own meshes of those nodes (the other form's model is hidden).
            for (auto& v : dominant->visibility) {
                auto m = ch.meshOfNode.find(v.first);
                if (m != ch.meshOfNode.end()) {
                    vis.push_back(
                        {m->second, {v.second.at(std::min(size_t(std::max(0.0f, fr)), v.second.size() - 1))}});
                }
            }
        }
        return out;
    }

    // Apply the pose and the placement to the character. The placement is [side, up, front] *
    // body +0x30 at the body position (the matrix 0x2e7184 decomposes into the actor's Euler
    // rotation).
    void apply(CharacterAnimation& ch, float frameOverride = -1) {
        if (!enabled && lastError.empty()) {
            return;
        }
        std::vector<std::pair<int, std::vector<bool>>> vis;
        auto target = evaluate(ch, vis, frameOverride);

        // Action change: start a cross-fade from the current pose.
        if (anim.action != poseAction) {
            if (!current.empty()) {
                auto it = interp.find(anim.action);
                int n = it != interp.end() ? it->second : (interp.count("Default") ? interp["Default"] : 3);
                fadeFrom = current;
                fadeTotal = float(n);
                fadeLeft = float(n);
            }
            poseAction = anim.action;
        }
        if (fadeLeft > 0 && fadeFrom.size() == target.size()) {
            float t = 1.0f - fadeLeft / fadeTotal;
            for (size_t i = 0; i < target.size(); i++) {
                Local& a = fadeFrom[i];
                Local& b = target[i];
                float dot = a.q[0] * b.q[0] + a.q[1] * b.q[1] + a.q[2] * b.q[2] + a.q[3] * b.q[3];
                float sg = dot < 0 ? -1.0f : 1.0f;
                for (int k = 0; k < 3; k++) {
                    b.s[k] = a.s[k] + (b.s[k] - a.s[k]) * t;
                    b.t[k] = a.t[k] + (b.t[k] - a.t[k]) * t;
                }
                float n = 0;
                for (int k = 0; k < 4; k++) {
                    b.q[k] = a.q[k] * sg * (1 - t) + b.q[k] * t;
                    n += b.q[k] * b.q[k];
                }
                n = std::sqrt(n);
                for (int k = 0; k < 4; k++) {
                    b.q[k] /= n;
                }
            }
            if (poseDirty) {
                fadeLeft -= 2.0f;
            }
        }
        // The partial slot: the carry clip replaces the pose of Spine1 and its descendants.
        // The action animation's override clip (0x181840, flag +0x14; the full-body throw) poses
        // every joint until it ends (0x181754 clears it).
        if (!overrideClip.empty() && clips.count(overrideClip)) {
            const PlayerClip& c = clips.at(overrideClip);
            const float f = std::min(overrideFrame, c.frames);
            for (size_t bi = 0; bi < target.size(); bi++) {
                float v[9];
                for (int k = 0; k < 9; k++) {
                    v[k] = c.bones[bi][k].evaluate(f, clipDefaults(ch, bi)[k]);
                }
                float q[4];
                eulerToQuat(v + 3, q);
                target[bi] = {{v[0], v[1], v[2]}, {q[0], q[1], q[2], q[3]}, {v[6], v[7], v[8]}};
            }
        } else if (!carry.upperClip.empty() && clips.count(carry.upperClip)) {
            if (upperMask.size() != ch.bones.size()) {
                upperMask.assign(ch.bones.size(), 0);
                for (size_t i = 0; i < ch.bones.size(); i++) {
                    const int p = ch.bones[i].parent;
                    upperMask[i] = ch.boneNames[i] == "Spine1" || (p >= 0 && upperMask[size_t(p)]);
                }
            }
            const PlayerClip& c = clips.at(carry.upperClip);
            const float f = std::min(carry.upperFrame, c.frames);
            for (size_t bi = 0; bi < target.size(); bi++) {
                if (!upperMask[bi]) {
                    continue;
                }
                float v[9];
                for (int k = 0; k < 9; k++) {
                    v[k] = c.bones[bi][k].evaluate(f, clipDefaults(ch, bi)[k]);
                }
                float q[4];
                eulerToQuat(v + 3, q);
                target[bi] = {{v[0], v[1], v[2]}, {q[0], q[1], q[2], q[3]}, {v[6], v[7], v[8]}};
            }
        }
        current = target;
        // The player's selector (0x43a408 / 0x29aee4 / 0x43a3b4 by resolver kind; tables read
        // in run hold): the shared clips (kinds 0 / 1) from the "Kinopio" lengths at scale 0.5, the
        // form's own clips (kind 2) from the form's lengths at scale 1.
        // The Super form: always scale 1 from the "Kinopio" lengths (its clips resolve to kind 2;
        // the loop runs every tick, exec watch on 0x294d90 at the opening and stage checkpoints and
        // at frame 1280 of hit2.tsv, where Spine1 / Spine2 are 5.0 / 4.83 in the local pose).
        if (form == 1) {
            retarget(ch, target, formClip ? 1.0f : 0.5f, formClip);
        } else {
            retarget(ch, target, 1.0f, false);
        }
        std::vector<Affine> local(target.size());
        for (size_t i = 0; i < target.size(); i++) {
            local[i] = localAffine(target[i]);
        }
        bool changed = local.size() != ch.localPose.size() || vis != ch.visibilityOverride;
        for (size_t i = 0; !changed && i < local.size(); i++) {
            changed = local[i].m != ch.localPose[i].m;
        }
        ch.localPose = std::move(local);
        ch.visibilityOverride = vis;

        // Placement.
        using namespace captain_toad::RecoveredVec;
        V3 side = cross(body.up, body.front);
        normalize(side);
        V3 up = cross(body.front, side);
        normalize(up);
        const float* m30 = body.mtx30;
        float R[9] = {side.x, up.x, body.front.x, side.y, up.y, body.front.y, side.z, up.z, body.front.z};
        Affine p;
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                p.m[r * 4 + c] =
                    (R[r * 3 + 0] * m30[0 * 4 + c] + R[r * 3 + 1] * m30[1 * 4 + c] + R[r * 3 + 2] * m30[2 * 4 + c]) *
                    body.scale;
            }
        }
        p.m[3] = body.pos.x;
        p.m[7] = body.pos.y;
        p.m[11] = body.pos.z;
        changed = changed || p.m != ch.placement.m;
        ch.placement = p;
        if (changed) {   // otherwise the last pose (and its skinned vertices) still hold
            ch.lastFrame = -1;
        }
        poseDirty = false;
    }

    // One-line status for the viewer's overlay.
    std::string status() const {
        std::string s = machine.current ? machine.current->name : "?";
        s += " - " + anim.action;
        if (!machine.blockedEver.empty()) {
            s += " - blocked:";
            for (auto& b : machine.blockedEver) {
                s += " " + b;
            }
        }
        if (!lastError.empty()) {
            s += " - stopped: " + lastError;
        }
        return s;
    }
};
