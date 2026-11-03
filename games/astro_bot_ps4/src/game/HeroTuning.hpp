// The robot hero's tuning: the <Hero> block of data/multi_platformer/multi_platformer.xml, which
// the game binds by name into its hero parameter set. Only the values the port uses so far are
// listed; the defaults are the shipped values (used when a name is missing).
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace beacon::game {

struct HeroTuning {
    float scale = 0.22f;   // model scale
    float speed = 1.0f;    // m/s at full stick
    float accelLimitMin = 0.08f, accelLimitMax = 0.15f;   // max velocity change per update (m/s)
    float gravity = -9.0f;
    float jumpHeightMin = 0.25f, jumpHeightMax = 0.45f;
    float jumpableTimeAfterFalling = 0.15f;
    float controlOnJumpMin = 0.04f, controlOnJumpMax = 0.7f;   // air control factor per update
    float hoverTime = 1.2f, hoverTimeMin = 0.3f;
    float hoverVelocityOnStart = 0.4f;
    float hoverGravityStart = 0.4f, hoverGravityEnd = -5.0f;
    float walkAnimSpeedScale = 4.0f, runAnimSpeedScale = 1.2f;
    float animBlendStartSpeed = 0.1f, animBlendEndSpeed = 1.0f;   // built-in defaults (not in the XML)
    float climbableStepHeight = 0.04f;
    float slopeMaxAngle = 80.0f;   // degrees
    // Slide surfaces (IsSlope): steeper than SlopeMinAngle starts a slide, which goes on down to
    // SlopeMinAngleOnSliding; the slide's acceleration and side control (0x688d60).
    float slopeMinAngle = 15.0f, slopeMinAngleOnSliding = 3.0f;   // degrees
    float slopeSideFriction = 3.0f, slopeSideControl = 5.0f, slopeAccel = 5.0f;
    float tiltSmoothRatio = 0.08f, tiltScale = 3.0f, tiltMaxAngle = 30.0f;
    float collisionYOffset = 0.12f, collisionHeight = 0.05f, collisionRadius = 0.08f;
    float collisionForWallYOffset = 0.14f, collisionForWallHeight = 0.05f, collisionForWallRadius = 0.06f;
    float enemySensorRadius = 0.3f, enemySensorHeight = 0.1f;   // EnemySensorCollision* (binder defaults)
    float minStickInput = 0.07f;
    // <Punch> (values at +0x3070, +0x3120, +0x31d0, +0x3280, +0x3540 of the parameter object).
    float punchNonCancelableTime = 0.25f;   // a press earlier than this after a punch is kept for later
    float punchCollisionStartTime = 0.1f;   // the hand sweep's window after the punch starts
    float punchCollisionTime = 0.15f;
    float punchCollisionRadius = 0.06f;
    float punchAnimCancelableTime = 0.3f;   // the stick or a landing may end the punch after this
    // <Damage> (parameter object +0x5690 .. +0x6df0): how long a knocked-down hero stays down by
    // the hit's weight, the push of a plain hit, the invincibility after getting up.
    float lightDamageDownTime = 0.5f, middleDamageDownTime = 1.0f, heavyDamageDownTime = 1.5f;
    float damageVelocityXZ = 0.5f, damageVelocityY = 2.0f;
    float invincibleTimeAfterKnockDown = 0.3f;
    // <Disp> (parameter object +0x8d50 .. +0x92e0, handed to the renderer by 0x695790): the
    // shadow kind (Decal = 1) and the decal's colour (a = strength), half height, fade start and
    // lift.
    bool decalShadow = true;
    float decalShadowColor[4] = {0, 0, 0, 0.7f};
    float decalShadowHeight = 0.05f, decalShadowFadeHeightRatio = 0.5f, decalShadowOffsetY = 0;
};

// Reads <MultiPlatformer><Hero> from the game folder `root`; nullopt if the file is unreadable.
std::optional<HeroTuning> loadHeroTuning(const std::filesystem::path& root, std::string* error = nullptr);

}   // namespace game
