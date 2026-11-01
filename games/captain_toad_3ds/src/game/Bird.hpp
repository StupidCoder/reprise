// Recovered bird behaviour: the two small birds that peck on the ground in the opening and fly
// away when the player comes close (original game, ARM11 main code; Ghidra exports in
// evidence/analysis/bird-a..f, wiring from tools/arm_disasm.py).
//
// Validation: the idle behaviour against the state-only trace evidence/runs/gpu-trace-birds-a
// (tools/bird_trace.py, evidence/behaviours/validation.json); the flee by tools/validate_birds.cpp
// (evidence/birds/validation.txt).
//
// The bird's state machine (nerves, see Nerve.hpp). Init 0x38e378 reads IsPerch and starts in N0.
// State objects 0x4f5498..0x4f54a8, vtables 0x47353c..0x47357c (static init 0x44f6c4):
//   N0 0x4075b4 GroundWait  At step 0 play GroundWaitA if a random number < 0.5, else GroundWaitB.
//                           A trigger (0x3f12e0 on +0x10) starts the flee (0x26cf9c). When the clip
//                           ends, turn (N1).
//   N1 0x38e4b0 Turn        At step 0 play Turn and pick a yaw rate of +-(30 + 60u)/6 degrees per
//                           tick (a second draw < 0.5 negates it). Rotate about Y every step
//                           (0x2dc638 -> 0x286220, degrees). At step >= 6 (after rotating) go back
//                           to N0. Seven rotations happen, so the bird turns 7/6 of the drawn angle.
//   N2 0x407540 Flee        At step 0 play FlyWait. Every step: velocity += 2.6 * flee direction
//                           (0x2c05f4), velocity *= 0.97 (0x2fa28c), then the actor moves by its
//                           velocity. At step >= 90 the virtual +0x18 (0x2f7b04 -> +0x1c = 0x2fc130)
//                           ends the actor: dead flag (*(+0x78) = 1), components released. The
//                           original's keeper step then stays at 91 (no further updates; run
//                           bird-flee-long-a to call 2400, no revival). Native: dead and hidden (the
//                           bird is about 4000 units away by then).
//   N3 0x38df30 revival fly-in and N4 0x38e678 perch variant are not reached in the opening.
//
// What makes a bird flee (evidence/analysis/bird-d/-e, runs bird-flee-exec-a, bird-flee-sensors-a):
//   The player's sensor contact handlers (0x1590ec / 0x15927c / 0x159428) send message 0x4d5130
//   (0x2d934c) when the player's Head sensor (radius 55) overlaps the bird's Body sensor (radius
//   400, at the bird position). The bird's receiveMsg 0x38dc08 accepts it in N0/N1 and calls 0x26d190
//   with the horizontal unit vector from the Head to the Body (0x2cab90). Sensor positions are those
//   stored on the previous tick (as for the companion's push); the flee state's first step runs on
//   the following tick. The microphone trigger 0x3f12e0 (shared input level > 0x4000, i.e. blowing
//   into the mic) and the 300-tick perch counter 0x38dd14 are not reached here.
//
// Random numbers: 0x2f934c is a xorshift128 over the global state at *0x4f8064, and
// unit = float(0x3f800000 | r >> 9) - 1. That generator is shared by all actors (and the sound
// system), so the exact draw sequence depends on everything else in the frame. The port uses the
// same algorithm; the laws are exact, the particular draws may differ.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace captain_toad::RecoveredBird {

// 0x2f934c: the game's shared xorshift128 random generator.
struct XorShift128 {
    uint32_t s[4] = {123456789u, 362436069u, 521288629u, 88675123u};

    uint32_t next() {
        uint32_t t = s[0] ^ (s[0] << 11);
        t = t ^ (t >> 8) ^ s[3] ^ (s[3] >> 19);
        s[0] = s[1];
        s[1] = s[2];
        s[2] = s[3];
        s[3] = t;
        return t;
    }

    // A number in [0, 1): the top 23 random bits become the mantissa of a float in [1, 2).
    float unit() {
        uint32_t bits = (next() >> 9) | 0x3f800000u;
        float f;
        memcpy(&f, &bits, 4);
        return f - 1.0f;
    }
};

// 0x2fd3a4: normalise; a zero-length vector stays unchanged.
inline void normalise(float v[3]) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length <= 0.0f) {
        return;
    }
    const float k = 1.0f / length;
    v[0] *= k;
    v[1] *= k;
    v[2] *= k;
}

// 0x2dc0b8: normalise; below a squared length of 1e-6 the vector becomes zero.
inline void normaliseOrZero(float v[3]) {
    if (v[0] * v[0] + v[1] * v[1] + v[2] * v[2] < 1e-6f) {
        v[0] = v[1] = v[2] = 0;
        return;
    }
    normalise(v);
}

struct Bird {
    enum State { GroundWait, Turn, Flee };

    State state = GroundWait;
    int step = 0;        // nerve step (0 on a state's first execution; -1 right after a change)
    float yaw = 0;       // degrees about +Y (placement rotation)
    float yawRate = 0;   // +0x90, degrees per tick while turning
    std::string clip = "GroundWaitB";
    float clipFrame = 0;   // current action frame
    float clipEnd = 70;    // and its end frame
    bool loop = false;
    bool dead = false;
    float pos[3] = {0, 0, 0};        // actor translation (= Body sensor centre)
    float velocity[3] = {0, 0, 0};   // actor velocity (vfunc +0x1c)
    float flee[3] = {0, 0, 0};       // +0x94 flee direction

    static constexpr float kBodyRadius = 400.0f;   // InitSensor "Body"

    // Clip lengths from the bird's model archive (GroundWaitA 39, GroundWaitB 70, Turn 4,
    // FlyWait 12 looped).
    static float length(const std::string& c) {
        if (c == "GroundWaitA") {
            return 39.f;
        }
        if (c == "GroundWaitB") {
            return 70.f;
        }
        if (c == "Turn") {
            return 4.f;
        }
        return 12.f;
    }

    int plays = 0;   // clip starts so far (observers detect a new or restarted clip)
    void play(const std::string& c) {
        plays++;
        clip = c;
        clipFrame = 0;
        clipEnd = length(c);
        loop = c == "FlyWait";
    }

    // Action end (0x2fa1b8: frame >= end frame), evaluated after the action has advanced.
    bool actionEnd() const { return clipFrame >= clipEnd; }

    // One 30 Hz game update. Traced order (gpu-trace-birds-a): the action advances one frame
    // (clamped at its end), then the state body runs. A state change takes effect with step 0 on
    // the next update (the keeper stores step -1, the update increments it).
    void tick(XorShift128& rng) {
        if (dead) {
            return;
        }
        clipFrame = clipFrame + 1.0f;
        if (clipFrame >= clipEnd) {
            clipFrame = loop ? clipFrame - clipEnd : clipEnd;
        }
        step++;

        if (state == Flee) {   // 0x407540
            if (step == 0) {
                play("FlyWait");
            }
            float acceleration[3] = {flee[0], flee[1], flee[2]};
            normaliseOrZero(acceleration);
            for (int i = 0; i < 3; i++) {
                velocity[i] = velocity[i] + acceleration[i] * 2.6f;
            }
            for (int i = 0; i < 3; i++) {
                velocity[i] = velocity[i] * 0.97f;   // air drag
            }
            if (step >= 90) {
                dead = true;
                return;
            }
            for (int i = 0; i < 3; i++) {
                pos[i] += velocity[i];
            }
            return;
        }

        if (state == GroundWait) {   // 0x4075b4
            if (step == 0) {
                play(rng.unit() < 0.5f ? "GroundWaitA" : "GroundWaitB");
            }
            if (actionEnd()) {
                setState(Turn);
            }
        } else {   // Turn, 0x38e4b0
            if (step == 0) {
                play("Turn");
                float angle = 30.0f + rng.unit() * 60.0f;   // one VMLA: 30 + u*60
                if (rng.unit() < 0.5f) {
                    angle = -angle;
                }
                yawRate = angle * 0.16666667f;   // 0x3e2aaaab = 1/6
            }
            yaw += yawRate;   // 0x2dc638: rotate about Y (degrees)
            if (step >= 6) {
                setState(GroundWait);
            }
        }
    }

    void setState(State s) {
        state = s;
        step = -1;
    }

    // Player Head sensor contact (message 0x4d5130 -> 0x38dc08). `head` is the stored Head sensor
    // centre of the previous tick. Returns true when the bird starts fleeing.
    bool headContact(const float head[3], float headRadius = 55.0f) {
        if (dead || (state != GroundWait && state != Turn)) {
            return false;
        }
        const float d[3] = {pos[0] - head[0], pos[1] - head[1], pos[2] - head[2]};
        const float r = headRadius + kBodyRadius;
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > r * r) {
            return false;   // no sensor overlap (boundary case "<=" is a hypothesis)
        }
        float away[3] = {d[0], 0.0f, d[2]};   // 0x2cab90: horizontal direction away from the player
        normaliseOrZero(away);
        startFlee(away);
        return true;
    }

    // 0x26d190: flee direction, facing and upward tilt; then nerve N2.
    void startFlee(const float d[3]) {
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] >= 1e-6f) {
            flee[0] = d[0];
            flee[1] = d[1];
            flee[2] = d[2];
        } else {
            flee[0] = -0.0f;
            flee[1] = -0.0f;
            flee[2] = -1.0f;
        }
        const float tilted[3] = {flee[0], flee[1] - 1.0f, flee[2]};
        if (std::sqrt(tilted[0] * tilted[0] + tilted[1] * tilted[1] + tilted[2] * tilted[2]) <= 0.001f) {
            flee[0] = -0.0f;
            flee[1] = -0.0f;
            flee[2] = -1.0f;
        }
        // The bird faces its flee direction (0x2edc80, up +Y). This field holds the negated Y
        // rotation (see session/SessionRender.cpp).
        yaw = -float(std::atan2(double(flee[0]), double(flee[2])) * 180.0 / M_PI);
        // Fly away and upwards: (x, 1, z), normalised.
        flee[1] = 1.0f;
        if (flee[0] * flee[0] + 1.0f + flee[2] * flee[2] >= 1e-6f) {
            normalise(flee);
        }
        setState(Flee);
    }
};

}   // namespace captain_toad::RecoveredBird
