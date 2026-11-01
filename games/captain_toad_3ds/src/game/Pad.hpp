// Recovered player pad processing (original game, ARM11 main code): the boundary between the
// platform's controls and the player's action machine.
//
// What it does, in player terms:
//   - The 3DS circle pad reports raw counts. The system library converts them into a unit-length
//     stick vector, with a dead zone and an outer range that adapts to how far the player
//     actually pushes the pad.
//   - The game maps the stick onto the ground plane relative to the camera ("up" walks away from
//     the camera), so moving feels the same whichever way the camera looks.
//   - The stick angle is "snapped": near the four axis directions and the four diagonals it locks
//     onto them. Walking straight along a corridor is then easy even with an imprecise thumb.
//     Near walls that face the camera, a wider snap blends in, so the protagonist walks along the
//     wall rather than bumping into it.
//   - The pad also tracks how far the stick has been rotated in one direction (a spinning gesture
//     the action machine can read).
//
// Corrected decompilation with the original operation order (compile with -ffp-contract=off):
//   0x27df28  SDK circle pad conversion (raw counts -> clamped unit pair, adaptive outer range);
//             called once per 60 Hz frame for the circle pad (state object 0x3104d76c).
//   0x3e31c8  controller wrapper stick (vtable 0x4c374c +0x3c). In this build its D-pad path is dead
//             (0x2c7a38 returns 0), so it returns the pad library's analog pair (0x2b7718).
//   0x2d7edc  player pad update (pad object 0x334e4c4c, vtable 0x4be61c), once per 30 Hz game tick.
//   0x15fa14  camera move basis, 0x15fbc4 stick rotation tracker.
//   0x15ff0c  stick adjust: angle snap 0x2d6100 (or 0x2d62f8), blended towards the wide-x snap while
//             the along-wall timer runs (a collider contact whose normal faces the camera).
// Not ported because inactive in the opening: the area-dependent branches ("StickFixArea",
// "StickSnapOffArea", "PlayerAlongWallArea", "PlayerWidenStickXSnapArea", "PlayerInclinedControlArea";
// none exist in the opening map), the move direction lock (+0x114, 0x15f7f0; never set in the traced
// runs) and the reset paths (+0xf8, +0x10c, 0x2d7420).
#pragma once

#include <cstdint>

#include "game/Math.hpp"

namespace captain_toad::RecoveredPad {

using captain_toad::RecoveredMath::bitsf;
using captain_toad::RecoveredMath::fbits;
using captain_toad::RecoveredVec::V3;

// ---- Circle pad conversion (system library) ------------------------------------------------------

// SDK circle pad conversion state (0x24 bytes at 0x3104d76c; values at the opening checkpoint).
struct CirclePadState {
    int16_t cal[7] = {40, 36, 40, 145, 145, 145, 141};   // calibration, +0x0..+0xa and +0xe (index 6)
    uint8_t mode = 0;                                    // +0xc
    float rangeScale = 1.5f;                             // +0x10
    float outer = 101.0f;                                // +0x14 adaptive outer range
    float growth = -1.0f;                                // +0x18
    float lastLength = 0.0f;                             // +0x1c
    float lastDelta = 0.0f;                              // +0x20
};

// 0x27df28(state, &x, &y, rawX, rawY): raw counts -> stick pair of length <= 1.
inline void convertCirclePad(CirclePadState& st, int16_t rawX, int16_t rawY, float& outX, float& outY) {
    const float zero = 0.0f;
    float s0 = zero;
    float s1 = 0.0f;   // inner (dead zone) radius
    float s2, s3, s4, s5, s6;
    if (st.mode == 0) {
        s1 = float(int32_t(st.cal[6]) - int32_t(st.cal[0]));
    } else if (st.mode == 1) {
        s1 = float(int32_t(st.cal[6]) - int32_t(st.cal[1]));
    } else if (st.mode == 2) {
        s1 = float(int32_t(st.cal[6]) - int32_t(st.cal[2]));
    }
    const int32_t r5 = int32_t(rawX) * int32_t(rawX) + int32_t(rawY) * int32_t(rawY);
    s3 = std::sqrt(float(r5));   // raw length
    if (s3 == zero) {
        s4 = zero;
        s2 = zero;
    } else {
        s2 = float(int32_t(rawX)) / s3;   // unit direction
        s4 = float(int32_t(rawY)) / s3;
        s0 = s3 - st.lastLength;
        if (!(s1 > s3)) {
            // Outside the inner range: track how fast the length is growing.
            if (st.lastLength >= s1) {
                s5 = st.growth;
                if (s0 > s5) {
                    s5 = s0;
                }
                st.growth = s5;
            } else if (st.lastDelta < s0) {
                st.growth = s0;
            } else {
                st.growth = st.lastDelta;
            }
        } else {
            // Inside: the position relative to the inner range.
            if (st.lastLength >= s1) {
                st.growth = s0;
            } else {
                s5 = st.growth;
                if (s5 >= s0) {
                    s5 = s0;
                }
                st.growth = s5;
            }
            s5 = s2 * s3;
            s6 = s4 * s3;
            s2 = s5 / s1;
            s4 = s6 / s1;
        }
    }
    // 0x27e05c: adapt the outer range.
    s6 = (int32_t(bitsf(st.rangeScale)) > int32_t(0x3f800000)) ? st.rangeScale * s1 : s1;
    s5 = st.growth;
    if (s3 < s1) {
        if (bitsf(s5) < 0xbf800000u) {
            s5 = -1.0f;
        }
    } else if (int32_t(bitsf(s5)) <= int32_t(0x3f800000)) {
        s5 = 1.0f;
    }
    st.growth = s5;
    float s7 = 0.0f;
    const float k = fbits(0x3be1fc78);   // 1/145
    if (st.mode == 0) {
        s7 = float(int32_t(st.cal[3]) - int32_t(st.cal[0])) * k;
    } else if (st.mode == 1) {
        s7 = float(int32_t(st.cal[4]) - int32_t(st.cal[1])) * k;
    } else if (st.mode == 2) {
        s7 = float(int32_t(st.cal[5]) - 0x28) * k;
    }
    s5 = st.outer;
    s5 = s5 + st.growth * s7;
    st.outer = s5;
    if (s5 < s1) {
        s5 = s1;
        st.outer = s1;
    } else if (s5 > s6) {
        s5 = s6;
        st.outer = s5;
    }
    float f = s5 / s6;
    float x = f * s2;
    float y = f * s4;
    // Clamp to the unit circle.
    float l2 = x * x;
    l2 = l2 + y * y;
    if (int32_t(bitsf(l2)) > int32_t(0x3f800000)) {
        float l = std::sqrt(l2);
        x = x / l;
        y = y / l;
    }
    st.lastLength = s3;
    st.lastDelta = s0;
    outX = x;
    outY = y;
}

// ---- The player's pad object -----------------------------------------------------------------------

// Pad object fields used by the player (0x334e4c4c).
struct Pad {
    bool stickActive = false;       // +0x0c |move| > 0.1
    bool prevStickActive = false;   // +0x0d
    int32_t rotDir = 0;             // +0x10 consecutive same-direction rotation ticks (signed)
    int32_t rotIdle = 0;            // +0x14 ticks without measurable rotation
    float rotSum = 0;               // +0x18 accumulated rotation (radians)
    float ring[30] = {};            // +0x1c per-tick rotation history
    int32_t ringIndex = 0;          // +0x94
    float rotWindow = 0;            // +0x98 sum of the last 30 entries
    int32_t ringCount = 0;          // +0x9c
    V3 move;                        // +0xa0 movement direction on the ground (snapped stick)
    V3 moveRaw;                     // +0xac the same without snapping
    V3 prevMove;                    // +0xb8
    float stickX = 0;               // +0xc4
    float stickY = 0;               // +0xc8
    bool dashHeld = false;          // +0xd6 (controller wrapper +0x14: pad-library masks 0x1 | 0x2 | 0x10 | 0x20)
    bool raw0d5 = false;            // +0xd5 (wrapper +0x10)
    float blend104 = 0;             // +0x104 along-wall blend (rises while wallTimer runs)
    int32_t wallTimer = 0;          // +0x108 set to 10 by a contact facing the camera (or a PlayerAlongWallArea)

    // Pad vtable getters read by the action conditions.
    float rotationSumDegrees() const {   // +0x18 (0x3e194c)
        return rotSum * fbits(0x42652ee0);
    }
    float rotationSpeedDegrees() const {   // +0x20 (0x3e1960)
        return (rotWindow * fbits(0x3d088889)) * fbits(0x42652ee0);
    }
};

// 0x15fa14 without the StickFixArea branch: the movement basis from the camera matrix (3x4
// row-major, the columns are the camera axes). Returns false when the camera's x axis is (almost)
// vertical.
inline bool cameraBasis(const float cam[12], V3& forward, V3& right) {
    using namespace captain_toad::RecoveredVec;
    const V3 up{0.0f, 1.0f, 0.0f};
    const V3 cameraX{cam[0], cam[4], cam[8]};
    forward = cross(up, cameraX);
    const float lo = fbits(0xba83126f);
    const float hi = fbits(0x3a83126f);
    if (!(forward.x < lo || forward.x > hi) && !(forward.y < lo || forward.y > hi) &&
        !(forward.z < lo || forward.z > hi)) {
        return false;
    }
    normalize(forward);
    right = cross(forward, up);
    normalize(right);
    return true;
}

// 0x2dc0b8: zero tiny vectors (returns true) or normalise (returns false).
inline bool normalizeOrZero(V3& v) {
    float l2 = v.x * v.x;
    l2 = l2 + v.y * v.y;
    l2 = l2 + v.z * v.z;
    if (l2 < fbits(0x358637be)) {
        v = {0, 0, 0};
        return true;
    }
    captain_toad::RecoveredVec::normalize(v);
    return false;
}

// 0x15fbc4: track how much the stick direction rotates, tick by tick.
inline void trackRotation(Pad& p) {
    const float zero = 0.0f;
    auto resetAll = [&]() {
        for (float& r : p.ring) {
            r = zero;
        }
        p.rotDir = 0;
        p.rotIdle = 0;
        p.rotSum = zero;
        p.ringIndex = 0;
        p.ringCount = 0;
        p.rotWindow = zero;
    };
    if (!(p.stickActive && p.prevStickActive)) {
        if (p.ringCount > 0) {
            resetAll();
        }
        return;
    }
    p.ringIndex = p.ringIndex == 29 ? 0 : p.ringIndex + 1;
    float angle = zero;
    V3 previous = p.prevMove;
    V3 current = p.move;
    if (normalizeOrZero(previous) || normalizeOrZero(current)) {
        p.rotIdle++;
    } else {
        float d = current.x * previous.x;
        d = d + current.y * previous.y;
        d = d + current.z * previous.z;
        if (d < -1.0f) {
            d = -1.0f;
        } else if (d > 1.0f) {
            d = 1.0f;
        }
        angle = captain_toad::RecoveredMath::acosf(d);
        // Direction of rotation: the sign of the cross product along up (0,1,0) (data at 0x55017c).
        V3 c = captain_toad::RecoveredVec::cross(current, previous);
        float s = c.x * 0.0f;
        s = s + c.y * 1.0f;
        s = s + c.z * 0.0f;
        float magnitude = s;
        if (s <= zero) {
            magnitude = -magnitude;
        }
        if (int32_t(bitsf(magnitude)) < int32_t(0x3cf5c28f)) {
            p.rotIdle++;
            angle = zero;
        } else {
            if (zero > s) {
                if (p.rotDir <= 0) {
                    angle = -angle;
                    p.rotDir = p.rotDir - 1;
                } else {
                    p.rotDir = 0;
                    p.rotSum = zero;
                }
            } else if (p.rotDir >= 0) {
                p.rotDir = p.rotDir + 1;
            } else {
                p.rotDir = 0;
                p.rotSum = zero;
            }
            p.rotIdle = 0;
        }
    }
    // 0x15fc94: accumulate, and keep a 30-tick window.
    p.rotSum = p.rotSum + angle;
    float window = p.rotWindow;
    if (!(p.ringCount < 30)) {
        window = window - p.ring[p.ringIndex];
        p.rotWindow = window;
    }
    p.rotWindow = window + angle;
    p.ring[p.ringIndex] = angle;
    p.ringCount++;
    if (p.rotDir != 0 && p.rotIdle > 30) {
        resetAll();
    }
}

// ---- Stick snapping ----------------------------------------------------------------------------------

// 0x2d6100: snap the stick angle within each quadrant. Dead bands of 0.314 rad around the axes and
// 0.204 rad around the diagonals lock onto those directions; between them the angle is stretched
// linearly (the bands swap inside a "PlayerInclinedControlArea").
inline void snapStick(float& x, float& y, bool inclinedArea = false) {
    float vx = x;
    float vy = y;
    float l2 = vx * vx;
    l2 = l2 + vy * vy;
    if (l2 < fbits(0x358637be)) {
        return;
    }
    {   // 0x2fd1f0: normalise
        float n2 = vx * vx;
        n2 = n2 + vy * vy;
        float n = std::sqrt(n2);
        if (!(n <= 0.0f)) {
            float k = 1.0f / n;
            vx = vx * k;
            vy = vy * k;
        }
    }
    float c = 1.0f * vx;
    c = c + 0.0f * vy;
    if (c < -1.0f) {
        c = -1.0f;
    } else if (c > 1.0f) {
        c = 1.0f;
    }
    float a = captain_toad::RecoveredMath::acosf(c);   // angle from +x, 0..pi
    const float halfPi = fbits(0x3fc90fdb);
    const float quarterPi = fbits(0x3f490fdb);
    const float band204 = fbits(0x3e511aba);
    const float band314 = fbits(0x3ea0d97c);
    float quadrantF = a / halfPi;
    uint32_t quadrant = quadrantF <= 0.0f ? 0u : uint32_t(quadrantF);   // vcvt.u32.f32 (truncation, saturating at 0)
    float r = a;
    r = r - float(quadrant) * halfPi;   // angle within the quadrant
    float bandAxis = inclinedArea ? band204 : band314;
    float bandDiagonal = inclinedArea ? band314 : band204;
    float s3 = halfPi - bandAxis;
    float s2 = quarterPi + bandDiagonal;
    float span = s3 - s2;
    float out;
    if (s3 < r) {
        out = halfPi;   // locked to the next axis
    } else if (!(s2 >= r)) {
        float t = r - s2;
        t = t * quarterPi;
        out = t / span + quarterPi;
    } else {
        float d = quarterPi - bandDiagonal;
        if (d < r) {
            out = quarterPi;   // locked to the diagonal
        } else if (r <= bandAxis) {
            out = 0.0f;   // locked to the axis
        } else {
            float t = r - bandAxis;
            t = t * quarterPi;
            out = t / span;
        }
    }
    out = out + float(quadrant) * halfPi;
    float m2 = x * x;
    m2 = m2 + y * y;
    const float length = std::sqrt(m2);
    const float originalY = y;
    x = captain_toad::RecoveredMath::cosf(out) * length;
    y = originalY < 0.0f ? -captain_toad::RecoveredMath::sinf(out) * length
                         : captain_toad::RecoveredMath::sinf(out) * length;
}

// 0x2d62f8: the wide snap along x: 0..50 degrees -> 0, 50..80 -> a ramp, 80..100 -> 90,
// 100..130 -> a ramp, 130 and more -> 180.
inline void snapStickWideX(float& x, float& y) {
    float vx = x;
    float vy = y;
    float l2 = vx * vx;
    l2 = l2 + vy * vy;
    if (l2 < fbits(0x358637be)) {
        return;
    }
    {
        float n2 = vx * vx;
        n2 = n2 + vy * vy;
        float n = std::sqrt(n2);
        if (!(n <= 0.0f)) {
            float k = 1.0f / n;
            vx = vx * k;
            vy = vy * k;
        }
    }
    float c = 1.0f * vx;
    c = c + 0.0f * vy;
    if (c < -1.0f) {
        c = -1.0f;
    } else if (c > 1.0f) {
        c = 1.0f;
    }
    float a = captain_toad::RecoveredMath::acosf(c);
    const int32_t angleBits = int32_t(bitsf(a));
    const float pi = fbits(0x40490fdb);
    const float half = 0.5f;
    const float halfPi = fbits(0x3fc90fdb);
    float out;
    if (angleBits <= int32_t(0x3f5f66f3)) {
        out = 0.0f;
    } else if (angleBits < int32_t(0x3fb2b8c3)) {
        float t = a - fbits(0x3f5f66f3);
        t = t * pi;
        t = t * half;
        t = t * fbits(0x3ff47643);
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > halfPi) {
            t = halfPi;
        }
        out = t;
    } else if (angleBits <= int32_t(0x3fdf66f3)) {
        out = halfPi;
    } else if (int32_t(0x4011361e) <= angleBits) {
        out = pi;
    } else {
        float t = a - fbits(0x3fdf66f3);
        t = t * pi;
        t = t * half;
        t = t * fbits(0x3ff47645);
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > halfPi) {
            t = halfPi;
        }
        out = t + halfPi;
    }
    float m2 = x * x;
    m2 = m2 + y * y;
    const float length = std::sqrt(m2);
    const float originalY = y;
    x = captain_toad::RecoveredMath::cosf(out) * length;
    y = originalY < 0.0f ? -captain_toad::RecoveredMath::sinf(out) * length
                         : captain_toad::RecoveredMath::sinf(out) * length;
}

// Area queries of the player (0x2f0de4); none of these areas exist in the opening map.
struct Areas {
    bool stickFix = false;
    bool stickSnapOff = false;
    bool alongWall = false;
    bool widenStickXSnap = false;
    bool inclinedControl = false;
};

// A collider contact the along-wall test reads (iterator 0x2d6698 / 0x2d6628 / 0x2d65cc over the
// collider lists +0x44, +0x48, +0x4c, +0x50, +0x58, +0x54).
struct WallContact {
    bool valid;
    V3 normal;
};

// 0x15ff0c: snap the stick; near camera-facing walls, blend in the wide-x snap.
inline void adjustStick(Pad& p, float& x, float& y, const Areas& areas, bool onGround) {
    if (areas.stickSnapOff) {
        return;
    }
    if (p.blend104 > 0.0f) {
        float sx = x;
        float sy = y;
        if (y > 0.0f) {
            snapStickWideX(sx, sy);
        }
        snapStick(x, y, areas.inclinedControl);
        const float b = p.blend104;
        const float keep = 1.0f - b;
        float ax = x * keep;
        float ay = y * keep;
        float bx = sx * b;
        float by = sy * b;
        x = ax + bx;
        y = ay + by;
        return;
    }
    if (areas.widenStickXSnap && !onGround) {
        snapStickWideX(x, y);
        return;
    }
    snapStick(x, y, areas.inclinedControl);
}

// 0x2d7edc, the movement part, once per game tick.
//   cam:      3x4 row-major camera matrix (the columns are the camera axes)
//   contacts: the six collider lists in iterator order
//   stickX/Y: the controller wrapper's stick pair
inline void update(Pad& p, const float cam[12], const WallContact contacts[6], bool onGround, float stickX,
                   float stickY, bool dash, const Areas& areas = {}) {
    using namespace captain_toad::RecoveredVec;
    // The camera's horizontal forward direction (or its up axis when looking straight down).
    V3 cameraForward{cam[2], cam[6], cam[10]};
    cameraForward = scale(cameraForward, -1.0f);
    cameraForward.y = 0.0f;
    if (normalizeOrZero(cameraForward)) {
        cameraForward = {cam[1], cam[5], cam[9]};
        cameraForward.y = 0.0f;
        normalizeOrZero(cameraForward);
    }

    // Along-wall timer: a contact whose normal points at the camera restarts it.
    if (p.wallTimer != 0) {
        p.wallTimer--;
    }
    for (int i = 0; i < 6; i++) {
        if (!contacts[i].valid) {
            continue;
        }
        const V3& n = contacts[i].normal;
        float d = n.x * cameraForward.x;
        d = d + n.y * cameraForward.y;
        d = d + n.z * cameraForward.z;
        if (!(bitsf(d) < 0xbf7f069eu)) {
            p.wallTimer = 10;
            break;
        }
    }
    if (areas.alongWall) {
        p.wallTimer = 10;
    }
    // The blend rises by 10% of the remainder per tick while the timer runs, and decays otherwise.
    if (p.wallTimer != 0) {
        float v = p.blend104;
        float t = 1.0f - v;
        p.blend104 = v + t * fbits(0x3dcccccd);
    } else {
        float v = p.blend104;
        float t = v;
        v = v - t * fbits(0x3dcccccd);
        p.blend104 = (int32_t(bitsf(v)) < int32_t(0x3ba3d70a)) ? 0.0f : v;
    }

    p.dashHeld = dash;
    p.prevMove = p.move;
    p.move = {0, 0, 0};
    V3 forward, right;
    if (!cameraBasis(cam, forward, right)) {
        return;
    }
    {
        V3 a = scale(forward, stickY);
        V3 b = scale(right, stickX);
        p.moveRaw = add(a, b);
    }
    p.stickX = stickX;
    p.stickY = stickY;
    float sx = stickX;
    float sy = stickY;
    adjustStick(p, sx, sy, areas, onGround);
    {
        V3 a = scale(forward, sy);
        V3 b = scale(right, sx);
        p.move = add(a, b);
    }
    p.prevStickActive = p.stickActive;
    float l2 = p.move.x * p.move.x;
    l2 = l2 + p.move.y * p.move.y;
    l2 = l2 + p.move.z * p.move.z;
    const float length = std::sqrt(l2);
    p.stickActive = int32_t(bitsf(length)) > int32_t(0x3dcccccd);   // |move| > 0.1
    trackRotation(p);
}

}   // namespace captain_toad::RecoveredPad
