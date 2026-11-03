// Lengths of the hero animations whose end the state logic waits for, read from the hero's
// object definition (mup_hero) and its .anim clips: the game ends these states when the clip has
// played through (Land 0x4cc150: animation finished -> Idle).
#pragma once

#include "data/GameFiles.hpp"
#include "data/ObjectDefinition.hpp"
#include "math/Transform.hpp"

#include <map>
#include <string>
#include <vector>

namespace beacon::game {

// A punch clip and the swinging hand's path: the joint's model-space position (unscaled, facing
// +Z) at each frame of the clip. The punch sweeps the hand's joint (hand_R at +0x24 / hand_L at
// +0x20 of the hero's model object, bound by name in 0x695790).
struct HeroPunchClip {
    const char* name = "";
    float length = 0;   // seconds at the definition's playback speed
    float rate = 60;    // frames per second of `hand` (playback speed applied)
    std::vector<math::Vec3> hand;
    math::Vec3 handAt(float time) const;
};

// A clip that moves the hero by its root motion (0x695020: the root-motion joint's travel over the
// update, through the model's frame): the joint's model-space position (unscaled, facing +Z) per
// frame. Damage (11), LightDamage (12) and DeathOnSpike (59) play these.
struct HeroMotionClip {
    float length = 0;   // seconds at the definition's playback speed
    float rate = 60;    // frames per second of `root` (playback speed applied)
    std::vector<math::Vec3> root;
    math::Vec3 rootAt(float time) const;
};

// The punch clips by hand and stance (Punch enter 0x4cf690): moving or standing still.
enum HeroPunchClipId { kPunchRightMoving, kPunchRightStill, kPunchLeftMoving, kPunchLeftStill, kPunchClipCount };

struct HeroClipLengths {
    float land = 0;          // jump_01_land (seconds, at the definition's playback speed)
    float landRunning = 0;   // jump_02_land, after a jump started faster than 0.5 m/s
    float landSlideFront = 0, landSlideBack = 0;   // landing_after_slide_front / _back
    float waterslide = 0, slideBelly = 0, fall = 0;  // waterslide, slide_belly, jump_01_fall
    float kick = 0;          // kick (Kick enter 0x4dc480, clip 0xc61c131f)
    HeroPunchClip punch[kPunchClipCount];
    std::map<std::string, HeroMotionClip> motion;   // by clip name (damage_*, death_spikes)
    const HeroMotionClip* motionClip(const std::string& name) const {
        const auto it = motion.find(name);
        return it == motion.end() ? nullptr : &it->second;
    }
};

// Missing clips leave 0 (the state then ends at its first chance).
HeroClipLengths loadHeroClipLengths(data::ObjectDefinitionLibrary& defs, const data::GameFiles& files);

}   // namespace game
