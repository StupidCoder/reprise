#include "game/HeroClips.hpp"

#include "anim/Pose.hpp"
#include "data/Animation.hpp"
#include "data/Hash.hpp"
#include "data/Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace beacon::game {

namespace {

void loadPunch(const data::ObjectDefinition& def, const data::GameFiles& files, const data::Skeleton& skeleton,
               const char* name, std::uint32_t handHash, HeroPunchClip& out) {
    out.name = name;
    const data::ObjectAnimation* a = def.animation(name);
    if (!a || a->clips.empty()) {
        return;
    }
    auto path = files.find(a->clips[0], ".anim");
    data::Bytes bytes;
    if (!path || !data::loadFile(*path, bytes)) {
        return;
    }
    auto clip = data::loadAnimation(bytes);
    if (!clip) {
        return;
    }
    const float speed = a->speed > 0 ? a->speed : 1;
    out.length = clip->duration / speed;
    out.rate = clip->sampleRate * speed;
    int hand = -1;
    for (std::size_t j = 0; j < skeleton.nameHash.size(); ++j) {
        if (skeleton.nameHash[j] == handHash) {
            hand = int(j);
        }
    }
    if (hand < 0) {
        return;
    }
    std::vector<data::JointPose> pose;
    std::vector<math::Mat4> world;
    for (std::uint32_t f = 0; f < clip->frameCount; ++f) {
        clip->sample(skeleton, float(f) / clip->sampleRate, pose);
        // The root-motion joint's travel is not drawn (the game moves the hero), so it does not
        // carry the hand either.
        for (std::size_t j = 0; j < pose.size(); ++j) {
            if (skeleton.nameHash[j] == 0x45279a6eu) {
                pose[j].translation = skeleton.basePose[j].translation;
            }
        }
        anim::jointWorldMatrices(skeleton, pose, world);
        const math::Mat4& m = world[std::size_t(hand)];
        out.hand.push_back({m[12], m[13], m[14]});
    }
}

// The root-motion joint's model-space path over a clip.
void loadMotion(const data::ObjectDefinition& def, const data::GameFiles& files, const data::Skeleton& skeleton,
                const char* name, HeroMotionClip& out) {
    const data::ObjectAnimation* a = def.animation(name);
    if (!a || a->clips.empty()) {
        return;
    }
    auto path = files.find(a->clips[0], ".anim");
    data::Bytes bytes;
    if (!path || !data::loadFile(*path, bytes)) {
        return;
    }
    auto clip = data::loadAnimation(bytes);
    if (!clip) {
        return;
    }
    const float speed = a->speed > 0 ? a->speed : 1;
    out.length = clip->duration / speed;
    out.rate = clip->sampleRate * speed;
    int root = -1;
    for (std::size_t j = 0; j < skeleton.nameHash.size(); ++j) {
        if (skeleton.nameHash[j] == 0x45279a6eu) {
            root = int(j);
        }
    }
    if (root < 0) {
        return;
    }
    std::vector<data::JointPose> pose;
    std::vector<math::Mat4> world;
    for (std::uint32_t f = 0; f < clip->frameCount; ++f) {
        clip->sample(skeleton, float(f) / clip->sampleRate, pose);
        anim::jointWorldMatrices(skeleton, pose, world);
        const math::Mat4& m = world[std::size_t(root)];
        out.root.push_back({m[12], m[13], m[14]});
    }
}

float clipLength(const data::ObjectDefinition& def, const data::GameFiles& files, const char* name) {
    const data::ObjectAnimation* a = def.animation(name);
    if (!a || a->clips.empty()) {
        return 0;
    }
    auto path = files.find(a->clips[0], ".anim");
    data::Bytes bytes;
    if (!path || !data::loadFile(*path, bytes)) {
        return 0;
    }
    auto clip = data::loadAnimation(bytes);
    return clip && a->speed > 0 ? clip->duration / a->speed : 0;
}

}   // namespace

math::Vec3 HeroPunchClip::handAt(float time) const {
    if (hand.empty()) {
        return {};
    }
    const float f = std::clamp(time * rate, 0.0f, float(hand.size() - 1));
    const std::size_t i = std::size_t(f);
    const std::size_t j = std::min(i + 1, hand.size() - 1);
    return math::lerp(hand[i], hand[j], f - float(i));
}

math::Vec3 HeroMotionClip::rootAt(float time) const {
    if (root.empty()) {
        return {};
    }
    const float f = std::clamp(time * rate, 0.0f, float(root.size() - 1));
    const std::size_t i = std::size_t(f);
    const std::size_t j = std::min(i + 1, root.size() - 1);
    return math::lerp(root[i], root[j], f - float(i));
}

HeroClipLengths loadHeroClipLengths(data::ObjectDefinitionLibrary& defs, const data::GameFiles& files) {
    HeroClipLengths c;
    if (const data::ObjectDefinition* def = defs.find("mup_hero")) {
        c.land = clipLength(*def, files, "jump_01_land");
        c.landRunning = clipLength(*def, files, "jump_02_land");
        c.landSlideFront = clipLength(*def, files, "landing_after_slide_front");
        c.landSlideBack = clipLength(*def, files, "landing_after_slide_back");
        c.waterslide = clipLength(*def, files, "waterslide");
        c.slideBelly = clipLength(*def, files, "slide_belly");
        c.fall = clipLength(*def, files, "jump_01_fall");
        c.kick = clipLength(*def, files, "kick");
        data::Bytes bytes;
        auto skelPath = files.find(def->skeleton.empty() ? def->model : def->skeleton, ".skel");
        std::optional<data::Skeleton> skeleton;
        if (skelPath && data::loadFile(*skelPath, bytes)) {
            skeleton = data::loadSkeleton(bytes);
        }
        if (skeleton) {
            const std::uint32_t right = data::fnv1a("hand_R"), left = data::fnv1a("hand_L");
            loadPunch(*def, files, *skeleton, "attack_01", right, c.punch[kPunchRightMoving]);
            loadPunch(*def, files, *skeleton, "punch_static_right", right, c.punch[kPunchRightStill]);
            loadPunch(*def, files, *skeleton, "attack_02", left, c.punch[kPunchLeftMoving]);
            loadPunch(*def, files, *skeleton, "punch_static_left", left, c.punch[kPunchLeftStill]);
            for (const char* name : {"damage_backward_in", "damage_backward_loop", "damage_backward_out", "damage_forward_in",
                                     "damage_forward_loop", "damage_forward_out", "damage_backward_light", "damage_forward_light",
                                     "damage_left_light", "damage_right_light", "death_spikes", "respawn", "respawn_somersault_out",
                                     "respawn_somersault_out_loop"}) {
                loadMotion(*def, files, *skeleton, name, c.motion[name]);
            }
        }
    }
    return c;
}

}   // namespace game
