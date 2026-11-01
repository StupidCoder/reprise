// Recovered player walk action (original game, ARM11 main code): the "Move" state's action object
// (vtable 0x4cb360; in the opening at 0x334e7938).
//
// How walking works, per 30 Hz tick (update, 0x1f3e04):
//   1. The stick vector (already camera-relative and snapped, Pad.hpp) is rescaled so that a stick
//      between the walk and run thresholds walks at a steady pace.
//   2. The velocity is carried over to the current ground plane (rotated from the previous ground
//      normal to the new one) and split into three parts: forward (along the facing), normal (along
//      the ground normal) and sideways.
//   3. Sideways speed always decays to zero over a few frames ("brake frames").
//   4. Forward speed: with no stick it decays; with the stick pointing more than about 100 degrees
//      away from the facing it brakes; otherwise it accelerates towards a target speed (walk maximum,
//      or dash maximum while the dash button is held), scaled by how far the stick is pushed and
//      boosted downhill.
//   5. The facing turns towards the stick direction, limited to a number of degrees per tick that
//      depends on the speed and on how fast the stick itself is rotating.
//   6. The velocity is recomposed; the walk/run animations are blended by speed (four clips:
//      WalkSlowly, Walk, WalkSpeedy, Run), their playback rate is set, and the body tilts slightly
//      into turns.
// Movement then happens in the collider (PlayerCollider.hpp), which applies the velocity against the
// terrain.
//
// Corrected decompilation of
//   0x1f3978 enter, 0x1f3e04 update, 0x1f517c exit,
//   0x1f37b0 Move blend weights, 0x3ea02c Move animation rate, 0x1f4ee4 forward tilt,
//   0x2a20cc / 0x3ea21c maximum speeds, 0x2a21d0 ground record,
// with the original operation order (compile with -ffp-contract=off). Branches that depend on
// objects absent from the opening (dash panels, invincible dash, ice, water, "Subjective" camera
// mode) are kept and fed by Services; their values in the opening come from the traced runs
// (evidence/runs/player-walk-*-b, tools/walk_trace.py). Comments of the form "s21" or "sp+0x48" name
// the original's VFP register or stack slot for a value, to make comparison with the disassembly easy.
#pragma once

#include <cstdint>
#include <string>

#include "game/Math.hpp"
#include "game/PlayerCollider.hpp"

namespace captain_toad::RecoveredPlayerWalk {

using captain_toad::RecoveredVec::V3;

// PlayerConst values read by the walk code. The runtime values at the opening checkpoint equal the
// registered defaults (evidence/player/params.json). Comments give the parameter getter offset.
struct WalkParams {
    float normalSpeedMaxGround = 16.6f;             // 0xb0  walk maximum (units per tick)
    float normalSpeedMaxGroundMini = 16.6f;         // 0xb4
    float speedDampWaterGround = 0.7f;              // 0xb8
    float walkSpeedRateMin = 0.3f;                  // 0xc0  speed fraction at the smallest stick tilt
    float dashSpeedMax = 18.0f;                     // 0xc4
    float dashSpeedMaxInvincible = 20.0f;           // 0xc8 (field 0xcc)
    float runAnimRateMaxInvincible = 1.5f;          // 0x134 (field 0x138)
    float walkSlowlyAnimRateMax = 1.0f;             // 0xcc
    float walkAnimRateMax = 1.4f;                   // 0xd0
    float walkSpeedyAnimRateMax = 2.0f;             // 0xd4
    float animRateMini = 1.4f;                      // 0xd8
    float leftStickRate = 1.5f;                     // 0xdc
    float leftStickMinWalkThreshold = 0.3f;         // 0xe0
    float leftStickRunThresholdDiff = 0.015f;       // 0xe4
    float walkAnimBlendThres = 0.6f;                // 0xe8
    float walkSpeedyAnimBlendThres = 0.65f;         // 0xec
    float runAnimBlendThres = 0.95f;                // 0xf0
    float animBlendWeightApproachRate = 0.6f;       // 0xf4
    float animBlendWeightApproachRateRun = 0.8f;    // 0xf8
    uint32_t brakeFrame = 1;                        // 0xfc
    uint32_t dashBrakeFrame = 5;                    // 0x104
    uint32_t stickOnBrakeFrame = 60;                // 0x108
    uint32_t brakeFrameOnIce = 45;                  // 0x10c
    uint32_t accelFrame = 2;                        // 0x110
    uint32_t dashAccelFrame = 120;                  // 0x114
    float roundLimitDegreeMax = 22.0f;              // 0x118  turn limit per tick at walking speed
    float roundLimitDegreeMin = 18.0f;              // 0x11c  ... at dash speed
    float roundLimitDegreeMaxStickRotate = 60.0f;   // 0x120  ... while the stick spins quickly
    float roundLimitDegreeMinStickRotate = 60.0f;   // 0x124  (name string at 0x1a9d70)
    float roundLimitInterpStart = 10.0f;            // 0x128  ...InterpolateStartStickRotateAngleSpeed
    float roundLimitInterpEnd = 50.0f;              // 0x12c  ...InterpolateEndStickRotateAngleSpeed
    float runAnimRateMax = 2.9f;                    // 0x130
    int32_t dashStartFrame = 15;                    // 0x138
    float downHillAccelStartDegree = 30.0f;         // 0x144
    float downHillAccelEndDegree = 60.0f;           // 0x148
    float downHillAccelAddRate = 2.0f;              // 0x14c
    float dashPanelSpeed = 40.0f;                   // 0x150
    float dashPanelSpeedInputReverseMin = 17.8f;    // 0x154
    float tiltDegreeMax = 40.0f;                    // 0x160
    float tiltBlendRate = 0.05f;                    // 0x164
    float tiltFrontAngleMax = 2.0f;                 // 0x168
    float tiltStartSpeed = 16.6f;                   // 0x16c
    float tiltEndSpeed = 17.0f;                     // 0x170
    uint32_t invincibleDashFrame = 30;              // 0x370
    float invincibleDashSpeed = 26.0f;              // 0x374
};

// Processed pad state (input object 0x334e4c4c, vtable 0x4be61c). The platform layer fills it.
struct PadState {
    V3 move;                    // +0xa0 (vtable +0x24) camera-relative move vector
    bool stickActive = false;   // +0x0c (vtable +0x08)
    bool dashHeld = false;      // +0xd6 (vtable +0x50)
    float stickRotate = 0;      // +0x98 (vtable +0x20 returns it * (1/30) * 57.29578, in degrees)

    float stickRotateDegrees() const {
        return (stickRotate * captain_toad::RecoveredMath::fbits(0x3d088889)) *
               captain_toad::RecoveredMath::fbits(0x42652ee0);
    }
};

// Player animation service (0x334e47d8, vtable 0x4c1ef4); only the calls the walk makes.
struct AnimationPort {
    virtual ~AnimationPort() = default;
    virtual bool isPlaying(const std::string& action) const = 0;   // +0x1c (0x3e29fc)
    virtual void start(const std::string& action) = 0;             // +0x08 (0x181fc8)
    virtual void setBlendWeights(const float w[6]) = 0;            // +0x64 (0x181c88)
    virtual void setRate(float rate) = 0;                          // +0x0c (0x2c6880)
};

// Collaborators that are inactive in the opening. Each comment names the original object.
struct Services {
    bool dashPanelActive = false;     // walk+0x1c (vtable 0x4cb420, +4 != 0); traced 0
    uint32_t invincibleCounter = 0;   // walk+0x10 (vtable 0x4c9f48) +4; ratio = counter / InvincibleDashFrame; traced 0
    bool invincibleActive = false;    // walk+0x10 vtable +4 (read on enter)
    bool subjective = false;          // walk+0x50 (vtable 0x4c54c0): asks a camera service for "Subjective"; traced 0
    bool panic = false;               // walk+0x18 selects "MovePanic"
    float speedFactor = 1.0f;         // walk+0x14 vtable +0 (0x439c78 returns 1.0)
    bool inWater = false;             // walk+0x4c vtable +4 (0x3e4ad4): SpeedDampWaterGround scale
    bool onIce = false;               // 0x2a2150: ground material "Skate"
    int mode = 0;                     // *(walk+0x2c): 0 normal, 1 mini (NormalSpeedMaxGroundMini, AnimRateMini)
    bool movingFlagSink = false;      // ctx+0x20 (vtable 0x4c37a0) +0x14 receives speed != 0
    int slipEffects = 0;              // ctx+0xc +0xc("PgSlipOnIce") calls (ice only)
};

struct Walk {
    captain_toad::RecoveredPlayerCollider::Body* body = nullptr;
    captain_toad::RecoveredPlayerCollider::Collider* collider = nullptr;
    AnimationPort* anim = nullptr;
    const PadState* pad = nullptr;
    WalkParams params;
    Services services;

    float blend = 0;              // +0x20 Move blend position (0 slow walk .. 1 run; 2 / 4 on dash starts)
    float field24 = 0;            // +0x24 (cleared on enter)
    int32_t dashFrames = 0;       // +0x28 DashStartFrame copy
    uint32_t fastTicks = 0;       // +0x30 ticks at >= 95% of the walk maximum
    V3 prevGroundNormal;          // +0x34
    int32_t overWalkMax = 0;      // +0x40 dash held above the walk maximum
    V3 recordedGroundUp;          // +0x44 object +8 (0x2a21d0)
    uint8_t speedFactorOne = 0;   // +0x48

    // The invincible walk (InvincibleWalk: the same Move action 0x2a3108 with vtable 0x4ce634)
    // overrides +0x28 (0x414c64: "Invincible"), +0x2c (0x414c24: DashSpeedMaxInvincible), +0x30
    // (0x414a34: rates 1, 1, 1, RunAnimRateMaxInvincible) and +0x3c (0x414a70: the rate from the
    // speed over the dash maximum).
    bool invincible = false;
    // The walk in water (MoveWater: vtable 0x4cd9c4, +0x28 0x3ebfe4: the action "MoveWater").
    bool water = false;

    // vtable +0x28 (0x3ea25c)
    const char* animName() const {
        if (invincible) {
            return "Invincible";
        }
        if (water) {
            return "MoveWater";
        }
        return services.panic ? "MovePanic" : "Move";
    }

    // vtable +0x34 (0x3e9fe0)
    float speedScale() const { return services.inWater ? params.speedDampWaterGround : 1.0f; }

    // 0x2a20cc: walk maximum.
    float maxSpeed() const {
        return (services.mode == 1 ? params.normalSpeedMaxGroundMini : params.normalSpeedMaxGround) * speedScale();
    }

    // vtable +0x2c (0x3ea21c): dash maximum.
    float dashMax() const { return (invincible ? params.dashSpeedMaxInvincible : params.dashSpeedMax) * speedScale(); }

    // 0x3e9588
    float invincibleRatio() const {
        return captain_toad::RecoveredMath::clamp01(float(services.invincibleCounter) /
                                                    float(params.invincibleDashFrame));
    }

    // vtable +0x30 (0x3e9f60): the maximum playback rate of blend clip i.
    float rateFor(int i) const {
        if (invincible) {
            return i == 3 ? params.runAnimRateMaxInvincible : 1.0f;
        }
        switch (i) {
        case 0:
            return params.walkSlowlyAnimRateMax;
        case 1:
            return params.walkAnimRateMax;
        case 2:
            return params.walkSpeedyAnimRateMax;
        case 3:
            return params.runAnimRateMax;
        }
        return 1.0f;
    }

    // 0x2a21d0: reset the body's +0x30 matrix and remember the current ground up.
    void recordGround() {
        static const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        memcpy(body->mtx30, identity, sizeof identity);
        recordedGroundUp = body->groundUp;
    }

    // 0x260cf8 through the collider: the ground contact list's vector.
    V3 groundInfoNormal() const { return collider->groundList().vec; }

    void ensureAnimation() {
        if (!anim->isPlaying(animName())) {
            anim->start(animName());
        }
    }

    // 0x1f3978: entering the Move state.
    void enter() {
        using namespace captain_toad::RecoveredVec;
        if (collider->onGround()) {
            prevGroundNormal = groundInfoNormal();
        } else {
            prevGroundNormal = body->groundUp;
        }
        recordGround();
        speedFactorOne = captain_toad::RecoveredMath::bitsf(services.speedFactor) == 0x3f800000u ? 1 : 0;

        // Drop a backwards horizontal velocity (unless on ice).
        float d = prevGroundNormal.x * body->vel.x;
        d = d + prevGroundNormal.y * body->vel.y;
        d = -d - prevGroundNormal.z * body->vel.z;
        V3 horizontal = addScaled(prevGroundNormal, body->vel, d);
        if (!(collider->onGround() && services.onIce)) {   // 0x2a2150 tests the ground first
            float f = horizontal.x * body->front.x;
            f = f + horizontal.y * body->front.y;
            f = f + horizontal.z * body->front.z;
            if (f < 0.0f) {
                body->vel = sub(body->vel, horizontal);
            }
        }

        // Speed in the body's horizontal plane picks the initial blend position.
        float u = body->up.x * body->vel.x;
        u = u + body->up.y * body->vel.y;
        u = -u - body->up.z * body->vel.z;
        V3 h = addScaled(body->up, body->vel, u);
        float l2 = h.x * h.x;
        l2 = l2 + h.y * h.y;
        l2 = l2 + h.z * h.z;
        const float speed = std::sqrt(l2);
        ensureAnimation();
        overWalkMax = 0;
        if (services.invincibleActive) {
            blend = 4.0f;
            dashFrames = params.dashStartFrame;
        } else if (pad->dashHeld && !(maxSpeed() >= speed)) {
            blend = 2.0f;
            dashFrames = params.dashStartFrame;
            overWalkMax = 1;
        } else {
            blend = captain_toad::RecoveredMath::clamp01(speed / maxSpeed());
            dashFrames = 0;
        }

        if (services.dashPanelActive) {
            body->vel = sub(body->vel, h);
            float h2 = h.x * h.x;
            h2 = h2 + h.y * h.y;
            h2 = h2 + h.z * h.z;
            if (h2 < captain_toad::RecoveredMath::fbits(0x358637be)) {
                h = scale(body->front, params.dashPanelSpeed);
            } else {
                const float s = params.dashPanelSpeed;
                float m2 = h.x * h.x;
                m2 = m2 + h.y * h.y;
                m2 = m2 + h.z * h.z;
                float m = std::sqrt(m2);
                if (!(m <= 0.0f)) {
                    float k = s / m;
                    h = {h.x * k, h.y * k, h.z * k};
                }
            }
            body->vel = add(body->vel, h);
            dashFrames = params.dashStartFrame;
        }
        field24 = 0.0f;
        // ctx+0x20 vtable +0 (0x194eb0): presentation hook, no movement state.
        fastTicks = (maxSpeed() * captain_toad::RecoveredMath::fbits(0x3f733333) < speed) ? 10u : 0u;
    }

    // 0x1f517c: leaving the Move state.
    void exit() {
        services.movingFlagSink = false;   // ctx+0x20 vtable +4, then +0x14(0)
        recordGround();
        body->tilt = 0.0f;
    }

    // 0x1f37b0: approach the blend position and set the four Move weights (WalkSlowly, Walk,
    // WalkSpeedy, Run; the "Move" entry of the action table). Between thresholds two neighbouring
    // clips crossfade.
    void updateBlend(float speed) {
        using captain_toad::RecoveredMath::clamp01;
        float t = clamp01(speed / dashMax());
        float diff = t - blend;
        const float rate = blend < params.runAnimBlendThres ? params.animBlendWeightApproachRate
                                                            : params.animBlendWeightApproachRateRun;
        blend = blend + diff * rate;
        const float a = params.walkAnimBlendThres;
        const float b = params.walkSpeedyAnimBlendThres;
        const float c = params.runAnimBlendThres;
        float w[6] = {0, 0, 0, 0, 0, 0};
        if (blend < b) {
            float u = clamp01((blend - a) / (b - a));
            w[0] = 1.0f - u;
            w[1] = u;
        } else if (blend < c) {
            float u = clamp01((blend - b) / (c - b));
            w[1] = 1.0f - u;
            w[2] = u;
        } else {
            float u = clamp01((blend - c) / (1.0f - c));
            w[2] = 1.0f - u;
            w[3] = u;
        }
        anim->setBlendWeights(w);
    }

    // 0x414a70 (InvincibleWalk's vtable +0x3c): playback rate from speed / dash maximum.
    void updateRateInvincible(float speed) {
        using captain_toad::RecoveredMath::clamp01;
        const float b = params.walkSpeedyAnimBlendThres;
        const float c = params.runAnimBlendThres;
        const float t = clamp01(speed / dashMax());
        float r;
        if (t < c) {
            const float u = clamp01((t - b) / (c - b));
            r = rateFor(1) * (1.0f - u);
            r = r + rateFor(2) * u;
        } else {
            const float u = clamp01((t - c) / (1.0f - c));
            r = rateFor(2) * (1.0f - u);
            r = r + rateFor(3) * u;
        }
        anim->setRate(r);
    }

    // 0x3ea02c (vtable +0x3c; its speed argument is unused): playback rate from the blend position.
    void updateRate() {
        using captain_toad::RecoveredMath::clamp01;
        const float a = params.walkAnimBlendThres;
        const float b = params.walkSpeedyAnimBlendThres;
        const float c = params.runAnimBlendThres;
        float r;
        if (blend < b) {
            float u = clamp01((blend - a) / (b - a));
            r = rateFor(0) * (1.0f - u);
            r = r + rateFor(1) * u;
        } else if (blend < c) {
            float u = clamp01((blend - b) / (c - b));
            r = rateFor(1) * (1.0f - u);
            r = r + rateFor(2) * u;
        } else {
            float u = clamp01((blend - c) / (1.0f - c));
            r = rateFor(2) * (1.0f - u);
            r = r + rateFor(3) * u;
        }
        if (services.mode == 1) {
            r = params.animRateMini * r;
        }
        anim->setRate(r);
    }

    // 0x1f4ee4: forward tilt from the turn between the previous and the new front, only above
    // walking speed (the protagonist leans into turns while running).
    void updateTilt(const V3& oldFront) {
        using namespace captain_toad::RecoveredVec;
        using captain_toad::RecoveredMath::fbits;
        const float maxAngle = params.tiltFrontAngleMax;
        float target = 0.0f;
        float magnitude = maxAngle;
        if (magnitude <= 0.0f) {
            magnitude = -magnitude;
        }
        if (!(magnitude < fbits(0x3a83126f))) {
            float forwardSpeed = body->vel.x * body->front.x;
            forwardSpeed = forwardSpeed + body->vel.y * body->front.y;
            forwardSpeed = forwardSpeed + body->vel.z * body->front.z;
            float speedRatio = forwardSpeed - params.tiltStartSpeed;
            speedRatio =
                captain_toad::RecoveredMath::clamp01(speedRatio / (params.tiltEndSpeed - params.tiltStartSpeed));
            // Signed turn angle (degrees) between the old and new front in the ground plane.
            const V3& g = body->groundUp;
            float k = g.x * oldFront.x;
            k = k + g.y * oldFront.y;
            k = -k - g.z * oldFront.z;
            V3 a = addScaled(g, oldFront, k);
            k = g.x * body->front.x;
            k = k + g.y * body->front.y;
            k = -k - g.z * body->front.z;
            V3 b = addScaled(g, body->front, k);
            float d = a.x * b.x;
            d = d + a.y * b.y;
            d = d + a.z * b.z;
            V3 c = cross(a, b);
            float c2 = c.x * c.x;
            c2 = c2 + c.y * c.y;
            c2 = c2 + c.z * c.z;
            float degrees = captain_toad::RecoveredMath::atan2f(std::sqrt(c2), d) * fbits(0x42652ee0);
            c = cross(a, b);
            float s = g.x * c.x;
            s = s + g.y * c.y;
            s = s + g.z * c.z;
            if (0.0f > s) {
                degrees = -degrees;
            }
            if (degrees < -maxAngle) {
                degrees = -maxAngle;
            } else if (degrees > maxAngle) {
                degrees = maxAngle;
            }
            float ratio = degrees / maxAngle;
            float v = params.tiltDegreeMax * (speedRatio * ratio);
            v = v * fbits(0x40490fdb);        // * pi
            target = v * fbits(0x3bb60b61);   // / 180: degrees -> radians
        }
        // Ease towards the target tilt.
        float t = body->tilt * (1.0f - params.tiltBlendRate);
        body->tilt = t + target * params.tiltBlendRate;
    }

    // 0x1f3e04: one tick of walking.
    void update() {
        using namespace captain_toad::RecoveredVec;
        using captain_toad::RecoveredMath::bitsf;
        using captain_toad::RecoveredMath::clamp01;
        using captain_toad::RecoveredMath::decayToZero;
        using captain_toad::RecoveredMath::fbits;
        using captain_toad::RecoveredMath::Quat;
        ensureAnimation();
        V3 move = pad->move;       // s21, s19, s20
        const float one = 1.0f;    // s18
        const float zero = 0.0f;   // s17

        // 1. Stick rescale between the walk and run thresholds (not while dashing or on a dash panel).
        if (!pad->dashHeld && !services.dashPanelActive) {
            const float runThreshold = params.runAnimBlendThres;   // s22
            float l2 = move.x * move.x;
            l2 = l2 + move.y * move.y;
            l2 = l2 + move.z * move.z;
            float length = std::sqrt(l2);
            if (!(length > runThreshold) && length > params.leftStickMinWalkThreshold) {
                float x = params.leftStickRate * move.x;
                float z = params.leftStickRate * move.z;
                float target = runThreshold - params.leftStickRunThresholdDiff;
                float m2 = x * x;
                m2 = m2 + move.y * move.y;
                m2 = m2 + z * z;
                float k = zero;
                if (!(m2 <= zero)) {
                    float inverse = one / std::sqrt(m2);
                    k = inverse * target;
                }
                move = {x * k, move.y * k, z * k};
            }
        }

        // 2. Ground normal; carry the velocity over a change of the ground plane.
        V3 groundNormal = body->groundUp;                                 // sp+0x48
        const bool ice = collider->onGround() ? services.onIce : false;   // r5 (0x2a2150 only when on ground)
        if (collider->onGround()) {
            groundNormal = groundInfoNormal();
            V3 axis;
            float angle = zero;
            Quat q{0, 0, 0, 1};
            if (captain_toad::RecoveredMath::rotationBetween(axis, angle, prevGroundNormal, groundNormal)) {
                q = captain_toad::RecoveredMath::fromAxisAngle(axis, angle * one);
            }
            prevGroundNormal = groundNormal;
            captain_toad::RecoveredMath::rotate(body->vel, q);
            float d = groundNormal.x * body->vel.x;
            d = d + groundNormal.y * body->vel.y;
            d = -d - groundNormal.z * body->vel.z;
            body->vel = addScaled(groundNormal, body->vel, d);
        }

        // Ground-aligned frame: front, normal and side axes rotated onto the ground plane.
        const V3 oldFront = body->front;   // sp+0xf8
        V3 front = body->front;            // sp+4
        const V3 up = body->groundUp;      // sp+0x54
        V3 side = cross(up, front);        // sp+0xec
        normalize(side);
        front = cross(side, up);
        normalize(front);
        V3 frontG = front;           // sp+0x78
        V3 normalG = groundNormal;   // sp+0x28
        V3 sideG = side;             // sp+0x38
        Quat q2{0, 0, 0, 1};         // sp+0x88
        {
            V3 axis;
            float angle = zero;
            if (captain_toad::RecoveredMath::rotationBetween(axis, angle, up, groundNormal)) {
                q2 = captain_toad::RecoveredMath::fromAxisAngle(axis, angle * one);
            }
            captain_toad::RecoveredMath::rotate(frontG, q2);
            captain_toad::RecoveredMath::rotate(sideG, q2);
        }

        // The velocity's three components in that frame.
        const V3 vel = body->vel;
        float forwardSpeed = frontG.x * vel.x;   // s16
        forwardSpeed = forwardSpeed + frontG.y * vel.y;
        forwardSpeed = forwardSpeed + frontG.z * vel.z;
        float normalSpeed = normalG.x * vel.x;   // s22
        normalSpeed = normalSpeed + normalG.y * vel.y;
        normalSpeed = normalSpeed + normalG.z * vel.z;
        float sideSpeedIn = sideG.x * vel.x;   // s23
        sideSpeedIn = sideSpeedIn + sideG.y * vel.y;
        sideSpeedIn = sideSpeedIn + sideG.z * vel.z;

        uint32_t brake = pad->dashHeld ? params.dashBrakeFrame : params.brakeFrame;   // sb
        if (ice) {
            brake = params.brakeFrameOnIce;
        }
        {
            const float limit = maxSpeed() * fbits(0x3f733333);   // 95% of the walk maximum
            if (forwardSpeed > limit || ice) {
                fastTicks++;
            } else if (fastTicks < 10) {
                fastTicks = 0;
            }
        }
        if (bitsf(forwardSpeed) > 0xbdcccccdu && ice) {
            services.slipEffects++;   // "PgSlipOnIce"
        }

        // 3. Sideways speed decays.
        const float sideSpeed = decayToZero(brake, sideSpeedIn, maxSpeed());   // s24
        const float eps = fbits(0x358637be);
        const float accelTolerance = fbits(0x3a83126f);   // s25
        float turnLimit = 0;                              // s25 later

        // 4. Forward speed.
        if (bitsf(forwardSpeed) > 0xbdcccccdu) {
            // Moving backwards faster than 0.1: brake.
            forwardSpeed = decayToZero(brake, forwardSpeed, maxSpeed());
        } else if (!pad->stickActive && !services.dashPanelActive) {
            // 0x1f4b58: no stick: slow down (gently after running for a while).
            forwardSpeed = fastTicks >= 10 ? decayToZero(brake, forwardSpeed, maxSpeed())
                                           : decayToZero(1, forwardSpeed, maxSpeed());
        } else {
            if (services.dashPanelActive) {
                float m2 = move.x * move.x;
                m2 = m2 + move.y * move.y;
                m2 = m2 + move.z * move.z;
                if (m2 < eps) {
                    move = front;
                }
            }
            V3 direction = move;   // sp+0xbc
            normalize(direction);
            float c = direction.x * front.x;
            c = c + direction.y * front.y;
            c = c + direction.z * front.z;
            const float s26 = fbits(0x3dcccccd);   // 0.1
            if (bitsf(c) > 0xbe31d0d4u) {
                // The stick points more than about 100 degrees away from the front: brake.
                if (services.dashPanelActive) {
                    float keep = forwardSpeed * fbits(0x3f7d70a4);
                    float minimum = params.dashPanelSpeedInputReverseMin;
                    forwardSpeed = (keep <= minimum) ? minimum : keep;
                } else {
                    forwardSpeed =
                        captain_toad::RecoveredMath::zeroBelow(decayToZero(brake, forwardSpeed, maxSpeed()), s26);
                }
            } else {
                // 0x1f450c: the desired direction in the ground plane.
                V3 want = move;   // sp+0x14
                float d = up.x * want.x;
                d = d + up.y * want.y;
                d = -d - up.z * want.z;
                want = addScaled(up, want, d);
                normalize(want);

                // Target speed: walk or dash maximum (or the invincible dash blend).
                float target;   // s28
                const float ratio = invincibleRatio();
                if (ratio > zero) {
                    float s = params.invincibleDashSpeed * speedScale();
                    s = s * ratio;
                    target = s + dashMax() * (one - ratio);
                } else if (pad->dashHeld) {
                    target = dashMax();
                } else {
                    target = maxSpeed();
                }
                // Scaled by the stick tilt: from WalkSpeedRateMin at the threshold to 1 at full tilt.
                float rate = params.walkSpeedRateMin;
                const float span = one - params.walkSpeedRateMin;
                float l2 = move.x * move.x;
                l2 = l2 + move.y * move.y;
                l2 = l2 + move.z * move.z;
                rate = rate + span * std::sqrt(l2);
                float wantSpeed = rate * target;   // s20

                // Downhill acceleration from the front's angle below the horizontal plane.
                const float range = params.downHillAccelEndDegree - params.downHillAccelStartDegree;
                float fd = frontG.x * body->groundUp.x;
                fd = fd + frontG.y * body->groundUp.y;
                fd = fd + frontG.z * body->groundUp.z;
                float slope = captain_toad::RecoveredMath::acosf(fd) * fbits(0x42652ee0);
                slope = slope - fbits(0x42b40000);   // - 90 degrees
                float t = clamp01((slope - params.downHillAccelStartDegree) / range);
                float boost = one + params.downHillAccelAddRate * t;
                float maximum = wantSpeed * boost;   // s19
                if (services.dashPanelActive) {
                    maximum = params.dashPanelSpeed;
                    if (forwardSpeed < maximum) {
                        forwardSpeed = maximum;
                    }
                    dashFrames = params.dashStartFrame;
                }
                if (speedFactorOne) {
                    maximum = services.speedFactor * maximum;
                } else if (bitsf(services.speedFactor) == 0x3f800000u) {
                    speedFactorOne = 1;
                }

                // Accelerate: quickly up to the walk maximum, slowly beyond it (dash).
                if (maximum > forwardSpeed) {
                    float diff = forwardSpeed - maxSpeed();
                    float step;
                    bool walkAccel = false;
                    if (diff < zero) {
                        diff = -diff;
                        if (!(accelTolerance > diff)) {
                            walkAccel = true;
                        }
                    }
                    if (walkAccel) {
                        step = ice ? s26 : maxSpeed() / float(params.accelFrame);
                    } else {
                        step = dashMax() / float(params.dashAccelFrame);
                    }
                    forwardSpeed = captain_toad::RecoveredMath::addClampMax(forwardSpeed, maximum, step);
                }
                if (forwardSpeed < zero) {
                    forwardSpeed = zero;
                } else if (forwardSpeed > maximum) {
                    // Brake towards the target.
                    if (dashMax() < forwardSpeed) {
                        forwardSpeed = decayToZero(brake, forwardSpeed, maxSpeed());
                    } else {
                        forwardSpeed = decayToZero(params.stickOnBrakeFrame, forwardSpeed, maxSpeed());
                    }
                    if (forwardSpeed < maximum) {
                        forwardSpeed = maximum;
                    }
                }

                // 5. Turn towards the stick, limited per tick.
                if (!ice) {
                    // The limit blends the walk and dash limits by speed, each interpolated by how
                    // fast the stick itself is rotating.
                    float tDash = clamp01((forwardSpeed - maxSpeed()) / (dashMax() - maxSpeed()));
                    float rotation = pad->stickRotateDegrees();
                    rotation = rotation > zero ? rotation : -rotation;
                    rotation = rotation - params.roundLimitInterpStart;
                    float tRotation = clamp01(rotation / (params.roundLimitInterpEnd - params.roundLimitInterpStart));
                    float dashLimit;   // s20
                    if (services.subjective) {
                        dashLimit = one;
                    } else {
                        dashLimit = params.roundLimitDegreeMinStickRotate * tRotation;
                        dashLimit = dashLimit + (one - tRotation) * params.roundLimitDegreeMin;
                    }
                    dashLimit = dashLimit * tDash;
                    float walkLimit;   // s25
                    if (services.subjective) {
                        walkLimit = fbits(0x40000000);
                    } else {
                        walkLimit = params.roundLimitDegreeMaxStickRotate * tRotation;
                        walkLimit = walkLimit + (one - tRotation) * params.roundLimitDegreeMax;
                    }
                    dashLimit = dashLimit + walkLimit * (one - tDash);
                    turnLimit = dashLimit * fbits(0x3c8efa35);   // degrees -> radians
                    V3 axis;
                    float angle = zero;
                    Quat q{0, 0, 0, 1};
                    if (captain_toad::RecoveredMath::rotationBetween(axis, angle, front, want)) {
                        float k = clamp01(turnLimit / angle);
                        q = captain_toad::RecoveredMath::fromAxisAngle(axis, angle * k);
                    }
                    captain_toad::RecoveredMath::rotate(front, q);
                    normalize(front);
                    body->front = front;
                }
            }
        }

        if (!ice) {
            // 0x1f4ba8 (also the steering block's exit): rotate the front by q2 inline and rebuild the
            // side axis in the new ground plane.
            const float qx = q2.x, qy = q2.y, qz = q2.z, qw = q2.w;
            const float fx = front.x, fy = front.y, fz = front.z;
            float s7 = -(qx * fx);
            float s5 = qx * fy;
            float s3 = qy * fz;
            float s4 = qz * fx;
            s7 = s7 - qy * fy;
            s5 = s5 - qy * fx;
            s3 = s3 - qz * fy;
            s4 = s4 - qx * fz;
            s7 = s7 - qz * fz;
            s5 = s5 + qw * fz;
            s3 = s3 + qw * fx;
            s4 = s4 + qw * fy;
            float ox = s3 * qw;
            float oy = s4 * qw;
            float oz = s4 * qx;
            ox = ox - s4 * qz;
            oy = oy + s3 * qz;
            oz = oz - s3 * qy;
            ox = ox + s5 * qy;
            oy = oy - s5 * qx;
            oz = oz + s5 * qw;
            ox = ox - s7 * qx;
            oy = oy - s7 * qy;
            oz = oz - s7 * qz;
            frontG = {ox, oy, oz};
            sideG = cross(normalG, frontG);
            normalize(sideG);
        }

        // 6. 0x1f4c4c: compose the velocity.
        V3 a = scale(frontG, forwardSpeed);
        V3 b = scale(normalG, normalSpeed);
        V3 ab = add(a, b);
        V3 c = scale(sideG, sideSpeed);
        body->vel = add(ab, c);

        // Horizontal speed drives the animation blend.
        V3 horizontal = body->vel;
        float d = normalG.x * horizontal.x;
        d = d + normalG.y * horizontal.y;
        d = -d - normalG.z * horizontal.z;
        horizontal = addScaled(normalG, horizontal, d);
        float h2 = horizontal.x * horizontal.x;
        h2 = h2 + horizontal.y * horizontal.y;
        h2 = h2 + horizontal.z * horizontal.z;
        const float speed = std::sqrt(h2);   // s16
        if (ice) {
            V3 m = pad->move;
            float m2 = m.x * m.x;
            m2 = m2 + m.y * m.y;
            m2 = m2 + m.z * m.z;
            if (!(m2 < eps)) {
                normalize(m);
                body->front = m;
            }
        }
        services.movingFlagSink = speed != 0.0f;
        if (pad->dashHeld && maxSpeed() < speed) {
            if (overWalkMax != 1) {
                overWalkMax = 1;
            }
        } else if (overWalkMax != 0) {
            overWalkMax = 0;
        }
        if (!anim->isPlaying("Invincible")) {
            updateBlend(speed);
        }
        if (invincible) {
            updateRateInvincible(speed);
        } else {
            updateRate();
        }
        updateTilt(oldFront);
        recordGround();
    }
};

}   // namespace captain_toad::RecoveredPlayerWalk
