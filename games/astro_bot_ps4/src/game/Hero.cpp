#include "game/Hero.hpp"

#include "game/LevelDocument.hpp"

#include "math/Vector.hpp"

#include <algorithm>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace beacon::game {

using namespace math;

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kFallStartSpeed = 0.1f;       // Jump -> Fall below this vertical speed (0x11f4b74)
constexpr float kHoverAfterJump = 0.1f;       // a new press starts hovering after this (0x11f206c)
constexpr float kPunchJumpCancelTime = 0.07f; // a jump may end a punch after this (0x11f209c)
constexpr int kSlideIterations = 6;             // collide-and-slide depth (0x66dd40)
constexpr float kSlideTouch = 0.001f;          // touching at the start / push-out skin (0x1222234)
constexpr float kSlideNudge = 0.001f;          // nudge off a contact on the capsule's lower cap (0x12210e0)
constexpr float kGroundProbeLength = 0.2f;     // extra probe reach on a moving support (0x12221f4)
constexpr float kEdgeAhead = 0.1f;            // drop-edge samples ahead / aside (0x1221180)
constexpr float kEdgeDepth = 5.0f;            // drop-edge sample cast depth (0x1221190)
constexpr float kEdgeSampleRadius = 0.02f;    // (0x1222248)
constexpr float kEdgeFaceDrop = 0.02f;        // face cast below the feet (0x1222250)
constexpr float kEdgeFaceRadius = 0.01f;      // (0x1222254)
constexpr float kEdgeNormalMin = 0.001f;      // (0x1222258)
constexpr float kEdgeInside = 0.04f;          // edge line inside the face (0x122225c)
constexpr float kTeeterBand = -0.05f;         // teetering wall: distance band behind the plane (0x12222a0)
constexpr float kTeeterStopDot = 0.9f;        // stop when the stick meets the wall this head-on (0x12222a4)
constexpr float kTeeterMinTime = 0.2f;        // Teeter: earliest leaving (0x11f2124)
constexpr float kTeeterLeaveDot = 0.7f;       // Teeter: leave when the stick points less towards the edge (0x11f2128)
constexpr float kTeeterProbe = 0.1f;          // Teeter: way check distance (0x11f2be0)
constexpr float kSlopeProbeReach = 1.5f;       // downhill check reach, times k dt (0x1222210)
constexpr float kRunningJumpSpeed = 0.5f;      // faster jumps land with jump_02_land (0x11f203c)
constexpr float kLandToWalkTime = 0.06f;       // Land hands over to Walk after this (0x11f2078)
constexpr float kLandToSlideTime = 0.1f;       // ... and to Slide on a slide surface (0x11f207c)
constexpr float kSlopeDirMin = 0.01f;          // shortest downhill vector of a slide surface (0x12221fc)
// Slide state (0x4d8530): off the slope, the landing clip after 0.2 s, Fall after 0.4 s in the
// air, Walk with the stick after 0.4 s, Idle after 0.3 s once the clip has played; on flat
// ground the velocity keeps 0.9 of itself each update (0x11f21f0 / f4 / f8, 0x11f2e70).
constexpr float kSlideLandClipTime = 0.2f, kSlideFallTime = 0.4f, kSlideWalkTime = 0.4f, kSlideIdleTime = 0.3f;
constexpr float kSlideBrake = 0.9f;
// The hero's query masks (0x66dd40 / 0x66ab90 / 0x673f50): its own body bits +0x160 (1 for the
// first player, 0x667200) and the target masks without them.
constexpr std::uint32_t kHeroSelf = 1;
const physics::rigid::QueryFilter kWallFilter{kHeroSelf, 0xd8241a1fu & ~kHeroSelf};
const physics::rigid::QueryFilter kGroundFilter{kHeroSelf, 0xc804091fu & ~kHeroSelf};
const physics::rigid::QueryFilter kEdgeFilter{kHeroSelf, 0xc844191fu & ~kHeroSelf};
// The rays: downhill (0x66ab90 at 0x66bcd1) with the hero's bits; the drop edge's look ahead
// (0x673f50 at 0x674039: self left at all ones, 0xc226a0; target without the hero's bits and those
// of the bodies it carries, list +0x230: none here); the teetering exit's wall and ground rays
// (0x4cf230 at 0x4d31b1 / 0x4d3215: both masks as written, self all ones).
const physics::rigid::QueryFilter kDownhillRay{kHeroSelf, 0xc804191fu & ~kHeroSelf};
const physics::rigid::QueryFilter kEdgeAheadRay{0xffffffffu, 0xd8241a1fu & ~kHeroSelf};
const physics::rigid::QueryFilter kTeeterWallRay{0xffffffffu, 0xd8241a1fu};
const physics::rigid::QueryFilter kTeeterGroundRay{0xffffffffu, 0xc804191fu};
// The drop edge's face cast (0x66eb30 at 0x66f2ea): the hero's bits, 0xc804191f.
const physics::rigid::QueryFilter kEdgeFaceFilter{kHeroSelf, 0xc804191fu & ~kHeroSelf};
constexpr float kUprightSin = 0.70710678f;
constexpr float kCeilingNormalY = -0.7f;      // a wall-capsule hit facing down (0x122223c)    // sin 45 degrees: the wall capsule's quaternion
constexpr float kGroundProbeRadius = 0.01f;    // ground probe (0x66ab90, 0x12221fc)
constexpr float kGroundProbeStartScale = 0.5f; // its start above the feet, times the model scale (0x12221f8)

float wrapAngle(float a) {
    a = std::fmod(a + kPi, 2 * kPi);
    if (a < 0) {
        a += 2 * kPi;
    }
    return a - kPi;
}

}   // namespace

void setHeroStick(HeroInput& in, float padX, float padY, const Vec3& right, const Vec3& forward,
                  float minStickInput) {
    const float sx = std::fabs(padX) < minStickInput ? 0.0f : padX;
    const float sy = std::fabs(padY) < minStickInput ? 0.0f : padY;
    Vec3 w = right * sx - forward * sy;   // pad y grows downwards: up is forward
    w.y = 0;
    const float len = length(w);
    if (len > 1) {
        w = w * (1 / len);
    }
    in.stickX = w.x;
    in.stickZ = w.z;
}

const char* heroStateName(HeroState s) {
    switch (s) {
    case HeroState::Idle:
        return "Idle";
    case HeroState::Walk:
        return "Walk";
    case HeroState::Jump:
        return "Jump";
    case HeroState::Fall:
        return "Fall";
    case HeroState::Land:
        return "Land";
    case HeroState::Hover:
        return "Hover";
    case HeroState::Slide:
        return "Slide";
    case HeroState::Teeter:
        return "Teeter";
    case HeroState::Punch:
        return "Punch";
    case HeroState::Kick:
        return "Kick";
    case HeroState::Damage:
        return "Damage";
    case HeroState::LightDamage:
        return "LightDamage";
    case HeroState::DeathOnSpike:
        return "DeathOnSpike";
    case HeroState::Respawn:
        return "Respawn";
    }
    return "?";
}

void Hero::spawn(const Vec3& feet, float yaw, bool standing) {
    pos_ = feet;
    vel_ = {};
    yaw_ = yaw;
    tilt_ = 0;
    grounded_ = false;
    airTime_ = 0;
    hoverAvailable_ = false;
    grounded_ = standing;
    hitRequest_ = false;
    damageable_ = lightDamageable_ = true;
    invincible_ = false;
    collisionOff_ = hidden_ = died_ = false;
    changeState(standing ? HeroState::Idle : HeroState::Fall);
}

float Hero::horizontalSpeed() const {
    return std::sqrt(vel_.x * vel_.x + vel_.z * vel_.z);
}

float Hero::stickAmount(const HeroInput& in) const {
    const float m = std::sqrt(in.stickX * in.stickX + in.stickZ * in.stickZ);
    return m < 1.19e-7f ? 0.0f : std::min(m, 1.0f);   // "no stick" below FLT_EPSILON (0x667b20)
}

void Hero::changeState(HeroState next) {
    if (state_ == HeroState::Punch) {
        windowOpen_ = false;   // Punch exit (0x4cfce0 -> 0x674c70)
    }
    exitState(next);
    if (next != HeroState::Jump) {
        boardJump_ = false;   // the board's flags last one jump (Jump enter clears +0x82)
    }
    state_ = next;
    stateTime_ = 0;
    if (next == HeroState::Teeter) {
        vel_ = {};   // 0x4d2f50
    } else if (next == HeroState::Jump) {
        // Enter (0x4caf20): launch at sqrt(-2 g JumpHeightMin); the body step of the same update
        // then applies one step of gravity (traced: first airborne row v0 - 9/60).
        jumpV0_ = std::sqrt(-2 * t_.gravity * t_.jumpHeightMin);
        vel_.y = jumpV0_;
        jumpHolding_ = true;
        grounded_ = false;
        hoverAvailable_ = true;   // +0x81
        jumpClip_ = "jump_01_up";
        if (boardJump_) {
            // A board's jump (+0x82): the stored speed straight up, no held-jump lift (+0x44 = 0),
            // and the board's clip (0x688aa0).
            vel_ = {0, boardSpeed_, 0};
            jumpHolding_ = false;
            if (boardFunny_) {
                // (previous + random 1..3) mod 3: never the same clip twice in a row.
                bumperRandom_ = bumperRandom_ * 1664525u + 1013904223u;
                bumperClip_ = (bumperClip_ + 1 + int((bumperRandom_ >> 16) % 3u)) % 3;
                static const char* const kFunny[3] = {"jump_bumper_02", "jump_bumper_03", "jump_bumper_04"};
                jumpClip_ = kFunny[bumperClip_];
            } else {
                jumpClip_ = "jump_bumper_01";
            }
        }
    } else if (next == HeroState::Slide) {
        enterSlide();
    } else if (next == HeroState::Damage) {
        enterDamage();
    } else if (next == HeroState::LightDamage) {
        enterLightDamage();
    } else if (next == HeroState::DeathOnSpike) {
        // Enter (0x4db660): no more hits (0x686550: +0x108 off, +0x1b8 on, the carried object
        // dropped), the foot body meets nothing (0x66fa60 mode 2), death_spikes (clip 0xf511719),
        // standing still.
        damageable_ = false;
        collisionOff_ = true;
        spikePhase_ = 0;
        spikeWait_ = 0;
        playAction("death_spikes");
        vel_ = {};
    } else if (next == HeroState::Hover) {
        // Enter (0x4cc440): uses up the hover of this airborne phase; rise at least at
        // HoverVelocityOnStart, a faster jump keeps its momentum until it drops below that speed.
        hoverAvailable_ = false;
        ++hoverCount_;
        hoverRising_ = vel_.y >= t_.hoverVelocityOnStart;
        if (!hoverRising_) {
            vel_.y = t_.hoverVelocityOnStart;
        }
    }
}

// Ground movement (0x683ed0): the change of velocity along the current direction is limited to
// lerp(AccelLimitMin, AccelLimitMax, |stick|) per update; starting and turning are immediate.
void Hero::groundControl(const HeroInput& in) {
    const float amount = stickAmount(in);
    const float sx = amount > 0 ? in.stickX : 0, sz = amount > 0 ? in.stickZ : 0;
    const Vec3 target{t_.speed * sx, 0, t_.speed * sz};
    const Vec3 v{vel_.x, 0, vel_.z};
    Vec3 d = target - v;
    const float limit = t_.accelLimitMin + (t_.accelLimitMax - t_.accelLimitMin) * amount;
    const float vv = dot(v, v);
    if (vv > 0) {
        const float projLen = std::fabs(dot(v, d)) / std::sqrt(vv);
        if (limit < projLen) {
            d *= limit / projLen;
        }
    }
    const float oldHeading = std::atan2(v.x, v.z);
    vel_.x += d.x;
    vel_.z += d.z;
    // Lean into turns: wrapped heading change scaled and clamped (the game's tilt), smoothed.
    float tiltTarget = 0;
    if (vv > 0 && (vel_.x != 0 || vel_.z != 0)) {
        const float maxTilt = t_.tiltMaxAngle * kPi / 180;
        tiltTarget = std::clamp(wrapAngle(std::atan2(vel_.x, vel_.z) - oldHeading) * t_.tiltScale, -maxTilt, maxTilt);
    }
    tilt_ += (tiltTarget - tilt_) * t_.tiltSmoothRatio;
}

// Air movement (0x684580): v.xz moves toward Speed * stick by lerp(ControlOnJumpMin, Max, |stick|).
void Hero::airControl(const HeroInput& in) {
    const float amount = stickAmount(in);
    const float sx = amount > 0 ? in.stickX : 0, sz = amount > 0 ? in.stickZ : 0;
    const float k = t_.controlOnJumpMin + (t_.controlOnJumpMax - t_.controlOnJumpMin) * amount;
    vel_.x += (t_.speed * sx - vel_.x) * k;
    vel_.z += (t_.speed * sz - vel_.z) * k;
    tilt_ += (0 - tilt_) * t_.tiltSmoothRatio;
}

// First request of an update wins (0x6a7350 ignores later ones).
void Hero::boardLaunch(float speed, bool funny) {
    boardRequest_ = true;
    boardSpeed_ = speed;
    boardFunny_ = funny;
}

void Hero::request(HeroState next) {
    if (!hasRequest_) {
        requested_ = next;
        hasRequest_ = true;
    }
}

void Hero::update(const HeroInput& in, const physics::PhysicsWorld& world) {
    // The state machine (0x6a77a0): the current state's update runs and may ask for another
    // state; the switch (exit, then the new state's enter) follows in the same update, the new
    // state's own update from the next one. The body step comes after the machine.
    const float dt = kStep;
    stateTime_ += dt;
    hasRequest_ = false;
    sweeps_.clear();
    kickSweep_.reset();
    punchPressed_ = in.punchPressed;
    kickable_ = in.kickable;
    if (invincible_) {
        // **[hypothesis]** the invincibility runs out after its time (+0x318 counts to +0x31c).
        invincibleTime_ += dt;
        if (invincibleTime_ >= invincibleFor_) {
            invincible_ = false;
        }
    }
    actionTime_ += dt;
    if (hitRequest_) {
        // A hit since the last update (0x6a7370 with force): its state comes first; the current
        // state's update still runs, its own requests are ignored.
        hitRequest_ = false;
        request(hitState_);
    }
    if (boardRequest_) {
        // A jump board asked during the objects' update (0x687c90 -> 0x6a7370 Jump).
        boardRequest_ = false;
        boardJump_ = true;
        request(HeroState::Jump);
    }
    if (punchRequest_) {
        // Asked for by the attack controller after the last update; the first request wins.
        punchRequest_ = false;
        request(kickRequest_ ? HeroState::Kick : HeroState::Punch);
    }
    {
        const float m = std::sqrt(in.stickX * in.stickX + in.stickZ * in.stickZ);
        stickDir_ = m > 1.19e-7f ? Vec3{in.stickX / m, 0, in.stickZ / m} : Vec3{};
    }
    switch (state_) {
    case HeroState::Idle:
        // Idle (0x4c8c60) does not move the hero: it only asks for the next state.
        if (!grounded_) {
            hoverAvailable_ = true;   // walking off an edge still allows one hover
            request(HeroState::Fall);
        } else if (onSlope_) {
            request(HeroState::Slide);
        } else if (in.jumpPressed) {
            runningJump_ = false;
            request(HeroState::Jump);
        } else if (stickAmount(in) > 0) {
            request(HeroState::Walk);
        } else if (groundState_ == 2) {
            request(HeroState::Teeter);   // stopped at an edge without stick (0x4c8c60)
        }
        break;
    case HeroState::Walk:
        // Walk (0x4c9b10): ground movement, then jump, coyote-time fall, or Idle once at rest.
        groundControl(in);
        if (in.jumpPressed) {
            runningJump_ = horizontalSpeed() > kRunningJumpSpeed;
            request(HeroState::Jump);
        } else if (onSlope_) {
            request(HeroState::Slide);   // 0x4ca54f
        } else if (groundState_ == 2) {
            request(HeroState::Teeter);   // the last body step stopped the hero at an edge
        } else if (!grounded_ && airTime_ > t_.jumpableTimeAfterFalling) {
            hoverAvailable_ = true;
            request(HeroState::Fall);   // coyote time over
        } else if (horizontalSpeed() < 1.19e-7f) {
            request(HeroState::Idle);
        }
        break;
    case HeroState::Land: {
        // Land (0x4cc150): ground movement while grounded; jump; with the stick held after
        // 0.06 s, Walk; otherwise Idle once the landing clip (jump_02_land after a running jump,
        // else jump_01_land) has played.
        if (grounded_) {
            groundControl(in);
        }
        const float clip = runningJump_ ? clips_.landRunning : clips_.land;
        if (in.jumpPressed && grounded_) {
            runningJump_ = horizontalSpeed() > kRunningJumpSpeed;
            request(HeroState::Jump);
        } else if (stateTime_ > kLandToWalkTime && stickAmount(in) > 0 && grounded_) {
            request(HeroState::Walk);
        } else if (stateTime_ > kLandToSlideTime && onSlope_) {
            request(HeroState::Slide);   // 0x4cc2cc
        } else if (stateTime_ >= clip) {   // also when airborne: Idle then asks for Fall
            request(HeroState::Idle);
        }
        break;
    }
    case HeroState::Teeter: {
        // Teetering (0x4d2fd0): jump -> Jump; after 0.2 s, a stick pointing less than 0.7 towards
        // the edge leaves when the way is free: no wall within 0.1 m that way (from half the
        // scale up) and ground within 5 m below that point; then ground movement and Walk.
        if (in.jumpPressed) {
            runningJump_ = false;
            request(HeroState::Jump);
            break;
        }
        if (onSlope_) {
            request(HeroState::Slide);   // 0x4d328a
            break;
        }
        if (stickAmount(in) > 0 && stateTime_ > kTeeterMinTime && dot(teeterDir_, stickDir_) < kTeeterLeaveDot) {
            const Vec3 from = pos_ + Vec3{0, t_.scale * 0.5f, 0};
            const Vec3 to = from + stickDir_ * kTeeterProbe;
            const bool blocked = world.castRay(from, from + stickDir_ * kTeeterProbe, physics::kHitHero, kTeeterWallRay).has_value();
            if (!blocked && world.castRay(to, to - Vec3{0, 5.0f, 0}, physics::kHitHero, kTeeterGroundRay)) {
                groundControl(in);
                request(HeroState::Walk);
            }
        }
        break;
    }
    case HeroState::Jump:
        airControl(in);
        if (!in.jumpHeld) {
            jumpHolding_ = false;
        }
        if (vel_.y > 0 && jumpHolding_) {
            // Reduced gravity while held so the apex reaches JumpHeightMax (0x4cb530).
            vel_.y += dt * (jumpV0_ * jumpV0_ * -0.5f / t_.jumpHeightMax - t_.gravity);
        }
        // Jump (0x4cb530): Hover after 0.1 s on a new press (unless blocked: +0x100, not ported),
        // then Land when grounded, Fall below 0.1 m/s.
        if (stateTime_ > kHoverAfterJump && in.jumpPressed) {
            request(HeroState::Hover);
        } else if (onSlope_) {
            request(HeroState::Slide);   // 0x4cb70d
        } else if (grounded_) {
            request(HeroState::Land);
        } else if (vel_.y < (boardJump_ ? -1.0f : kFallStartSpeed)) {   // +0x51: -1.0 after a board
            request(HeroState::Fall);
        }
        break;
    case HeroState::Fall:
        // Fall (0x4cb940): Land when grounded (a hard-landing event above 1.5 m of fall is not
        // ported); a press hovers if this airborne phase still has its hover (+0x81).
        airControl(in);
        if (onSlope_) {
            request(HeroState::Slide);   // 0x4cbca0
        } else if (grounded_) {
            request(HeroState::Land);
        } else if (in.jumpPressed && hoverAvailable_) {
            request(HeroState::Hover);
        }
        break;
    case HeroState::Punch: {
        // Punch (0x4cfcf0): ground or air movement; a press within NonCancelableTime is kept for
        // the next punch; jump after 0.07 s (or when the punch came from a kept press) -> Jump;
        // after AnimCancelableTime: at an edge -> Teetering, landed from an air start -> Land,
        // stick -> Walk; when the clip has played -> Idle (Fall in the air, Teetering at an edge).
        // The flag +0x4a (cleared on release, then grounded -> Idle) is never set by the enter;
        // its start value is not known and taken as off (hypothesis).
        if (grounded_) {
            groundControl(in);
        } else if (groundState_ != 2) {
            airControl(in);
        }
        if (stateTime_ < t_.punchNonCancelableTime && in.punchPressed) {
            punchBuffered_ = true;
        }
        if (groundState_ == 2) {
            punchAtEdge_ = true;
        }
        if ((stateTime_ > kPunchJumpCancelTime || punchBufferedAtStart_) && in.jumpPressed) {
            punchBuffered_ = false;
            runningJump_ = false;
            request(HeroState::Jump);
            break;
        }
        if (stateTime_ > t_.punchAnimCancelableTime) {
            if (punchAtEdge_) {
                request(HeroState::Teeter);
            } else if (grounded_ && punchFromAir_) {
                request(HeroState::Land);
            } else if (stickAmount(in) > 0 && grounded_) {
                request(HeroState::Walk);
            }
        }
        if (stateTime_ >= clips_.punch[punchClip_].length) {   // the clip has played (0x694fd0)
            request(punchAtEdge_ ? HeroState::Teeter : grounded_ ? HeroState::Idle : HeroState::Fall);
        }
        break;
    }
    case HeroState::Hover: {
        airControl(in);
        // Rising from a faster jump: plain gravity; the update that finds the speed below
        // HoverVelocityOnStart still has no hover gravity (run roof_box1, 4113-4115: 0.3547,
        // 0.2047, then +dt x hover gravity).
        const bool rising = hoverRising_;
        if (hoverRising_ && vel_.y < t_.hoverVelocityOnStart) {
            hoverRising_ = false;
        }
        const bool keep = (in.jumpHeld || stateTime_ < t_.hoverTimeMin) && stateTime_ <= t_.hoverTime;
        if (!keep) {
            request(HeroState::Fall);   // 0x4ccee0: always Fall, which lands next update
            break;
        }
        if (!rising) {
            const float s = stateTime_ / t_.hoverTime;
            const float g = s * t_.hoverGravityEnd + (1 - s) * t_.hoverGravityStart;
            vel_.y += dt * (g - t_.gravity);   // body step adds Gravity back
        }
        if (onSlope_) {
            request(HeroState::Slide);   // 0x4cd18d
        } else if (grounded_) {
            request(HeroState::Land);
        }
        break;
    }
    case HeroState::Slide:
        updateSlide(in);
        break;
    case HeroState::Kick:
        updateKick(in);
        break;
    case HeroState::Damage:
        updateDamage(in);
        break;
    case HeroState::LightDamage:
        updateLightDamage(in);
        break;
    case HeroState::DeathOnSpike:
        updateDeathOnSpike();
        break;
    case HeroState::Respawn:
        if (respawnPhase_ == 0) {
            // Placed once the VR player no longer warps (0xa641d0).
            if (!vrBusy_) {
                pos_ = respawnAt_;
                vel_ = {};
                yaw_ = kPi;   // 0x66db40 with (0, 0, -1)
                hidden_ = false;
                playAction("respawn");
                respawnTimer_ = 0;
                respawnPhase_ = 1;
            }
        } else if (respawnPhase_ == 1) {
            respawnTimer_ += dt;
            if (respawnEffectTime_ < respawnTimer_) {
                playAction("respawn_somersault_out");
                respawnPhase_ = 2;
            }
        } else if (respawnPhase_ == 2) {
            if (actionDone()) {
                playAction("respawn_somersault_out_loop");
                collisionOff_ = false;   // 0x66fa60 mode 0
                setInvincible(respawnInvincible_);
                respawnPhase_ = 3;
            }
        } else if (grounded_) {
            // Land (in water Idle, not ported); invincible again from the landing.
            request(HeroState::Land);
            setInvincible(respawnInvincible_);
        }
        break;
    }
    if (hasRequest_) {
        changeState(requested_);
        if (state_ == HeroState::Punch) {
            enterPunch(in);
        } else if (state_ == HeroState::Kick) {
            // Kick enter (0x4dc480): the kick clip; a kept press is dropped.
            punchBuffered_ = false;
            kickLanded_ = false;
        }
    }
    attackController();
    updateAttackWindow();
    // The slide sets its own facing; the damage states keep the facing their enter gave
    // (**[hypothesis]**: the pushes do not turn the hero).
    const bool ownFacing = state_ == HeroState::Slide || state_ == HeroState::Damage || state_ == HeroState::Respawn ||
                           state_ == HeroState::LightDamage || state_ == HeroState::DeathOnSpike;
    if (horizontalSpeed() > 1.19e-7f && !ownFacing) {
        yaw_ = std::atan2(vel_.x, vel_.z);
    }
    if (state_ == HeroState::Respawn && respawnPhase_ < 3) {
        // Held where it was placed (hidden while waiting): no gravity until the somersault ends.
        vel_ = {};
        grounded_ = false;
    } else {
        bodyStep(world);
    }
    if (grounded_) {
        airTime_ = 0;
    } else {
        airTime_ += dt;
    }
}

// Slide enter (0x4d8190): facing uphill (the facing and the horizontal downhill direction pointing
// apart) is a belly slide, backwards (+0x44, clip slide_belly), else waterslide; the slide's
// velocity starts as the hero's, laid onto the surface and without a part going up the slope.
void Hero::enterSlide() {
    Vec3 dh{slopeDir_.x, 0, slopeDir_.z};
    const float len = std::sqrt(dot(dh, dh));
    if (len >= 1.1754944e-38f) {
        dh = dh * (1 / len);
    }
    const Vec3 facing{std::sin(yaw_), 0, std::cos(yaw_)};
    slideBackwards_ = dot(facing, dh) < 0;
    playSlideClip(slideBackwards_ ? "slide_belly" : "waterslide", slideBackwards_ ? clips_.slideBelly : clips_.waterslide);
    Vec3 v = vel_ - slopeNormal_ * dot(vel_, slopeNormal_);
    const float along = dot(slopeDir_, v);
    if (along < 0) {
        v = v - slopeDir_ * along;
    }
    slideVel_ = v;
    slideOffTime_ = 0;
}

void Hero::playSlideClip(const char* name, float length) {
    slideClip_ = name;
    slideClipLength_ = length;
    slideClipTime_ = 0;
}

// Slide update (0x4d8530). On the slope (+0x14c == 3) the slide moves (0x688d60): the hero faces
// down the slope (up it when backwards); the slide's velocity is laid onto the surface, then
//   v += dt (-SlopeSideFriction s + SlopeAccel (-d.y) d + SlopeSideControl (stick - d (stick . d)))
// with d the downhill direction and s = v - d (v . d) the sideways part; that is the hero's
// velocity. A jump press jumps. In the air the time runs (Fall after 0.4 s; a jump within
// JumpableTimeAfterFalling). On flat ground the hero brakes (0.9 per update), the landing clip
// follows after 0.2 s, then Walk with the stick after 0.4 s or Idle after 0.3 s once the clip has
// played. (Exit 0x4d8510 sends event 0x38e1d2d3 to the hero: not ported.)
void Hero::updateSlide(const HeroInput& in) {
    const float dt = kStep;
    slideClipTime_ += dt;
    if (groundState_ == 3) {
        Vec3 dh{slopeDir_.x, 0, slopeDir_.z};
        const float len = std::sqrt(dot(dh, dh));
        if (len > 1.1754944e-38f) {
            dh = dh * (1 / len);
        }
        if (slideBackwards_) {
            dh = dh * -1.0f;
        }
        yaw_ = std::atan2(dh.x, dh.z);
        const Vec3& n = slopeNormal_;
        const Vec3& d = slopeDir_;
        Vec3 v = slideVel_ - n * dot(slideVel_, n);
        const Vec3 side = v - d * dot(v, d);
        const Vec3 stick{in.stickX, 0, in.stickZ};   // hero +0x110 / +0x114
        const Vec3 stickSide = stick - d * dot(stick, d);
        v = v + (side * -t_.slopeSideFriction + d * (-d.y * t_.slopeAccel) + stickSide * t_.slopeSideControl) * dt;
        slideVel_ = v;
        vel_ = v;
        slideOffTime_ = 0;
        if (in.jumpPressed) {
            request(HeroState::Jump);
        }
        return;
    }
    if (groundState_ == 0) {
        slideOffTime_ += dt;
        if (slideOffTime_ > kSlideFallTime) {
            playSlideClip("jump_01_fall", clips_.fall);
            request(HeroState::Fall);
        }
        if (in.jumpPressed && slideOffTime_ < t_.jumpableTimeAfterFalling) {
            request(HeroState::Jump);
        }
        return;
    }
    if (groundState_ != 1) {
        return;   // stopped at an edge (2): nothing this update
    }
    slideOffTime_ += dt;
    vel_ = Vec3{vel_.x, 0, vel_.z} * kSlideBrake;
    slideVel_ = vel_;
    if (slideOffTime_ > kSlideLandClipTime) {
        if (std::string_view(slideClip_) == "waterslide") {
            playSlideClip("landing_after_slide_front", clips_.landSlideFront);
        } else if (std::string_view(slideClip_) == "slide_belly") {
            playSlideClip("landing_after_slide_back", clips_.landSlideBack);
        }
    }
    if (in.jumpPressed) {
        request(HeroState::Jump);
    } else if (stickAmount(in) <= 0 || slideOffTime_ <= kSlideWalkTime) {
        const bool clipDone = slideClipTime_ >= slideClipLength_;   // 0x694fd0: the clip has played
        if (slideOffTime_ > kSlideIdleTime && clipDone) {
            request(HeroState::Idle);
        }
    } else {
        request(HeroState::Walk);
    }
}

// The state machine's post-update hook (0x684950), punch part: a square press (or a kept one)
// more than NonCancelableTime after the last punch starts a punch with the other hand, from the
// states of the table at 0x1227044 (Idle, Walk, Jump, Fall, Land, Hover, Punch, and states not
// ported) when on the ground and not in Jump; asked for now, applied in the next update. A kept
// press is dropped in other states than Punch / JumpPunch / 51. Air punches (state 15, JumpPunch
// from the jump states) and water are not ported.
void Hero::attackController() {
    sincePunch_ += kStep;
    if (punchBuffered_ && state_ != HeroState::Punch && state_ != HeroState::Kick) {
        punchBuffered_ = false;
    }
    if (!punchPressed_ && !punchBuffered_) {
        return;
    }
    // Only from the states of the table at 0x1227044 {1, 2, 27, 3, 4, 8, 5, 6, 9, 48, 51}: not
    // while teetering, sliding or damaged.
    const bool fromState = state_ == HeroState::Idle || state_ == HeroState::Walk || state_ == HeroState::Jump ||
                           state_ == HeroState::Fall || state_ == HeroState::Land || state_ == HeroState::Hover ||
                           state_ == HeroState::Punch || state_ == HeroState::Kick;
    if (sincePunch_ <= t_.punchNonCancelableTime || !fromState) {
        return;
    }
    if (!grounded_ || state_ == HeroState::Jump) {
        return;
    }
    sincePunch_ = 0;
    punchHand_ = punchHand_ + 1 == 2 ? 0 : punchHand_ + 1;
    punchBufferedAtStart_ = punchBuffered_;
    punchRequest_ = true;
    kickRequest_ = kickable_;   // 0x678330: state 0x33 instead of 8
}

// Kick (0x4dc4e0): moves like the punch; a press within NonCancelableTime is kept; after 0.4 s a
// grounded hero with the stick walks off (0x6882a0), otherwise the played clip ends it (Idle).
// From 0.1 s until it meets something, a sphere of 0.1 m sweeps 0.05..0.2 m ahead of the hero at
// half its scale above the feet, hitting along its facing (hit type 1).
void Hero::updateKick(const HeroInput& in) {
    constexpr float kWalkOffTime = 0.4f;    // 0x11f2288
    constexpr float kHitStart = 0.1f;       // 0x11f228c
    constexpr float kRadius = 0.1f;         // 0x11f228c (the query's radius)
    constexpr float kReachFrom = 0.05f, kReach = 0.15f;   // 0x11f31a0 / 0x11f31b0
    if (grounded_) {
        groundControl(in);
    } else if (groundState_ != 2) {
        airControl(in);
    }
    if (stateTime_ < t_.punchNonCancelableTime && in.punchPressed) {
        punchBuffered_ = true;
    }
    if (kWalkOffTime < stateTime_ && stickAmount(in) > 0 && grounded_) {
        request(HeroState::Walk);
    } else if (stateTime_ >= clips_.kick) {
        request(HeroState::Idle);
    }
    if (!kickLanded_ && kHitStart < stateTime_) {
        const Vec3 facing{std::sin(yaw_), 0, std::cos(yaw_)};
        HeroKickSweep k;
        k.from = pos_ + Vec3{0, 0.5f * t_.scale, 0} + kReachFrom * facing;
        k.to = k.from + kReach * facing;
        k.direction = facing;
        k.radius = kRadius;
        kickSweep_ = k;
    }
}

// Punch enter (0x4cf690): the clip by hand and stance (no stick: the standing punch), the air /
// edge flags, and the attack window for that hand from CollisionStartTime for CollisionTime.
void Hero::enterPunch(const HeroInput& in) {
    const bool right = punchHand_ != 0;
    const bool still = stickAmount(in) <= 0;   // +0x11c
    punchClip_ = right ? (still ? kPunchRightStill : kPunchRightMoving) : (still ? kPunchLeftStill : kPunchLeftMoving);
    punchAtEdge_ = groundState_ == 2;
    punchFromAir_ = !grounded_ && !punchAtEdge_;
    punchBuffered_ = false;
    windowOpen_ = true;
    windowRight_ = right;
    windowLeft_ = !right;
    windowTime_ = 0;
    windowStart_ = t_.punchCollisionStartTime;
    windowEnd_ = t_.punchCollisionStartTime + t_.punchCollisionTime;
    ++windowCount_;
    lastRight_ = handPosition(true);
    lastLeft_ = handPosition(false);
}

// The swinging hand's world position: the punch clip's hand joint at the pose of the last drawn
// frame (the animation advances after the attack update), placed by the hero's transform.
math::Vec3 Hero::handPosition(bool right) const {
    const HeroPunchClip& c = clips_.punch[punchClip_];
    (void)right;   // each punch clip carries the hand it swings
    const math::Vec3 h = c.handAt(std::max(0.0f, windowTime_ - kStep)) * t_.scale;
    const float cy = std::cos(yaw_), sy = std::sin(yaw_);
    return {pos_.x + h.x * cy + h.z * sy, pos_.y + h.y, pos_.z - h.x * sy + h.z * cy};
}

// Attack window (0x66a730): the time runs while open; before CollisionStartTime the hands' places
// are only recorded; from then each update sweeps each active hand from its last place to the
// new one (0x674c80 also records the new place); after the window it closes.
void Hero::updateAttackWindow() {
    if (!windowOpen_) {
        return;
    }
    windowTime_ += kStep;
    if (windowTime_ > windowEnd_) {
        windowOpen_ = false;
        return;
    }
    const math::Vec3 right = handPosition(true), left = handPosition(false);
    if (windowTime_ >= windowStart_) {
        if (windowRight_) {
            sweeps_.push_back({lastRight_, right, t_.punchCollisionRadius, windowCount_});
        }
        if (windowLeft_) {
            sweeps_.push_back({lastLeft_, left, t_.punchCollisionRadius, windowCount_});
        }
    }
    lastRight_ = right;
    lastLeft_ = left;
}

// Body step (0x66ab90): gravity, then the horizontal move with collide-and-slide, then the
// vertical move against the floor and ceiling.
void Hero::bodyStep(const physics::PhysicsWorld& world) {
    const float dt = kStep;
    vel_.y += t_.gravity * dt;
    // Standing on a moving body (0x66ab90, support motion type 1/2 sets +0x1b8): the hero's x/z
    // follow the body's rigid motion of this update (linear and angular velocity applied to the
    // position); the ground probe then reaches 0.2 m further down to stay on it.
    const bool carried = grounded_ && support_ >= 0;
    const bool wasOnSlope = groundState_ == 3;
    if (carried) {
        const math::Vec3 moved = transformPoint(world.delta(support_), pos_);
        pos_.x = moved.x;
        pos_.z = moved.z;
    }
    // Collide-and-slide of the whole move (0x66dd40), then the ground probe from the old height
    // down to where the slide ended.
    const Vec3 start = pos_;
    const float oldY = pos_.y;
    slide(world, {vel_.x * dt, vel_.y * dt, vel_.z * dt});
    const float dy = pos_.y - oldY;
    pos_.y = oldY;
    // The probe reaches 0.2 m further on a moving support or a slide (+0x1b8 / +0x14c == 3).
    moveVertical(world, dy, carried || wasOnSlope);
    groundState_ = onSlope_ ? 3 : grounded_ ? 1 : 0;
    if (groundState_ == 1 && (teeterWallStop(start) || dropEdgeStop(world, start))) {
        groundState_ = 2;
    }
    // The original then pushes the hero 7 mm per update horizontally away from contacts of its
    // body with moving (keyframed) objects (0x66e950); the static level does not push. Moving
    // objects are not in this collision world yet.
}

// Collide-and-slide (0x66dd40): the wall capsule (radius CollisionForWallRadius, half height
// CollisionForWallHeight / 2, centred CollisionForWallYOffset above the feet, upright) cast with
// the module's capsule query from the start to the end of this update's move, including the
// vertical part. A hit at a fraction >= 0.001 moves the start to the touching point (no skin),
// keeps the hit normal and projects the rest of the move off every kept normal it points into; up
// to 6 passes. Touching at the start (fraction < 0.001) with the contact beside the capsule's
// straight part: pushed out horizontally to radius + 0.001. The velocity is only changed at a
// ceiling (a hit facing down: the rise cut to the ceiling's vertical speed).
math::Vec3 Hero::collideAndSlide(const physics::PhysicsWorld& world, const Vec3& feet, const Vec3& delta) {
    const Vec3 keepPos = pos_, keepVel = vel_;
    pos_ = feet;
    slide(world, delta);
    const Vec3 end = pos_;
    pos_ = keepPos;
    vel_ = keepVel;
    return end;
}

void Hero::slide(const physics::PhysicsWorld& world, const Vec3& delta) {
    const float r = t_.collisionForWallRadius;
    const float half = t_.collisionForWallHeight * 0.5f;
    const float centre = t_.collisionForWallYOffset;
    const math::Quat upright{0, 0, kUprightSin, kUprightSin};   // the capsule's x axis turned to y
    std::vector<Vec3> normals;
    Vec3 start = pos_, end = pos_ + delta;
    for (int pass = 0; pass < kSlideIterations; ++pass) {
        const Vec3 move = end - start;
        const float len = length(move);
        if (len < 1e-7f) {
            break;
        }
        const Vec3 up{0, centre, 0};
        const auto hit = world.castCapsule(start + up, end + up, r, half, upright, physics::kHitHero, kWallFilter);
        static const bool debug = std::getenv("BEACON_SLIDE_DEBUG") != nullptr;   // research aid
        if (debug) {
            std::fprintf(stderr, "slide %.6f %.6f %.6f -> %.6f %.6f %.6f r %.4f h %.4f: %s t %.6f p %.6f %.6f %.6f n %.4f %.4f %.4f\n",
                         start.x, start.y + centre, start.z, end.x, end.y + centre, end.z, r, half, hit ? "hit" : "miss",
                         hit ? hit->fraction : 1.0f, hit ? hit->point.x : 0.f, hit ? hit->point.y : 0.f,
                         hit ? hit->point.z : 0.f, hit ? hit->normal.x : 0.f, hit ? hit->normal.y : 0.f,
                         hit ? hit->normal.z : 0.f);
        }
        if (!hit) {
            break;
        }
        const float fraction = hit->fraction;
        Vec3 next = start + move * fraction;
        if (fraction < kSlideTouch) {
            const float cy = start.y + centre;
            if (hit->point.y <= cy + half && hit->point.y >= cy - half) {
                Vec3 away{start.x - hit->point.x, 0, start.z - hit->point.z};
                const float dist = length(away);
                if (dist >= 1.19e-7f) {
                    next = start + away * ((r - std::min(r, dist) + kSlideTouch) / dist);
                    next.y = end.y;
                }
            } else if (hit->point.y < cy - half) {
                // Below the straight part (the lower cap): nudged 1 mm horizontally away from the
                // contact, seen from the cap's centre (0x66dd40, constant 0x12210e0; traced: the
                // ROOFTOPS slide's rail joints are passed this way).
                Vec3 away{start.x - hit->point.x, 0, start.z - hit->point.z};
                const float dist = length(away);
                if (dist >= 1.19e-7f) {
                    next = start + away * (kSlideNudge / dist);
                    next.y = end.y;
                }
            }
        }
        if (hit->normal.y < kCeilingNormalY) {
            // A ceiling (0x66dd40: hit normal y < -0.7; sets +0x488): the rise is cut to the
            // ceiling's own vertical speed - 0 for the static level, the body's for a keyframed
            // or active one (motion 2 / 1, 0xca5c50 / 0xca5f50).
            const float limit = world.moving(hit->body) ? world.linearVelocity(hit->body).y : 0.0f;
            if (limit + 1.1920929e-7f < vel_.y) {
                vel_.y = limit;
            }
        }
        normals.push_back(hit->normal);
        Vec3 rest = end - next;
        for (const Vec3& n : normals) {
            const float into = dot(rest, n);
            if (into < 0) {
                rest -= n * into;
            }
        }
        start = next;
        end = next + rest;
    }
    pos_ = end;
}

// Teetering walls (0x6738f0, from the edge check 0x66eb30 at the end of the body step, while
// grounded): for each wall the foot body touches, d = the new position's distance along the wall
// normal (0x953e90); a position up to 5 cm behind the wall (-0.05 < d < 0) with the stick pointing
// against the normal (s = dot(-normal, stick or facing) > 0) is put back onto the wall plane; when s > 0.9
// the hero stops there: velocity 0, ground state 2 (Walk/Idle then turn to Teeter) and the edge
// direction is kept. Traced: the stop at a forest_castle ledge lands on the wall plane to 4e-5.
bool Hero::teeterWallStop(const Vec3& start) {
    (void)start;
    if (!teeterWalls_) {
        return false;
    }
    // The stick direction, or without stick the facing direction (0x6738f0: orientation, +Z).
    const bool stick = stickDir_.x != 0 || stickDir_.z != 0;
    const Vec3 dir = stick ? stickDir_ : Vec3{std::sin(yaw_), 0, std::cos(yaw_)};
    bool stopped = false;
    const Vec3 foot = pos_ + Vec3{0, t_.collisionYOffset, 0};
    for (const TeeterWall& w : *teeterWalls_) {
        // Contact of the foot body (sphere of CollisionRadius) with the wall box.
        const Vec3 rel = foot - w.centre;
        const Vec3 local{dot(rel, w.axisX), dot(rel, w.axisY), dot(rel, w.axisZ)};
        const Vec3 clamped{std::clamp(local.x, -w.half.x, w.half.x), std::clamp(local.y, -w.half.y, w.half.y),
                           std::clamp(local.z, -w.half.z, w.half.z)};
        const Vec3 gap = local - clamped;
        if (dot(gap, gap) > t_.collisionRadius * t_.collisionRadius) {
            continue;
        }
        const float d = dot(pos_ - w.centre, w.axisZ);
        if (!(d < 0 && d > kTeeterBand)) {
            continue;
        }
        const float s = -dot(w.axisZ, dir);
        if (s <= 0) {
            continue;
        }
        pos_ -= w.axisZ * d;
        if (s > kTeeterStopDot) {
            vel_ = {};
            teeterDir_ = w.axisZ * -1.0f;
            stopped = true;
        }
    }
    return stopped;
}

// The drop samples' cast (mask 0xc844191f) includes the teetering walls' group (0x400000): a wall
// below or around the sample counts as ground (a wall is not a teetering_collision, 0x673f50).
bool Hero::sampleHitsTeeterWall(const Vec3& sample) const {
    if (!teeterWalls_) {
        return false;
    }
    for (const TeeterWall& w : *teeterWalls_) {
        // The cast's swept volume (sphere r 0.02 from the sample 5 m down) against the wall box.
        for (float t = 0; t <= kEdgeDepth; t += kEdgeSampleRadius) {
            const Vec3 c = sample - Vec3{0, t, 0} - w.centre;
            const Vec3 local{dot(c, w.axisX), dot(c, w.axisY), dot(c, w.axisZ)};
            const Vec3 q{std::clamp(local.x, -w.half.x, w.half.x), std::clamp(local.y, -w.half.y, w.half.y),
                         std::clamp(local.z, -w.half.z, w.half.z)};
            const Vec3 g = local - q;
            if (dot(g, g) <= kEdgeSampleRadius * kEdgeSampleRadius) {
                return true;
            }
            if (local.y < -w.half.y - kEdgeSampleRadius) {
                break;   // below the wall
            }
        }
    }
    return false;
}

// Drop edges (0x66eb30 after the teetering walls): samples 0.1 m ahead of the new position in the
// move direction (without a move: the facing), then 90 degrees left and right of it, each from
// half the scale above the ground: when a ray from the hero to the sample is free and a sphere
// (r 0.02) cast 5 m down from it finds nothing (0x673f50), the first such sample marks an edge
// (teetering_collision volumes also mark edges: not ported). A cast (r 0.01) 0.02 m below the
// feet from that sample back to the old position finds the edge face; the point s on that line
// 0.04 m inside the face limits the move: once the new position is past s it is moved onto the
// line through s along the edge (sliding along it); meeting the edge head-on (dot >= 0.9 with the
// face normal) or standing still stops the hero there (velocity 0, ground state 2 -> Teeter).
bool Hero::dropEdgeStop(const physics::PhysicsWorld& world, const Vec3& start) {
    Vec3 move{pos_.x - start.x, 0, pos_.z - start.z};
    const float len = length(move);
    const bool still = !(len > 1.19e-7f);
    const Vec3 dir = still ? Vec3{std::sin(yaw_), 0, std::cos(yaw_)} : move * (1 / len);
    const Vec3 up{0, t_.scale * 0.5f, 0};
    const Vec3 aheads[3] = {dir * kEdgeAhead, Vec3{dir.z, 0, -dir.x} * kEdgeAhead, Vec3{-dir.z, 0, dir.x} * kEdgeAhead};
    std::optional<Vec3> edgeSample;
    for (const Vec3& a : aheads) {
        const Vec3 from = pos_ + up;
        const Vec3 sample = from + a;
        if (world.castRay(from, from + normalize(a, dir) * kEdgeAhead, physics::kHitHero, kEdgeAheadRay)) {
            continue;   // something between the hero and the sample: no verdict
        }
        if (!world.castSphere(sample, sample - Vec3{0, kEdgeDepth, 0}, kEdgeSampleRadius, physics::kHitHero, kEdgeFilter) &&
            !sampleHitsTeeterWall(sample)) {
            edgeSample = sample;
            break;
        }
    }
    if (!edgeSample) {
        return false;
    }
    const Vec3 p{edgeSample->x, start.y - kEdgeFaceDrop, edgeSample->z};
    const Vec3 q{start.x, start.y - kEdgeFaceDrop, start.z};
    const Vec3 pq = q - p;
    const float pqLen = length(pq);
    if (pqLen < 1e-6f) {
        return false;
    }
    auto face = world.castSphere(p, q, kEdgeFaceRadius, physics::kHitHero, kEdgeFaceFilter);
    if (!face) {
        return false;
    }
    Vec3 n{face->normal.x, 0, face->normal.z};
    if (length(n) <= kEdgeNormalMin) {
        return false;
    }
    n = normalize(n, n);
    const Vec3 u = normalize(p - q, dir);   // from the old position towards the edge
    const Vec3 sPoint = face->point - u * (kEdgeInside / dot(u, n));
    Vec3 d = pos_ - sPoint;
    d.y = 0;
    if (dot(u, d) < 0) {
        return false;   // not past the edge line yet
    }
    Vec3 tangent{n.z, 0, -n.x};
    if (dot(tangent, dir) < 0) {
        tangent = tangent * -1.0f;
    }
    const bool stop = still || dot(dir, n) >= kTeeterStopDot;
    const float y = pos_.y;
    pos_ = sPoint + tangent * dot(d, tangent);
    pos_.y = y;
    if (stop) {
        vel_ = {};
        teeterDir_ = n;
    }
    return stop;
}

std::vector<TeeterWall> collectTeeterWalls(const Level& level) {
    std::vector<TeeterWall> walls;
    for (const auto& o : level.objects) {
        if (o.className != "teetering_wall" || o.asTemplate) {
            continue;
        }
        const math::Mat4 m = o.transform.matrix();
        TeeterWall w;
        w.centre = {m[12], m[13], m[14]};
        const Vec3 cx{m[0], m[1], m[2]}, cy{m[4], m[5], m[6]}, cz{m[8], m[9], m[10]};
        const float sx = length(cx), sy = length(cy), sz = length(cz);
        w.axisX = cx * (1 / sx);
        w.axisY = cy * (1 / sy);
        w.axisZ = cz * (1 / sz);
        w.half = {0.5f * sx, 0.25f * sy, 0.05f * sz};   // 0x128e114..0x128e11c
        walls.push_back(w);
    }
    return walls;
}

// Vertical move (0x66ab90): rising or falling, a thin ground probe (the module's sphere query,
// radius 0.01) sweeps from half the model scale above the old feet height to the new one; the
// feet are put exactly on what it hits. (No ceiling test here: ceilings are met by the wall
// capsule in slide(); traced: a jump beside a moss lip rises although the foot body touches the lip.) Standing still,
// one step of gravity is the reach below the feet; walking off a ledge it misses and the hero
// falls; steps up to 0.11 m are climbed; low lips inside the footprint (trim 4 cm high) pass
// under the foot body. (A special mode, flag +0x1b8, reaches 0.2 m further: not ported.)
// Traced in the original: the feet rest on the floor height to 1e-6, ledges are left falling.
void Hero::moveVertical(const physics::PhysicsWorld& world, float dy, bool extendedReach) {
    const float walkableY = std::cos(t_.slopeMaxAngle * kPi / 180);
    const bool wasOnSlope = groundState_ == 3;
    grounded_ = false;
    onSlope_ = false;
    const float rise = t_.scale * kGroundProbeStartScale;
    const Vec3 start = pos_ + Vec3{0, rise, 0};
    const float reach = rise - dy + (extendedReach ? kGroundProbeLength : 0.0f);
    auto hit = world.castSphere(start, start - Vec3{0, reach, 0}, kGroundProbeRadius, physics::kHitHero, kGroundFilter);
    // A hit counts only on a body of motion type below 3 and while the hero does not rise faster
    // than it (v.y <= FLT_EPSILON or <= the body's own v.y; 0x66ab90 at 0x66b87e): a jump rises
    // through a step's height without landing on it (traced: run roof_slide1, update 2920).
    if (hit && hit->body >= 0) {
        const bool lowMotion = int(world.motion(hit->body)) < 3;
        const bool notRising = vel_.y <= 1.1920929e-7f || vel_.y <= world.linearVelocity(hit->body).y;
        if (!lowMotion || !notRising) {
            hit.reset();
        }
    }
    support_ = -1;
    groundBody_ = -1;
    // The probe's hit normal (meshes: the facet's; the same normal decides walkable and the slope).
    if (hit && hit->normal.y >= walkableY) {
        pos_.y = hit->point.y;
        grounded_ = true;
        vel_.y = 0;
        support_ = world.moving(hit->body) ? hit->body : -1;
        groundBody_ = hit->body;
        // A slide surface (the body's IsSlope) steeper than SlopeMinAngle (SlopeMinAngleOnSliding
        // while sliding) with a downhill direction: ground kind 3, its normal and the direction
        // down along it, (0, -1, 0) laid onto the surface (0x66ab90 at 0x66b98f..0x66bae3).
        if (hit->body >= 0 && world.surface(hit->body).slope) {
            const float minAngle = wasOnSlope ? t_.slopeMinAngleOnSliding : t_.slopeMinAngle;
            const Vec3 n = hit->normal;
            if (n.y < std::cos(minAngle * kPi / 180)) {
                const Vec3 down{n.x * n.y, n.y * n.y - 1, n.z * n.y};   // (0,-1,0) - n (n . (0,-1,0))
                const float len = std::sqrt(dot(down, down));
                if (len > kSlopeDirMin) {
                    onSlope_ = true;
                    slopeNormal_ = n;
                    slopeDir_ = down * (1 / len);
                }
            }
        }
        // Downhill (0x66ab90 at 0x66bb75, slides too: traced v.y -0.140 entering the ROOFTOPS slide): the walking velocity laid onto the ground plane gives
        // k = -(n.x v.x + n.z v.z) / n.y; when k < 0, a ray from half the scale above the next
        // horizontal position down to 1.5 k dt below it confirms the slope continues, and v.y
        // becomes k (traced: -0.0153 on the moss at 1 m/s; leaving such a slope keeps it).
        const Vec3 n = hit->normal;
        const float k = n.y > 0 ? -(n.x * vel_.x + n.z * vel_.z) / n.y : 0.0f;
        if (k < 0) {
            const Vec3 ahead = pos_ + Vec3{vel_.x * kStep, 0, vel_.z * kStep};
            const float down = rise - kSlopeProbeReach * k * kStep;
            if (world.castRay(ahead + Vec3{0, rise, 0}, ahead + Vec3{0, rise - down, 0}, physics::kHitHero, kDownhillRay)) {
                vel_.y = k;
            }
        }
    } else if (hit) {
        pos_.y = start.y - hit->fraction * reach - kGroundProbeRadius;   // steep surface: stop, keep falling next step
    } else {
        pos_.y += dy;
    }
}


// ---- Bodies -----------------------------------------------------------------------------------

namespace {
const math::Quat kUpright{0, 0, 0.70710678f, 0.70710678f};   // the library capsule's x axis turned up
}

void HeroBodies::attach(physics::PhysicsWorld& world, const HeroTuning& t, const math::Vec3& feet, std::uint32_t self) {
    // 0x667200. The shapes sit at their offsets above the body's origin, which follows the feet.
    physics::BodyOptions foot;
    foot.motion = physics::rigid::MotionType::Active;
    foot.filterSelf = self;
    foot.filterTarget = 0x7fffffffu;
    foot.surface.mask = 0;   // the module's queries do not stop at the hero
    foot_ = world.addCapsule(0.5f * t.collisionHeight, t.collisionRadius, 100.0f, foot, {0, t.collisionYOffset, 0},
                             kUpright);
    physics::BodyOptions sensor;
    sensor.motion = physics::rigid::MotionType::Trigger;
    sensor.filterSelf = 0x800000u;
    sensor.filterTarget = 0x10041000u;
    sensor.surface.mask = 0;
    sensor_ = world.addCylinder(0.5f * t.enemySensorHeight, t.enemySensorRadius, 1.0f, sensor,
                                {0, 0.5f * t.scale, 0}, kUpright);
    world.setPose(foot_, feet, {});
    world.setPose(sensor_, feet, {});
}

void HeroBodies::place(physics::PhysicsWorld& world, const math::Vec3& feet) {
    // 0x66d5f0 (the hero update's end; mode +0x168 == 2, set straight, is not ported).
    if (foot_ >= 0) {
        world.moveTo(foot_, feet);
        world.setOrientation(foot_, {});
        world.setAngularVelocity(foot_, {});
    }
    if (sensor_ >= 0) {
        world.setPose(sensor_, feet, {});
    }
}

// ---- damage ------------------------------------------------------------------------------------

namespace {

// Hit types whose knock-down is heavy / light (Damage enter 0x4cff50, +0x5c; type 9 middle; any
// other type keeps the last weight).
bool heavyHit(int type) {
    switch (type) {
    case 12: case 31: case 32: case 34: case 38: case 39: case 43: case 47: case 49: case 50:
        return true;
    default:
        return false;
    }
}
bool lightHit(int type) { return type == 59 || type == 62 || type == 63 || type == 64; }

// Types after which Damage gets up again (0x4cff50's second switch, sound 0x8173c031); every other
// type dies at the end of the down time (+0x58, sound 0x3318d318). Type 5 with a flag not ported.
bool survivableHit(int type) {
    switch (type) {
    case 1: case 2: case 3: case 5: case 6: case 0x11: case 0x28: case 0x29: case 0x2a: case 0x32: case 0x33:
    case 0x34: case 0x3b: case 0x3e: case 0x40: case 0x42: case 0x4d:
        return true;
    default:
        return false;
    }
}

constexpr float kLightRecoverTime = 0.3f;   // LightDamage: hittable again on the ground after (0x11f20c8)
constexpr float kLightWalkTime = 0.4f;      // ... and the stick walks off after (0x11f20cc)
constexpr float kDamageWalkTime = 0.5f;     // Damage: the stick walks off this long into the out clip (0x11f20ac)
constexpr float kSpikeDeathWait = 0.4f;     // DeathOnSpike: hidden this long before the death (0x11f2278)

// -hit.direction on the ground plane, unit (towards the hit's source).
Vec3 towardsSource(const HeroHit& hit) {
    Vec3 d{-hit.direction.x, 0, -hit.direction.z};
    const float len = std::sqrt(d.x * d.x + d.z * d.z);
    return len > 1.17549435e-38f ? d * (1 / len) : Vec3{};
}

}   // namespace

bool Hero::takeHit(const HeroHit& hit) {
    // 0x66faf0: nothing while invincible (+0x316), except types 0xf / 0x15. The other refusals
    // (+0x100 of the machine, a second machine busy) are not ported.
    if (state_ == HeroState::Respawn) {
        return false;   // the second machine (+0x88 block +0x18) busy: no hits at all
    }
    if (invincible_ && hit.type != 0xf && hit.type != 0x15) {
        return false;
    }
    HeroState next;
    switch (hit.type) {
    case 0x14:
    case 0x37:
    case 0x47:
    case 0x53:
        if (!damageable_) {
            return false;
        }
        next = HeroState::DeathOnSpike;
        break;
    case 0x12:
    case 0x2e:
        if (!damageable_ || !lightDamageable_) {
            return false;
        }
        next = HeroState::LightDamage;
        break;
    default:
        // 0x672270 (water: DeathInWater, not ported).
        if (!damageable_) {
            return false;
        }
        next = HeroState::Damage;
        break;
    }
    hit_ = hit;
    hitRequest_ = true;   // a later hit before the next update replaces it (forced requests)
    hitState_ = next;
    return true;
}

void Hero::playAction(const char* name) {
    actionClip_ = name;
    actionTime_ = 0;
    action_ = clips_.motionClip(name);
}

bool Hero::actionDone() const {
    return !action_ || actionTime_ >= action_->length;
}

Vec3 Hero::rootMotion() const {
    // The root-motion joint's travel over this update, turned by the facing and scaled by the
    // model's scale (the model's frame, 0x695020).
    if (!action_) {
        return {};
    }
    const Vec3 a = action_->rootAt(std::max(0.0f, actionTime_ - kStep)), b = action_->rootAt(actionTime_);
    const Vec3 d = (b - a) * t_.scale;
    const float c = std::cos(yaw_), sn = std::sin(yaw_);
    return {c * d.x + sn * d.z, d.y, -sn * d.x + c * d.z};
}

void Hero::setInvincible(float seconds) {
    invincible_ = true;
    invincibleTime_ = 0;
    invincibleFor_ = seconds;
}

void Hero::die() {
    // 0x671f80: telemetry ("enemey"), drops what the hero carries, effect and sound, the machines
    // reset (0x688730); +0x2f2 cleared. The respawn is the level's (not ported: the scene restarts).
    died_ = true;
}

void Hero::startRespawn(const Vec3& at, float effectTime, float invincibleTime) {
    // DefaultRespawn enter (0x6a1170): hidden, no collision, the main machine idle (0).
    exitState(HeroState::Respawn);
    state_ = HeroState::Respawn;
    stateTime_ = 0;
    hitRequest_ = false;
    respawnAt_ = at;
    respawnEffectTime_ = effectTime;
    respawnInvincible_ = invincibleTime;
    respawnPhase_ = 0;
    hidden_ = true;
    died_ = false;
    collisionOff_ = true;
    damageable_ = lightDamageable_ = true;
    vel_ = {};
    windowOpen_ = false;
}

void Hero::exitState(HeroState next) {
    (void)next;
    switch (state_) {
    case HeroState::LightDamage:   // 0x4d0d80
        lightDamageable_ = true;
        vel_ = {};
        break;
    case HeroState::Damage:   // 0x4d0740
        if (!damageDies_) {
            collisionOff_ = false;   // 0x66fa60 mode 0
        }
        damageable_ = true;
        break;
    case HeroState::DeathOnSpike:   // 0x4db6f0
        damageable_ = true;
        break;
    default:
        break;
    }
}

// LightDamage enter (0x4d0a50): not again until it ends (+0x109); the clip by where the hit came
// from, seen from the hero (a = atan2(local z, local x) of the direction towards the source, plus
// pi/4, taken to [0, 2 pi) after adding pi: left / forward / right / backward light by quarter);
// the push is the clip's root motion; the vertical speed is kept.
void Hero::enterLightDamage() {
    lightDamageable_ = false;
    const Vec3 d = towardsSource(hit_);
    const float c = std::cos(yaw_), sn = std::sin(yaw_);
    const float lx = c * d.x - sn * d.z, lz = sn * d.x + c * d.z;
    const float f = std::fmod(std::atan2(lz, lx) + kPi * 0.25f + kPi + 2 * kPi, 2 * kPi);
    playAction(f < kPi * 0.5f ? "damage_left_light"
               : f < kPi      ? "damage_forward_light"
               : f < kPi * 1.5f ? "damage_right_light"
                                : "damage_backward_light");
    vel_.x = 0;
    vel_.z = 0;
}

// LightDamage update (0x4d0de0): Idle when the clip has played; on the ground hittable again
// after 0.3 s and walking off with the stick after 0.4 s; the horizontal velocity is the clip's
// root travel over the update.
void Hero::updateLightDamage(const HeroInput& in) {
    if (actionDone()) {
        request(HeroState::Idle);
    }
    if (grounded_ && stateTime_ > kLightRecoverTime) {
        lightDamageable_ = true;
    }
    if (grounded_ && stateTime_ > kLightWalkTime && stickAmount(in) > 0) {
        request(HeroState::Walk);   // 0x6882a0 (+0x11c: no stick)
    }
    const Vec3 r = rootMotion();
    vel_.x = r.x / kStep;
    vel_.z = r.z / kStep;
}

// Damage enter (0x4cff50): the weight (down time) and whether it ends in death come from the hit's
// type; the hero turns towards the source (damage_backward_*) when the source is in front, else
// away from it (damage_forward_*); launched away from the source at DamageVelocityXZ, up at
// DamageVelocityY (other launch speeds for other types, not ported).
void Hero::enterDamage() {
    const int type = hit_.type;
    if (heavyHit(type)) {
        damageWeight_ = 2;
    } else if (lightHit(type)) {
        damageWeight_ = 0;
    } else if (type == 9) {
        damageWeight_ = 1;
    }
    damageable_ = false;
    damageDies_ = !survivableHit(type);
    damagePhase_ = 0;
    damageOutTime_ = 0;
    Vec3 d = towardsSource(hit_);
    if (d.x == 0 && d.z == 0) {
        d = {std::sin(yaw_), 0, std::cos(yaw_)};   // **[guess]** the original picks a random direction
    }
    const float c = std::cos(yaw_), sn = std::sin(yaw_);
    const float lz = sn * d.x + c * d.z;
    if (lz >= 0) {
        playAction("damage_backward_in");
        damageLoop_ = "damage_backward_loop";
        damageOut_ = "damage_backward_out";
        yaw_ = std::atan2(d.x, d.z);
    } else {
        playAction("damage_forward_in");
        damageLoop_ = "damage_forward_loop";
        damageOut_ = "damage_forward_out";
        yaw_ = std::atan2(-d.x, -d.z);
    }
    const float up = type == 0x53 ? 0.0f : t_.damageVelocityY;
    vel_ = Vec3{-d.x * t_.damageVelocityXZ, up, -d.z * t_.damageVelocityXZ};
    grounded_ = false;
}

// Damage update (0x4d0780): stopped on the ground; in -> loop -> after the down time either the
// death routine (a deadly hit) or the out clip, after which Idle (or Walk with the stick after
// 0.5 s); hittable again InvincibleTimeAfterKnockDown + 0.5 s into the out clip, invincible until
// then once it leaves. The clip's root travel moves the hero directly.
void Hero::updateDamage(const HeroInput& in) {
    if (grounded_) {
        vel_ = {};
    }
    const float downTime = damageWeight_ == 2   ? t_.heavyDamageDownTime
                           : damageWeight_ == 1 ? t_.middleDamageDownTime
                                                : t_.lightDamageDownTime;
    if (damagePhase_ == 0) {
        if (actionDone()) {
            playAction(damageLoop_);
            damagePhase_ = 1;
        }
    } else if (damagePhase_ == 1) {
        if (stateTime_ > downTime) {
            if (damageDies_) {
                die();
                damagePhase_ = 3;
            } else {
                playAction(damageOut_);
                damagePhase_ = 2;
            }
        }
    } else if (damagePhase_ == 2) {
        damageOutTime_ += kStep;
        const float recover = t_.invincibleTimeAfterKnockDown + kDamageWalkTime;
        if (damageOutTime_ > recover) {
            damageable_ = true;
        }
        bool leaving = true;
        if (actionDone()) {
            request(HeroState::Idle);
        } else if (damageOutTime_ > kDamageWalkTime && stickAmount(in) > 0) {
            request(HeroState::Walk);
        } else {
            leaving = false;
        }
        if (leaving && damageOutTime_ < recover) {
            setInvincible(recover - damageOutTime_);
        }
    }
    pos_ = pos_ + rootMotion();
}

// DeathOnSpike update (0x4db720): death_spikes with its root travel; when it has played the model
// goes (0x6960f0) and ef_hero_death_spikes appears at the hero (not ported); 0.4 s later the death
// routine.
void Hero::updateDeathOnSpike() {
    if (spikePhase_ == 0) {
        if (actionDone()) {
            spikePhase_ = 1;
            hidden_ = true;
        }
        pos_ = pos_ + rootMotion();
    }
    if (spikePhase_ == 1) {
        spikeWait_ += kStep;
        if (spikeWait_ > kSpikeDeathWait) {
            die();
            spikePhase_ = 2;
        }
    }
}

}   // namespace game
