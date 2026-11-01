// =============================================================================================
// PlayerWater.hpp - the protagonist and the water: getting wet, drying, splashing
// =============================================================================================
//
// What the player sees: when the protagonist walks into deep water his head goes under; after a
// while under water, once he comes out, he drips for some seconds. Crossing the water surface
// throws up a splash.
//
// Wet state (the player's update 0x15b698 -> 0x15d118; parameters from the player's table):
//   The routine runs every second call (counter +0x2cc). Its check point is the feet plus
//   WetCheckHeight along up (Super 140, Mini 80; param getters +0x180 / +0x184).
//   - Check point in a WaterArea (0x2d7468): the time under water (+0x12c) grows by 2; a running
//     dry timer (+0x164, the countdown class of the invulnerability counter) is cleared and, if
//     the model plays "WetStart", it gets "WetEnd" and the effects WetBody and WetHead stop
//     (0x2f312c).
//   - Otherwise: when the time under water has reached ToWetInWaterFrame (60) - or the player is
//     in a PlayerWetArea with the timer stopped - the time is cleared, the timer is set to
//     WetToDryFrame (300), the model starts "WetStart" and (outside a PlayerWetArea) the effects
//     WetHead and WetBody start (0x2ebfec). While the timer runs, "WetStart" is kept on (restarted
//     with the effects if it stopped); when it has run out, "WetStart" gets "WetEnd" and the
//     effects stop.
//   Hypothesis: the timer counts down once per player tick (the countdown class, as the
//   invulnerability counter's 0x1feab4). Not ported: PlayerWetArea (none in the ported courses)
//   and the model's WetStart / WetEnd animations (in KinopioAnimation.bch; not exported yet).
//
// Water surface (the detector 0x20eacc, per update of the watched point): the segment from the
// point's previous position to its current one (at most 100 long, literal 0x20ebbc) is tested
// against the water areas' boxes (0x2c09a0); the face's normal decides the hit reaction: up
// (y > 0.996) 水柱ヒット[水面], down 水柱ヒット[ベース], otherwise 水柱ヒット[横], played at the
// crossing point. Hypothesis: the watched point is the protagonist's position (feet).
// =============================================================================================
#pragma once

#include "game/VecLib.hpp"

#include <functional>
#include <string>
#include <vector>

namespace captain_toad::RecoveredPlayerWater {

using captain_toad::RecoveredVec::V3;

struct Params {
    int toWetInWaterFrame = 60;   // 0x180 ToWetInWaterFrame
    int wetToDryFrame = 300;      // 0x17c WetToDryFrame
    float wetCheckHeightSuper = 140.0f;
    float wetCheckHeightMini = 80.0f;
};

// An axis-aligned water box (WaterArea: x, z in centre +- 500 scale, y in [base, base + 1000 scale]).
struct WaterBox {
    V3 min, max;
};

struct Wet {
    Params params;
    uint32_t calls = 0;      // +0x2cc
    int underTime = 0;       // +0x12c
    int timer = 0;           // +0x164 -> +4
    bool wetStart = false;   // the model plays "WetStart"
    bool effects = false;    // WetHead / WetBody running
    // Events of this update for the effect keeper.
    bool startEffects = false, stopEffects = false;

    // One player tick. `inWater(p)`: the point is inside a WaterArea.
    void tick(const V3& pos, const V3& up, bool mini, const std::function<bool(const V3&)>& inWater) {
        startEffects = stopEffects = false;
        if (timer > 0) {
            timer--;   // the countdown (hypothesis: once per player tick)
        }
        calls++;
        if (calls & 1) {
            return;
        }
        const float h = mini ? params.wetCheckHeightMini : params.wetCheckHeightSuper;
        const V3 check = captain_toad::RecoveredVec::add(pos, captain_toad::RecoveredVec::scale(up, h));
        if (inWater(check)) {
            underTime += 2;
            if (timer == 0) {
                return;
            }
            timer = 0;
            if (wetStart) {
                wetStart = false;
                stop();
            }
            return;
        }
        if (params.toWetInWaterFrame <= underTime) {
            underTime = 0;
            timer = params.wetToDryFrame;
            wetStart = true;
            start();
        }
        if (timer > 0) {
            if (!wetStart) {
                wetStart = true;
                start();
            }
            return;
        }
        if (wetStart) {
            wetStart = false;
            stop();
        }
    }
    void start() {
        startEffects = true;
        effects = true;
    }
    void stop() {
        stopEffects = true;
        effects = false;
    }
};

// The surface detector: returns the reaction name for a crossing of a box face by the segment
// from `from` to `to` (empty: none) and the crossing point.
inline std::string surfaceCrossing(const std::vector<WaterBox>& boxes, const V3& from, const V3& to, V3& at) {
    const V3 d = captain_toad::RecoveredVec::sub(to, from);
    if (captain_toad::RecoveredVec::dot(d, d) > 100.0f * 100.0f) {   // literal 0x20ebbc
        return {};
    }
    for (const WaterBox& b : boxes) {
        // The nearest face the segment passes through (slab test on the faces' planes).
        float best = 2.0f;
        int axis = -1;
        const float lo[3] = {b.min.x, b.min.y, b.min.z}, hi[3] = {b.max.x, b.max.y, b.max.z};
        const float f[3] = {from.x, from.y, from.z}, t[3] = {to.x, to.y, to.z};
        for (int k = 0; k < 3; k++) {
            for (int side = 0; side < 2; side++) {
                const float plane = side ? hi[k] : lo[k];
                const float a = f[k] - plane, c = t[k] - plane;
                if ((a < 0) == (c < 0) || a == c) {
                    continue;
                }
                const float s = a / (a - c);
                float p[3];
                for (int j = 0; j < 3; j++) {
                    p[j] = f[j] + (t[j] - f[j]) * s;
                }
                bool inside = true;
                for (int j = 0; j < 3; j++) {
                    if (j != k && (p[j] < lo[j] || p[j] > hi[j])) {
                        inside = false;
                    }
                }
                if (inside && s < best) {
                    best = s;
                    axis = k * 2 + side;
                    at = {p[0], p[1], p[2]};
                }
            }
        }
        if (axis < 0) {
            continue;
        }
        if (axis == 3) {
            return "水柱ヒット[水面]";   // the top face (normal up)
        }
        if (axis == 2) {
            return "水柱ヒット[ベース]";
        }
        return "水柱ヒット[横]";
    }
    return {};
}

}   // namespace captain_toad::RecoveredPlayerWater
