// Recovered game camera of the opening (original game, ARM11 main code).
//
// How the camera works in the opening:
//   - The level's camera data (CameraParam) defines "tickets": camera setups that become active
//     under conditions. The stage starts on a Start ticket: a fixed angle and distance looking at
//     a set point. It ends as soon as the player touches the camera controls or walks away more
//     than 50 units.
//   - Then the area ticket of the captain_toad takes over: an orbit camera around a look-at point that
//     follows the player. The player can rotate it in 45-degree steps with L/R (holding them
//     rotates continuously), with the C-stick or D-pad, or by dragging on the touch screen, and
//     ZL (or L+R) swings it behind the protagonist. X cycles the zoom levels.
//   - Whenever the active ticket changes, an interpolator eases eye, target, up and field of view
//     from the old view to the new one.
// The director holds the tickets in priority slots and runs the active camera every 30 Hz tick.
//
// Corrected decompilation of the scene camera path traced in the opening (evidence/runs/camera-*;
// notes docs/camera.md). The original operation order is kept; compile with -ffp-contract=off
// (VMLA/VMLS are chained on the ARM11 and in the JIT).
//
//   StartCamera   vtable 0x4ccf44  Start ticket (ticket class with AngleV/AngleH/Distance/
//                                  LookAtPos). start 0x210160, calc 0x210278.
//   PlayerCamera  vtable 0x4ccbc0  the player's exploration ticket class (CameraArea obj543).
//                                  ctor 0x20bdd8, params 0x20b184, start 0x20b6dc / 0x29cf28,
//                                  calc 0x21ff74 -> 0x20b7f0 (tail 0x209ec8), pose 0x3eb670.
//   Interpolator  0x23f5d8 start, 0x23f7ec update, nerve 0x218c1c (default class pair),
//                 0x3f02a4 copy-out, 0x29ab60 fov.
//   Director      0x24dba4 ticket selection/switch, 0x42aaec per tick; ticket slots 0x234de8
//                 (six priority slots per player; slot 3 is cleared when its camera has ended,
//                 the base end 0x21ff50 sets +0x14).
//
// Platform boundary: CameraPad mirrors the game's controller object (0x3104ede8 in the reference
// run, 0x28716c): triggered mask +0x4, held mask +0x110, touch position +0x114, circle pad +0x11c
// and C-stick +0x124, in the game's own bit layout (observed with research/inputs/pad-bits-probe.tsv):
// A 0x1, B 0x2, ZL 0x4, X 0x8, Y 0x10, ZR 0x20, SELECT 0x800, L 0x2000, R 0x4000, touch 0x8000,
// D-pad up/down/left/right 0x10000/0x20000/0x40000/0x80000, C-stick directions 0x1000000..0x8000000.
//
// Not ported (absent from the opening or host-specific): the motion-sensor branch of 0x29c8c0 (the
// native host has no gyro, so 0x2a4dc4 returns the identity basis, as observed in the reference
// run), the camera-move sound 0x20bb08, the zoom / UI events (their names are kept as strings),
// the stereo parameters, and the special interpolation nerves of other class pairs.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "game/Math.hpp"
#include "game/VecLib.hpp"

namespace captain_toad::RecoveredCamera {

using captain_toad::RecoveredVec::M34;
using captain_toad::RecoveredVec::V3;
namespace RV = RecoveredVec;
namespace RM = RecoveredMath;

constexpr float kDeg2Rad = 0.01745329238474369f;   // 0x3c8efa35
constexpr float kRad2Deg = 57.2957763671875f;      // 0x42652ee0

inline float fbits(uint32_t b) {
    return RM::fbits(b);
}
inline uint32_t bitsf(float f) {
    return RM::bitsf(f);
}
inline int32_t sbits(float f) {
    return int32_t(bitsf(f));
}

// Unit vectors initialised at run time (0x550170, 0x55017c, 0x550188).
inline const V3 kUnitX{1, 0, 0}, kUnitY{0, 1, 0}, kUnitZ{0, 0, 1};

enum PadBit : uint32_t {
    kA = 0x1,
    kB = 0x2,
    kZL = 0x4,
    kX = 0x8,
    kY = 0x10,
    kZR = 0x20,
    kSelect = 0x800,
    kL = 0x2000,
    kR = 0x4000,
    kTouch = 0x8000,
    kDUp = 0x10000,
    kDDown = 0x20000,
    kDLeft = 0x40000,
    kDRight = 0x80000,
    kCStickAny = 0xf000000,
};

struct CameraPad {
    uint32_t trig = 0;   // buttons pressed this frame
    uint32_t held = 0;   // buttons held
    float touch[2] = {0, 0};
    float stick[2] = {0, 0};
    float cstick[2] = {0, 0};
    bool triggered(uint32_t mask) const { return (trig & mask) != 0; }   // 0x292268
    bool isHeld(uint32_t mask) const { return (held & mask) != 0; }      // 0x299b84
};

// Persistent options read through 0x2a26cc (save data): horizontal / vertical camera inversion.
struct CameraSettings {
    bool invertH = false;
    bool invertV = false;
};

// Global camera flags at 0x4f4944 (observed values at the opening checkpoint).
struct CameraGlobals {
    uint8_t g[9] = {1, 1, 0, 0, 1, 0, 0, 0, 0};
    int8_t snapLock = 0;   // 0x4f7fb4 (0x29cd70 / 0x29d79c)
    int touchCount = 0;    // [0x4f6d14 + 4] (0x337ef4)
};

// 0x310004 -> 0x2efcd0: pose matrix from Euler angles in degrees (x, y, z) and a position.
inline M34 eulerMatrix(const V3& rotDeg, const V3& pos) {
    const float k = kDeg2Rad;
    float rx = rotDeg.x * k;
    float ry = rotDeg.y * k;
    float rz = rotDeg.z * k;
    float sx = RM::sinf(rx);
    float sy = RM::sinf(ry);
    float sz = RM::sinf(rz);
    float cx = RM::cosf(rx);
    float cy = RM::cosf(ry);
    float cz = RM::cosf(rz);
    float s1 = cx * sz;
    float s6 = sx * sy;
    float s7 = sx * sz;
    float s9 = cy * sz;
    float s11 = sx * cy;
    float s3 = cx * cz;
    float s10 = cy * cz;
    float s0 = -sy;
    float s8 = cx * cy;
    float s2 = sy * s1;
    s1 = s6 * cz - s1;   // VNMLS
    s7 = s7 + s3 * sy;
    s3 = s3 + s6 * sz;
    s2 = s2 - sx * cz;
    return {{s10, s1, s7, pos.x, s9, s3, s2, pos.y, s0, s11, s8, pos.z}};
}

// What the camera follows: the player actor's pose position (actor +0x30 -> +4) and its base
// matrix (pose vtable +0x44, row-major 3x4) for the snap-behind path.
struct CameraTarget {
    V3 pos;
    M34 baseMtx{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    bool present = true;
    // Areas named ZoomInvalidArea / ForceZoomOutArea: none in the opening.
    bool inArea(const std::string&) const { return false; }
};

struct Pose {
    V3 eye;
    V3 target;
    V3 up{0, 1, 0};
    float fovy = 35.0f;
};

// 0x2b00f4: (1 - t) * a + b * t with t clamped to [0, 1].
inline float lerpClamped(float a, float b, float t) {
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    float s3 = 1.0f - t;
    float r = s3 * a;
    r = r + b * t;
    return r;
}

// 0x2fe398: fmodf through an exact remainder; std::fmod is exact as well.
inline float fmodDeg(float x, float y) {
    return std::fmod(x, y);
}

// 0x2f22e4: C = A * B (3x4), in the original's accumulation order.
inline M34 mul(const M34& A, const M34& B) {
    const float* a = A.m;
    const float* b = B.m;
    M34 C;
    for (int r = 0; r < 3; ++r) {
        const float a0 = a[4 * r];
        const float a1 = a[4 * r + 1];
        const float a2 = a[4 * r + 2];
        float c0 = b[0] * a0;
        float c1 = b[1] * a0;
        float c2 = b[2] * a0;
        float c3 = a[4 * r + 3];
        c3 = c3 + b[3] * a0;
        c0 = c0 + b[4] * a1;
        c1 = c1 + b[5] * a1;
        c2 = c2 + b[6] * a1;
        c3 = c3 + b[7] * a1;
        c0 = c0 + b[8] * a2;
        c1 = c1 + b[9] * a2;
        c2 = c2 + b[10] * a2;
        c3 = c3 + b[11] * a2;
        C.m[4 * r] = c0;
        C.m[4 * r + 1] = c1;
        C.m[4 * r + 2] = c2;
        C.m[4 * r + 3] = c3;
    }
    return C;
}

// The orbit direction from the target to the eye: (quat(UnitY, h) * quat(-UnitX, v)) applied to
// UnitZ (0x209ec8, 0x210544). v tilts the camera up (degrees), h turns it around the target.
inline V3 orbitDir(float vDeg, float hDeg) {
    M34 mv = RM::toMatrix(RM::fromAxisAngle(V3{-kUnitX.x, -kUnitX.y, -kUnitX.z}, vDeg * kDeg2Rad));
    M34 mh = RM::toMatrix(RM::fromAxisAngle(kUnitY, hDeg * kDeg2Rad));
    M34 m = mul(mh, mv);
    return RV::mulPoint(m, kUnitZ);
}

// The angle-difference wrap used throughout: d into (-180, 180] by signed / unsigned bit tests.
inline float wrap180(float d) {
    if (sbits(d) > sbits(180.0f)) {
        return d - 360.0f;
    }
    if (bitsf(d) > bitsf(-180.0f)) {
        return d + 360.0f;
    }
    return d;
}

// The "between" test of 0x29ca84 / 0x29cc30 / 0x20a404: is v inside [lo, hi] (hi < lo allowed).
inline bool between(float v, float lo, float hi) {
    if (!(hi >= lo)) {
        if (v < hi) {
            return false;
        }
        if (lo < v) {
            return false;
        }
        return true;
    }
    if (v < lo) {
        return false;
    }
    if (hi < v) {
        return false;
    }
    return true;
}

// 0x2a4dc4 with no motion device: the identity basis; returns false.
inline bool readMotion(V3& r0, V3& r1, V3& r2) {
    r0 = kUnitX;
    r1 = kUnitY;
    r2 = kUnitZ;
    return false;
}

// 0x2d2574: both vectors non-tiny and the angle between them within `angle` radians.
inline bool withinAngle(const V3& a, const V3& b, float angle) {
    const float tiny = fbits(0x358637be);
    float la = a.x * a.x;
    la = la + a.y * a.y;
    la = la + a.z * a.z;
    if (la < tiny) {
        return false;
    }
    float lb = b.x * b.x;
    lb = lb + b.y * b.y;
    lb = lb + b.z * b.z;
    if (lb < tiny) {
        return false;
    }
    V3 na = a;
    V3 nb = b;
    RV::normalize(na);
    RV::normalize(nb);
    float d = na.x * nb.x;
    d = d + na.y * nb.y;
    d = d + na.z * nb.z;
    return d >= RM::cosf(angle);
}

// 0x29d0f8: direction -> (horizontal, vertical) angles in degrees.
inline void dirToAngles(float& h, float& v, const V3& d) {
    V3 horizontal{d.x, 0.0f, d.z};
    float l2 = horizontal.x * horizontal.x;
    l2 = l2 + 0.0f * 0.0f;
    l2 = l2 + horizontal.z * horizontal.z;
    if (l2 < fbits(0x358637be)) {
        h = 0.0f;
        v = (d.y >= 0.0f) ? -90.0f : 90.0f;
        return;
    }
    RV::normalize(horizontal);
    float c = horizontal.x * kUnitZ.x;
    c = c + horizontal.y * kUnitZ.y;
    c = c + horizontal.z * kUnitZ.z;
    if (c < -1.0f) {
        c = -1.0f;
    } else if (c > 1.0f) {
        c = 1.0f;
    }
    h = RM::acosf(c) * kRad2Deg;
    if (horizontal.x < 0.0f) {
        h = -h;
    }
    float y = d.y;
    if (y < -1.0f) {
        y = -1.0f;
    } else if (y > 1.0f) {
        y = 1.0f;
    }
    v = std::asin(y) * kRad2Deg;   // 0x2db050 asinf (library routine; std::asin used)
}

// 0x133b38: C-stick response curve (k = (1 - l2) + l2 * 50), then a unit clamp (0x2fd1f0).
inline void cstickCurve(float out[2], const float in[2]) {
    float l2 = in[0] * in[0];
    l2 = l2 + in[1] * in[1];
    float k = 1.0f - l2;
    k = k + l2 * 50.0f;
    out[0] = in[0] * k;
    out[1] = in[1] * k;
    float m = out[0] * out[0];
    m = m + out[1] * out[1];
    if (sbits(m) > sbits(1.0f)) {
        float length = std::sqrt(m);
        if (length > 0.0f) {
            float inverse = 1.0f / length;
            out[0] *= inverse;
            out[1] *= inverse;
        }
    }
}

// 0x2a4804: camera input source. Returns 0 (C-stick), 2 (D-pad) or 3 (none). A source that was
// active on the previous tick keeps priority while it is held.
inline int cameraInput(float out[2], int previous, const CameraPad& pad, bool useDpad) {
    float dpad[2] = {0, 0};   // 0x23d374
    if (pad.isHeld(kDUp)) {
        dpad[1] = 1.0f;
    }
    if (pad.isHeld(kDDown)) {
        dpad[1] = -1.0f;
    }
    if (pad.isHeld(kDLeft)) {
        dpad[0] = -1.0f;
    }
    if (pad.isHeld(kDRight)) {
        dpad[0] = 1.0f;
    }
    auto useStick = [&]() {
        cstickCurve(out, pad.cstick);
        return 0;
    };
    auto useDirectionPad = [&]() {
        out[0] = dpad[0];
        out[1] = dpad[1];
        return 2;
    };
    const bool dpadTriggered =
        pad.triggered(kDUp) || pad.triggered(kDDown) || pad.triggered(kDRight) || pad.triggered(kDLeft);
    const bool dpadHeld = pad.isHeld(kDUp) || pad.isHeld(kDDown) || pad.isHeld(kDRight) || pad.isHeld(kDLeft);
    if (pad.triggered(kCStickAny)) {
        return useStick();
    }
    if (useDpad && dpadTriggered) {
        return useDirectionPad();
    }
    if (pad.isHeld(kCStickAny) && previous == 0) {
        return useStick();
    }
    if (useDpad && previous == 2 && dpadHeld) {
        return useDirectionPad();
    }
    if (pad.isHeld(kCStickAny)) {
        return useStick();
    }
    if (useDpad && dpadHeld) {
        return useDirectionPad();
    }
    return 3;
}

struct Context {
    CameraPad pad;
    CameraSettings settings;
    CameraGlobals* globals = nullptr;
    const CameraTarget* target = nullptr;
    std::vector<std::string>* events = nullptr;   // zoom / UI notifications (0x2fa878, 0x2de43c)
    void event(const char* name) const {
        if (events) {
            events->push_back(name);
        }
    }
};

// ---- Cameras -----------------------------------------------------------------------------------

// Base camera fields (0x2b3530): +0x14 ended, +0x18 eye, +0x24 target, +0x30 up, +0x3c fovy,
// +0x88 near, +0x8c InterpoleStep, +0x90 flag.
struct CameraBase {
    bool ended = true;
    V3 eye{0, 0, 0};
    V3 target{0, 0, 500};
    V3 up{0, 1, 0};
    float fovy = 35.0f;
    float nearClip = 100.0f;
    int interpoleStep = 60;
    bool flag90 = true;

    Pose pose() const { return {eye, target, up, fovy}; }   // 0x3eb670 / 0x3ecc34
    void end() { ended = true; }                            // 0x21ff50
};

// The Start ticket camera (vtable 0x4ccf44): a fixed orbit around a set point that ends as soon as
// the player uses the camera controls or walks away.
struct StartCamera : CameraBase {
    float angleV = 0;                     // +0xdc
    float angleH = 0;                     // +0xe0
    float distance = 0;                   // +0xe4
    V3 lookAtOffset;                      // +0xe8
    V3 lookAtPos;                         // +0xf4
    bool isSetLookAt = false;             // +0x100
    bool enabled = true;                  // +0x101
    V3 startTargetPos;                    // +0xd0
    float motionQuat[4] = {0, 0, 0, 1};   // +0x104..+0x110 (motion reference)
    int motionDelay = 0;                  // +0x114
    int count = 0;                        // +0xcc

    void start(const Context&) {   // 0x210160 (identity motion basis)
        ended = false;
        motionQuat[0] = -0.0f;
        motionQuat[1] = -0.0f;
        motionQuat[2] = -0.0f;
        motionQuat[3] = 1.0f;
        motionDelay = 60;
    }

    void calc(const Context& c) {   // 0x210278
        if (flag90) {
            startTargetPos = c.target->pos;
        }
        V3 t = c.target->pos;
        if (enabled) {
            // After 15 ticks, any camera input ends the ticket.
            if (count > 15) {
                float l2 = c.pad.cstick[0] * c.pad.cstick[0];
                l2 = l2 + c.pad.cstick[1] * c.pad.cstick[1];
                if (sbits(std::sqrt(l2)) > sbits(0.3f) || c.pad.triggered(kL) || c.pad.triggered(kR) ||
                    c.pad.triggered(kDUp) || c.pad.triggered(kDDown) || c.pad.triggered(kDLeft) ||
                    c.pad.triggered(kDRight) || c.pad.triggered(kTouch)) {
                    end();
                    return;
                }
            }
            if (motionDelay > 0) {
                --motionDelay;
            }
            // (else: the motion-sensor tilt test (> 12 degrees) never fires with the identity basis.)
            // Walking more than 50 units away ends the ticket.
            V3 d = RV::sub(startTargetPos, t);
            float l2 = d.x * d.x;
            l2 = l2 + d.y * d.y;
            l2 = l2 + d.z * d.z;
            if (sbits(std::sqrt(l2)) > sbits(50.0f)) {
                end();
                return;
            }
        }
        // 0x210544: the pose.
        target = isSetLookAt ? lookAtPos : t;
        float s16 = angleH * kDeg2Rad;
        float s18 = RM::cosf(s16) * lookAtOffset.x;
        s18 = s18 + lookAtOffset.z * RM::sinf(s16);
        target.x = s18 + target.x;
        target.y = target.y + lookAtOffset.y;
        s16 = angleH * kDeg2Rad;
        s18 = RM::cosf(s16) * lookAtOffset.z;
        s18 = s18 + lookAtOffset.x * RM::sinf(s16);
        target.z = s18 + target.z;
        V3 dir = orbitDir(angleV, angleH);
        eye = RV::add(RV::scale(dir, distance), target);
        up = kUnitY;
        ++count;
    }
};

// The player's exploration camera (vtable 0x4ccbc0). Field comments give the original offsets.
struct PlayerCamera : CameraBase {
    float touchCur[2] = {0, 0};    // +0xcc
    float touchPrev[2] = {0, 0};   // +0xd4
    float touchVel[2] = {0, 0};    // +0xdc
    float angleStartV = 30;        // +0xe4
    float angleStartH = 45;        // +0xe8
    float outV = 30;               // +0xec final vertical angle
    float outH = 45;               // +0xf0 final horizontal angle
    float vMin = 0;                // +0xf4 (AngleV)
    float vMax = 89;               // +0xf8
    float vBase = 30;              // +0xfc
    float hTarget = 45;            // +0x100
    float vCur = 30;               // +0x104
    float hCur = 45;               // +0x108
    float gyroV = 0;               // +0x10c
    float gyroH = 0;               // +0x110
    float gyroVs = 0;              // +0x114
    float gyroHs = 0;              // +0x118
    V3 motion0 = kUnitX;           // +0x11c
    V3 motion1 = kUnitY;           // +0x128
    V3 motion2 = kUnitZ;           // +0x134
    float motionV = 0;             // +0x140
    float motionH = 0;             // +0x144
    int delay = 0;                 // +0x148
    float prevV = 0;               // +0x14c
    float prevH = 0;               // +0x150
    float distance = 5500;         // +0x154
    bool distanceDetail = false;   // +0x158
    float distX = 5500;            // +0x15c..
    float distY = 5500;
    float distZ = 5500;
    bool hasTarget = false;          // +0x168 != 0
    uint8_t follow[3] = {0, 0, 0};   // +0x16c IsFollowLookAtPlayerX/Y/Z
    bool resetForce = false;         // +0x16f
    bool flag170 = false;
    V3 followOffset;              // +0x174
    bool zoomFlag = true;         // +0x180
    bool upsideDown = false;      // +0x181
    bool useMin[3] = {0, 0, 0};   // +0x182..+0x184
    bool useMax[3] = {0, 0, 0};   // +0x185..+0x187
    V3 limitMin;                  // +0x188
    V3 limitMax;                  // +0x194
    int zoomState = 0;            // +0x1a0 (0 far, 1 near, 2 medium, 3 closest, 4 follow)
    int zoomSaved = 0;            // +0x1a4
    float zoomFovy = 10;          // +0x1a8
    float zoomFovyMedium = 18;    // +0x1ac
    float zoomFovyFar = 30;       // +0x210
    float fovSaved = 30;          // +0x1b0
    V3 zoomOffset;                // +0x1b4
    V3 targetSaved;               // +0x1c0
    int startCount = 20;          // +0x1cc
    int startTotal = 20;          // +0x1d0
    V3 startTarget;               // +0x1d4
    float startFov = 35;          // +0x1e0
    int inputState = 3;           // +0x1e8
    int zoomRequest = 0;          // +0x1ec
    uint8_t f1f0 = 0;             // +0x1f0..+0x1f5
    uint8_t f1f1 = 0;
    uint8_t f1f2 = 0;
    uint8_t f1f3 = 0;
    uint8_t f1f4 = 0;
    uint8_t forceZoomOut = 0;
    const V3* zoomFollowPos = nullptr;        // +0x1f8 (state 4)
    uint8_t zoomFollowFlags[3] = {0, 0, 0};   // +0x1fc
    V3 zoomFollowOffset;                      // +0x200
    float zoomFollowRate = 0;                 // +0x20c
    float snapH = 30;                         // +0x214
    float snapCount = 0;                      // +0x218
    bool snapActive = false;                  // +0x21c
    V3 snapFront;                             // +0x220
    V3 snapPos;                               // +0x22c
    int initZoomMode = 2;                     // +0x238
    float hMin = -180;                        // +0x23c
    float hMax = 180;                         // +0x240
    int hMode = 0;                            // +0x244 (0 free, 1 limited range, 2 fixed)

    // The target's base matrix and position, refreshed by calc().
    const float* target_mtx = nullptr;
    V3 target_pos;

    PlayerCamera() { fovy = 30.0f; }   // 0x20bdd8 (+0x3c = 30)

    // The 0x20b184 subset used by the opening ticket (obj543): the caller passes the ticket's values.
    struct Ticket {
        bool hasLookAt = false;
        V3 lookAtPos;
        int initZoomMode = -1;
        float distance = 5500;
        float distX = 5500;
        float distY = 5500;
        float distZ = 5500;
        bool distanceDetail = false;
        bool hasAngleV = false;
        float vMin = 0;
        float vMax = 89;
        bool hasAngleH = false;
        float hMin = -180;
        float hMax = 180;
        int interpoleStep = 60;
        float zoomFovy = 10;
        float zoomFovyMedium = 18;
        float zoomFovyFar = 30;
        bool hasAngleStart = false;
        float startV = 30;
        float startH = 45;
        bool resetForce = false;
        uint8_t follow[3] = {0, 0, 0};
        V3 followOffset;
        bool upsideDown = false;
        bool useMin[3] = {0, 0, 0};
        bool useMax[3] = {0, 0, 0};
        V3 limitMin;
        V3 limitMax;
        bool hasForceZoomOut = false;
        uint8_t forceZoomOut = 0;
        float nearClip = 100;
    };

    void applyTicket(const Ticket& t, CameraGlobals& g) {
        if (t.hasLookAt) {
            target = t.lookAtPos;   // (multiplied by the identity +0x94 matrix)
        }
        if (t.initZoomMode >= 0) {
            initZoomMode = t.initZoomMode;
        }
        distance = t.distance;
        distanceDetail = t.distanceDetail;
        distX = t.distX;
        distY = t.distY;
        distZ = t.distZ;
        if (t.hasAngleV) {
            vMin = t.vMin;
            vMax = t.vMax;
        }
        if (t.hasAngleH) {
            hMin = t.hMin;
            hMax = t.hMax;
        }
        interpoleStep = t.interpoleStep;
        zoomFovy = t.zoomFovy;
        zoomFovyMedium = t.zoomFovyMedium;
        zoomFovyFar = t.zoomFovyFar;
        if (t.hasAngleStart) {
            angleStartV = t.startV;
            angleStartH = t.startH;
        }
        resetForce = t.resetForce;
        for (int i = 0; i < 3; ++i) {
            follow[i] = t.follow[i];
            useMin[i] = t.useMin[i];
            useMax[i] = t.useMax[i];
        }
        followOffset = t.followOffset;
        upsideDown = t.upsideDown;
        if (zoomState != 2) {
            zoomState = 2;
        }
        zoomFlag = true;
        limitMin = t.limitMin;
        limitMax = t.limitMax;
        fovSaved = fovy;
        targetSaved = target;
        zoomOffset = {0, 100, 0};   // 0x20b44c: (0, 100, 0), the zoomed target's lift
        g.g[8] = 0;
        if (t.hasForceZoomOut) {
            initZoomMode = 0;
            forceZoomOut = t.forceZoomOut;
        }
        nearClip = t.nearClip;
    }

    // 0x29d028(h = s0, v = s1): reset the orbit angles relative to the motion basis.
    void resetAngles(float h, float v) {
        V3 r0, r1, r2;
        readMotion(r0, r1, r2);
        float s0 = r1.x * r1.x;
        s0 = s0 + r1.z * r1.z;
        float s1 = std::sqrt(s0);
        if (r2.y <= 0.0f) {
            s1 = -s1;
        }
        float s16 = RM::atan2f(r1.y, s1) * kRad2Deg;
        float s0b = RM::atan2f(-r0.z, r0.x) * kRad2Deg;
        if (upsideDown) {
            s16 = -s16;
            s0b = -s0b;
        }
        float v2 = v - s16;
        gyroVs = s16;
        motionV = s16;
        gyroV = s16;
        gyroHs = s0b;
        float h2 = h - s0b;
        motionH = s0b;
        gyroH = s0b;
        vBase = v2;
        hTarget = h2;
        vCur = v2;
        hCur = h2;
    }

    // 0x29cf28: zoom / start interpolation priming at activation.
    void primeZoom() {
        zoomState = initZoomMode;
        startCount = 20;
        startTotal = 20;
        startTarget = target;
        startFov = fovy;
        startInterp(startTarget, targetSaved, startCount, startTotal);
        float s = 1.0f - float(startCount) / float(startTotal);
        float p = std::pow(s, 1.6f);   // 0x300b48 powf (library routine; std::pow used)
        fovy = lerpClamped(startFov, zoomFovyFar, 1.0f - p);
        if (startCount == startTotal) {
            fovy = zoomFovyFar;
        }
        zoomRequest = forceZoomOut ? 2 : 0;
        zoomOffset = {0, 100, 0};
    }

    // 0x20a5a8: the hand-over from the previous player camera when the director switches between
    // two cameras of this class (transfer table 0x48a850: "キノピオ探検隊" -> "キノピオ探検隊"), after
    // the new camera's start. The new camera keeps the old one's orbit, zoom state and offset,
    // field of view (its own becomes the start value) and, while zoomed, its target.
    void takeOver(const PlayerCamera& o) {
        if (!flag170 && resetForce) {
            return;
        }
        outV = o.outV;
        outH = o.outH;
        vBase = o.vBase;
        hTarget = o.hTarget;
        vCur = o.vCur;
        hCur = o.hCur;
        gyroV = o.gyroV;
        gyroH = o.gyroH;
        gyroVs = o.gyroVs;
        gyroHs = o.gyroHs;
        zoomState = o.zoomState;
        zoomOffset = o.zoomOffset;
        startCount = 0;
        if (o.forceZoomOut == 1) {
            targetSaved = o.target;
            fovSaved = o.fovy;
            target = o.target;
            startFov = o.fovy;
            startTarget = o.target;
            return;
        }
        startFov = fovy;
        fovy = o.fovy;
        const bool zoomed = zoomState >= 1;
        if (zoomed || (follow[0] && o.follow[0])) {
            target.x = o.target.x;
        }
        if (zoomed || (follow[1] && o.follow[1])) {
            target.y = o.target.y;
        }
        if (zoomed || (follow[2] && o.follow[2])) {
            target.z = o.target.z;
        }
        if (zoomState == 0) {
            if (!follow[0]) {
                target.x = targetSaved.x;
            }
            if (!follow[1]) {
                target.y = targetSaved.y;
            }
            if (!follow[2]) {
                target.z = targetSaved.z;
            }
        }
    }

    // 0x20b6dc: start (vtable +8).
    void start(const Context& c) {
        snapCount = 0;
        ended = false;
        snapActive = false;
        if (c.globals->snapLock != 0) {
            resetAngles(angleStartH, angleStartV);
            snapCount = 30.0f;
            return;
        }
        if (!flag90) {
            if (!resetForce && !flag170) {
                return;
            }
            if (flag170) {
                return;
            }
        }
        resetAngles(angleStartH, angleStartV);
        hasTarget = c.target && c.target->present;
        if (hasTarget) {
            V3 p = RV::add(c.target->pos, followOffset);
            if (follow[0]) {
                target.x = p.x;
            }
            if (follow[1]) {
                target.y = p.y;
            }
            if (follow[2]) {
                target.z = p.z;
            }
        }
        primeZoom();
    }

    // 0x29d258: the start interpolation of the target (eased by pow 1.6).
    void startInterp(const V3& from, const V3& to, int count, int total) {
        float t = float(count) / float(total);
        float e = 1.0f - std::pow(1.0f - t, 1.6f);
        if (!follow[0]) {
            target.x = lerpClamped(from.x, to.x, e);
        }
        if (!follow[1]) {
            target.y = lerpClamped(from.y, to.y, e);
        }
        if (!follow[2]) {
            target.z = lerpClamped(from.z, to.z, e);
        }
        if (count == total) {
            if (!follow[0]) {
                target.x = to.x;
            }
            if (!follow[1]) {
                target.y = to.y;
            }
            if (!follow[2]) {
                target.z = to.z;
            }
        }
    }

    // 0x29d34c: move the target towards pos + offset, within the move limits.
    void followLimit(float rate, const V3& pos, const V3& offset, const uint8_t flags[3]) {
        V3 p = RV::add(pos, offset);
        if (useMin[0] && p.x < limitMin.x) {
            p.x = limitMin.x;
        }
        if (useMax[0] && p.x > limitMax.x) {
            p.x = limitMax.x;
        }
        if (useMin[1] && p.y < limitMin.y) {
            p.y = limitMin.y;
        }
        if (useMax[1] && p.y > limitMax.y) {
            p.y = limitMax.y;
        }
        if (useMin[2] && p.z < limitMin.z) {
            p.z = limitMin.z;
        }
        if (useMax[2] && p.z > limitMax.z) {
            p.z = limitMax.z;
        }
        float k = 1.0f - rate;
        if (flags[0]) {
            float v = target.x * k;
            v = v + rate * p.x;
            target.x = v;
        }
        if (flags[1]) {
            float v = target.y * k;
            v = v + rate * p.y;
            target.y = v;
        }
        if (flags[2]) {
            float v = target.z * k;
            v = v + rate * p.z;
            target.z = v;
        }
    }

    // 0x29d478: back to zoom state 0 with a 20-tick interpolation.
    void zoomReset(const Context& c) {
        if (zoomState == 0) {
            return;
        }
        zoomState = 0;
        startCount = 0;
        startTotal = 20;
        startTarget = target;
        startFov = fovy;
        c.event("ZoomOut");
    }

    // 0x29c8c0: motion-sensor orbit offsets (with the identity basis: smoothing only).
    void orbitMotion() {
        V3 m0, m1, m2;
        bool valid = readMotion(m0, m1, m2);
        if (!valid) {
            m0 = motion0;
            m1 = motion1;
            m2 = motion2;
        }
        const float five = fbits(0x3db2b8c2);   // 5 degrees in radians
        if (withinAngle(m0, motion0, five) && withinAngle(m1, motion1, five) && withinAngle(m2, motion2, five)) {
            m0 = motion0;
            m1 = motion1;
            m2 = motion2;
        } else {
            motion0 = m0;
            motion1 = m1;
            motion2 = m2;
            snapActive = false;
        }
        float s0 = m1.x * m1.x;
        s0 = s0 + m1.z * m1.z;
        float s1 = std::sqrt(s0);
        if (m2.y <= 0.0f) {
            s1 = -s1;
        }
        float s18 = RM::atan2f(m1.y, s1) * kRad2Deg;
        float s3 = RM::atan2f(-m0.z, m0.x) * kRad2Deg;
        if (upsideDown) {
            s18 = -s18;
            s3 = -s3;
        }
        // valid == true: the gyro integration branch 0x29ca58..0x29ccc4 is not ported (no device).
        motionV = s18;
        motionH = s3;
        gyroVs = lerpClamped(gyroVs, gyroV, 0.2f);
        float a = fmodDeg(gyroHs + 360.0f, 360.0f);
        float b = fmodDeg(gyroH + 360.0f, 360.0f);
        float d = b - a;
        if (sbits(d) > sbits(180.0f)) {
            a = a + 360.0f;
        }
        if (bitsf(d) > bitsf(-180.0f)) {
            b = b + 360.0f;
        }
        float r = lerpClamped(a, b, 0.2f);
        f1f2 = valid;
        gyroHs = fmodDeg(r + 360.0f, 360.0f);
    }

    // 0x20a0b4: buttons (L/R in 45-degree steps; holding them adds 3.5 degrees per tick) and the
    // C-stick / D-pad rotation (3 degrees per tick at full deflection).
    void manualRotate(const Context& c) {
        bool moved = false;
        f1f1 = 0;
        const bool heldL = c.pad.isHeld(kL);
        const bool heldR = c.pad.isHeld(kR);
        const bool trigL = c.pad.triggered(kL);
        const bool trigR = c.pad.triggered(kR);
        const bool anyLR = heldL || heldR;
        float s16 = 0.0f;
        if (snapCount != 0.0f) {
            moved = true;
        }
        if (snapCount == 0.0f && hMode == 0) {
            if ((trigL && !trigR) || (trigR && !trigL)) {
                // One 45-degree step.
                float s17 = 0.0f;
                if (trigL) {
                    s17 = -45.0f;
                }
                if (trigR) {
                    s17 = 45.0f;
                }
                if (upsideDown) {
                    s17 = -s17;
                }
                f1f1 = 1;
                touchVel[0] = 0;
                touchVel[1] = 0;
                float a = s17;
                if (a <= 0.0f) {
                    a = -a;
                }
                if (a > 0.0f) {
                    if (c.settings.invertH) {
                        s17 = -s17;
                    }
                    float s0 = outH - gyroH;
                    hTarget = s0 + s17;
                    moved = true;
                }
                snapActive = false;
                if (f1f3 == 0) {
                    f1f3 = 1;
                }
            } else if (!moved) {
                snapActive = false;
            }
        }
        // Holding L/R after the step has completed: continuous rotation.
        if (f1f4) {
            if (heldL) {
                s16 = -3.5f;
            }
            if (heldR) {
                s16 = s16 + 3.5f;
            }
            if (!anyLR) {
                f1f3 = 0;
                f1f4 = 0;
            }
        } else if (f1f3 == 1 && hTarget == hCur) {
            if (!moved) {
                f1f3 = 0;
            } else {
                if (heldL || trigL) {
                    s16 = -3.5f;
                }
                f1f4 = 1;
                if (heldR || trigR) {
                    s16 = s16 + 3.5f;
                }
            }
        }
        float a = s16;
        if (a <= 0.0f) {
            a = -a;
        }
        if (a > 0.0f) {
            if (upsideDown) {
                s16 = -s16;
            }
            if (c.settings.invertH) {
                s16 = -s16;
            }
            hTarget = hTarget + s16;
            return;
        }
        if (moved) {
            return;
        }
        // C-stick / D-pad: rotate along the stick's dominant axis.
        float vec[2] = {0, 0};
        const bool useDpad = c.globals->g[0] && (inputState == 3 || inputState == 1 || inputState == 2);
        inputState = cameraInput(vec, inputState, c.pad, useDpad);
        if (c.settings.invertV) {
            vec[1] = -vec[1];
        }
        if (c.settings.invertH) {
            vec[0] = -vec[0];
        }
        if (upsideDown) {
            vec[0] = vec[0] * -1.0f;
            vec[1] = vec[1] * -1.0f;
        }
        float ax = vec[0];
        if (ax <= 0.0f) {
            ax = -ax;
        }
        float ay = vec[1];
        if (ay <= 0.0f) {
            ay = -ay;
        }
        if (!(sbits(ax) > sbits(0.05f)) && !(sbits(ay) > sbits(0.05f))) {
            return;
        }
        if (ax < ay) {
            float s3 = vec[1] * -3.0f;
            float v = gyroV + vBase;
            v = v + s3;
            if (between(v, vMin, vMax)) {
                vBase = vBase + s3;
            } else if (vMin > v) {
                vBase = vMin - gyroV;
            } else if (vMax < v) {
                vBase = vMax - gyroV;
            }
        } else {
            float h = hTarget;
            h = h + vec[0] * -3.0f;
            hTarget = h;
        }
        snapActive = false;
        f1f1 = 1;
        touchVel[0] = 0;
        touchVel[1] = 0;
    }

    // 0x20afdc: D-pad rotation when global flag 5 is set (not set in the opening).
    void dpadRotate(const Context& c) {
        if (!c.globals->g[5]) {
            return;
        }
        if (c.pad.isHeld(kDRight) || c.pad.isHeld(kDLeft)) {
            hTarget = hTarget + (c.pad.isHeld(kDRight) ? -3.0f : 3.0f);
        }
        if (!(c.pad.isHeld(kDUp) || c.pad.isHeld(kDDown))) {
            return;
        }
        float s17 = gyroV + vBase;
        float s16 = c.pad.isHeld(kDUp) ? -3.0f : 3.0f;
        float v = s17 + s16;
        if (between(v, vMin, vMax)) {
            vBase = vBase + s16;
            return;
        }
        if (s16 > 0.0f && vMin > s17) {
            vCur = vMin - gyroV;
            vBase = vCur + s16;
            return;
        }
        if (s16 < 0.0f && vMax < s17) {
            vCur = vMax - gyroV;
            vBase = vCur + s16;
        }
    }

    // 0x20ad58: touch-screen drag (scale 0.35, clamped to 10 / 9 degrees per tick, 0.8 inertia).
    void touchRotate(const Context& c) {
        if (!c.globals->g[4]) {
            return;
        }
        if (hostDrag) {   // the host turns the camera itself (smoothDrag); no tick-wise rotation
            touchVel[0] = 0;
            touchVel[1] = 0;
            return;
        }
        if (c.globals->touchCount > 0) {
            touchVel[0] = 0;
            touchVel[1] = 0;
            if (c.pad.triggered(kTouch) || c.pad.isHeld(kTouch)) {
                touchCur[0] = c.pad.touch[0];
                touchCur[1] = c.pad.touch[1];
            }
            return;
        }
        if (c.pad.triggered(kTouch)) {
            touchCur[0] = c.pad.touch[0];
            touchCur[1] = c.pad.touch[1];
        }
        if (!c.pad.isHeld(kTouch)) {
            // Released: keep turning with decaying inertia.
            float l2 = touchVel[0] * touchVel[0];
            l2 = l2 + touchVel[1] * touchVel[1];
            if (!(sbits(std::sqrt(l2)) > sbits(1.0f))) {
                touchVel[0] = 0;
                touchVel[1] = 0;
                return;
            }
            float vx = touchVel[0] * 0.8f;
            float vy = touchVel[1] * 0.8f;
            touchVel[0] = vx;
            touchVel[1] = vy;
            if (upsideDown) {
                vBase = vBase - vy;
                hTarget = hTarget - vx;
            } else {
                hTarget = hTarget + vx;
                vBase = vBase + vy;
            }
            return;
        }
        touchPrev[0] = touchCur[0];
        touchPrev[1] = touchCur[1];
        touchCur[0] = c.pad.touch[0];
        touchCur[1] = c.pad.touch[1];
        float dx = touchCur[0] - touchPrev[0];
        float dy = touchCur[1] - touchPrev[1];
        float l2 = dx * dx;
        l2 = l2 + dy * dy;
        float s0, s1;
        if (sbits(std::sqrt(l2)) < sbits(1.5f)) {
            s0 = 0;
            s1 = 0;
        } else {
            s0 = dx * -0.35f;
            s1 = dy * 0.35f;
        }
        if (sbits(s0) > sbits(10.0f)) {
            s0 = 10.0f;
        } else if (bitsf(s0) >= bitsf(-10.0f)) {
            s0 = -10.0f;
        }
        if (sbits(s1) > sbits(9.0f)) {
            s1 = 9.0f;
        } else if (bitsf(s1) >= bitsf(-9.0f)) {
            s1 = -9.0f;
        }
        if (upsideDown) {
            vBase = vBase - s1;
            hTarget = hTarget - s0;
        } else {
            vBase = vBase + s1;
            hTarget = hTarget + s0;
        }
        touchVel[0] = s0;
        touchVel[1] = s1;
    }

    // 0x2096f8: the zoom state machine (X cycles far -> medium -> near), target follow and field
    // of view.
    void zoomUpdate(const Context& c) {
        CameraGlobals& g = *c.globals;
        const CameraTarget* tgt = c.target;
        const int targets = 0;   // 0x3e1710: number of extra players beyond one (single player)
        bool skipToTail = false;
        if (g.g[6]) {
            zoomSaved = zoomState;
            g.g[6] = 0;
        } else if (g.g[7]) {
            if (zoomSaved == 2) {
                if (zoomState != 2) {
                    zoomState = 2;
                }
            } else if (zoomSaved == 1 && zoomState != 1) {
                if (targets > 0) {
                    c.event("ZoomInvalid");
                } else if (zoomState == 0) {
                    c.event("ZoomIn");
                }
                zoomState = 1;
                f1f0 = 0;
            }
            g.g[7] = 0;
            skipToTail = true;
        }
        if (!skipToTail) {
            bool tailB48 = false;
            bool tailB5c = false;
            if (g.g[1]) {
                bool open = tgt && tgt->present && !tgt->inArea("ZoomInvalidArea") && zoomRequest != 2;
                bool locked = !open;
                bool toggle = c.pad.triggered(kX) || zoomRequest == 1;
                switch (zoomState) {
                case 0:
                    if (!toggle) {
                        tailB5c = true;
                        break;
                    }
                    if (startCount != startTotal || locked) {
                        tailB5c = true;
                        break;
                    }
                    if (targets > 0) {
                        c.event("ZoomInvalid");
                        return;
                    }
                    c.event("ZoomIn");
                    zoomFlag = true;
                    if (zoomState != 2) {
                        zoomState = 2;
                    }
                    if (startCount == startTotal) {
                        targetSaved = target;
                        fovSaved = fovy;
                    }
                    zoomOffset = {0, 100, 0};
                    break;
                case 1:
                    if (locked) {
                        zoomReset(c);
                        break;
                    }
                    if (g.g[2]) {
                        if (c.pad.triggered(kX) || zoomRequest == 1) {
                            zoomState = 3;
                            tailB48 = true;
                            break;
                        }
                    } else if (c.pad.triggered(kX)) {
                        zoomFlag = false;
                        zoomReset(c);
                    }
                    break;
                case 3:
                    if (locked || c.pad.triggered(kX)) {
                        zoomReset(c);
                        break;
                    }
                    if (c.pad.triggered(kDDown)) {
                        zoomState = 1;
                        tailB48 = true;
                    }
                    break;
                case 2:
                    if (locked) {
                        zoomReset(c);
                        break;
                    }
                    if (c.pad.triggered(kX)) {
                        g.g[8] = 0;
                        if (!zoomFlag) {
                            zoomReset(c);
                            break;
                        }
                        c.event("ZoomIn2");
                        if (zoomState == 1) {
                            tailB48 = true;
                            break;
                        }
                        if (targets > 0) {
                            c.event("ZoomInvalid");
                            break;
                        }
                        if (zoomState == 0) {
                            c.event("ZoomIn");
                        }
                        zoomState = 1;
                        f1f0 = 0;
                        tailB48 = true;
                    }
                    break;
                default:
                    break;
                }
            }
            if (!tailB5c) {
                if (tailB48 || zoomState > 0) {
                    if (targets > 0) {
                        zoomReset(c);
                    }
                }
            }
            if (tgt && tgt->present && !tgt->inArea("ForceZoomOutArea")) {
                g.g[3] = 0;
            } else if (!g.g[3]) {
                g.g[3] = 1;
                zoomReset(c);
            }
        }
        // 0x209b94: follow the target and approach the zoom level's field of view.
        if (zoomState > 0) {
            float d = zoomFovy - fovy;
            if (d <= 0.0f) {
                d = -d;
            }
            f1f0 = d < 3.0f;
            float rate;
            if (zoomState == 4) {
                rate = zoomFollowRate;
                followLimit(zoomFollowRate, zoomFollowPos ? *zoomFollowPos : V3{}, zoomFollowOffset, zoomFollowFlags);
            } else {
                rate = 0.2f;
                V3 p = RV::add(tgt->pos, zoomOffset);
                float t;
                float s;
                t = target.x;
                s = p.x - t;
                t = t + s * 0.2f;
                target.x = t;
                t = target.y;
                s = p.y - t;
                t = t + s * 0.2f;
                target.y = t;
                t = target.z;
                s = p.z - t;
                t = t + s * 0.2f;
                target.z = t;
            }
            float targetFov = 0.0f;
            switch (zoomState) {
            case 1:
                targetFov = zoomFovy;
                break;
            case 2:
                targetFov = zoomFovyMedium;
                break;
            case 3:
                targetFov = 5.0f;
                break;
            case 0:
            case 4:
                targetFov = zoomFovyFar;
                break;
            default:
                break;
            }
            fovy = lerpClamped(fovy, targetFov, rate);
        } else if (startCount < startTotal) {
            ++startCount;
            startInterp(startTarget, targetSaved, startCount, startTotal);
            float s = 1.0f - float(startCount) / float(startTotal);
            float e = 1.0f - std::pow(s, 1.6f);
            fovy = lerpClamped(startFov, zoomFovyFar, e);
            if (startCount == startTotal) {
                fovy = zoomFovyFar;
            }
        }
        zoomRequest = 0;
    }

    // 0x20a830: move vCur towards vBase by 6 degrees per tick, and hCur towards hTarget (the short
    // way around the circle).
    void approach() {
        const float step = 6.0f;
        if (vCur < vBase) {
            vCur = vCur + step;
            if (vCur > vBase) {
                vCur = vBase;
            }
        } else {
            vCur = vCur - step;
            if (vBase > vCur) {
                vCur = vBase;
            }
        }
        float h = fmodDeg(hTarget + 360.0f, 360.0f);
        hTarget = h;
        if (hMode == 0) {
            float l2 = touchVel[0] * touchVel[0];
            l2 = l2 + touchVel[1] * touchVel[1];
            float length = std::sqrt(l2);
            if (length != 0.0f && f1f1 != 1) {
                hCur = h;
                return;
            }
            float s2 = h + 360.0f;
            float s1 = hCur;
            float s3 = s2 - s1;
            if (!(sbits(s3) < sbits(180.0f))) {
                s2 = h - 360.0f;
                s3 = s1 - s2;
                if (!(sbits(180.0f) <= sbits(s3))) {
                    h = s2;
                }
            } else {
                h = s2;
            }
            float next;
            if (s1 < h) {
                next = s1 + step;
                if (next > h) {
                    next = h;
                }
            } else {
                next = s1 - step;
                if (h > next) {
                    next = h;
                }
            }
            hCur = fmodDeg(next + 360.0f, 360.0f);
        } else {
            hCur = h;
        }
    }

    // 0x209d78: swing behind the target (ZL, or L+R).
    void snapBehind() {
        const float* m = target_mtx;
        V3 d{-m[2], -m[6], -m[10]};
        float h, v;
        dirToAngles(h, v, d);
        float vNow = outV;
        float oldH = hCur;
        resetAngles(h, vNow);
        float a = fmodDeg(hTarget + 360.0f, 360.0f);
        float b = fmodDeg(oldH + 360.0f, 360.0f);
        float diff = b - a;
        if (sbits(diff) > sbits(180.0f)) {
            diff = diff - 360.0f;
        } else if (bitsf(diff) > bitsf(-180.0f)) {
            diff = diff + 360.0f;
        }
        hCur = diff + hTarget;
        snapH = hCur;
        snapCount = 15.0f;
        snapActive = true;
        snapFront = {m[2], m[6], m[10]};
        snapPos = target_pos;
    }

    // 0x3eb534: has the target turned or moved since the snap?
    bool targetChangedSinceSnap(const CameraTarget& t) const {
        V3 f{t.baseMtx.m[2], t.baseMtx.m[6], t.baseMtx.m[10]};
        V3 d = RV::sub(f, snapFront);
        float l2 = d.x * d.x;
        l2 = l2 + d.y * d.y;
        l2 = l2 + d.z * d.z;
        if (std::sqrt(l2) > 0.001f) {
            return true;
        }
        V3 e = RV::sub(snapPos, t.pos);
        float m2 = e.x * e.x;
        m2 = m2 + e.y * e.y;
        m2 = m2 + e.z * e.z;
        return !(0.001f >= std::sqrt(m2));
    }

    // 0x20a9b8: swing behind the target while keeping the current eye circle. Returns false when the
    // line behind the target misses the circle (then 0x209d78 snaps outright).
    bool snapIfAligned() {
        const float* m = target_mtx;
        V3 t{target.x, target_pos.y, target.z};
        V3 d = RV::sub(target_pos, t);
        V3 back{-m[2], 0.0f, -m[10]};
        float l2 = back.x * back.x;
        l2 = l2 + back.y * back.y;
        l2 = l2 + back.z * back.z;
        if (l2 < fbits(0x358637be)) {
            return false;
        }
        RV::normalize(back);
        float projection = back.x * d.x;
        projection = projection + back.y * d.y;
        projection = projection + back.z * d.z;
        V3 q = RV::sub(target_pos, RV::scale(back, projection));   // closest point of the back line
        float a = q.x - t.x;
        float b = q.y - t.y;
        float c = q.z - t.z;
        float s1 = a * a;
        s1 = s1 + b * b;
        s1 = s1 + c * c;
        float distanceToLine = std::sqrt(s1);
        float ex = eye.x - t.x;
        float ez = eye.z - t.z;
        float ey = t.y - t.y;
        float r2 = ex * ex;
        r2 = r2 + ey * ey;
        r2 = r2 + ez * ez;
        float R = std::sqrt(r2);
        float k = R * R;
        k = k - distanceToLine * distanceToLine;
        if (!(k >= 0.0f)) {
            return false;
        }
        V3 candidate = RV::add(q, RV::scale(back, std::sqrt(k)));
        V3 dir = RV::sub(candidate, t);
        float dl = dir.x * dir.x;
        dl = dl + dir.y * dir.y;
        dl = dl + dir.z * dir.z;
        if (dl < fbits(0x358637be)) {
            dir = {0, 0, 0};
        } else {
            RV::normalize(dir);
        }
        float h, v;
        dirToAngles(h, v, dir);
        v = vCur + gyroVs;
        float oldGyroHs = gyroHs;
        float oldH = hCur;
        resetAngles(h, v);
        float x = fmodDeg(hTarget + 360.0f, 360.0f);
        float y = fmodDeg(oldH + 360.0f, 360.0f);
        hCur = wrap180(y - x) + hTarget;
        snapH = hCur;
        float g0 = fmodDeg(gyroH + 360.0f, 360.0f);
        float g1 = fmodDeg(oldGyroHs + 360.0f, 360.0f);
        gyroHs = wrap180(g1 - g0) + gyroH;
        snapCount = 15.0f;
        snapActive = true;
        snapFront = {m[2], m[6], m[10]};
        snapPos = target_pos;
        return true;
    }

    // 0x21ff74 -> 0x20b7f0 (+ tail 0x209ec8): one camera tick.
    void calc(const Context& c) {
        if (!hasTarget) {
            hasTarget = c.target && c.target->present;
            if (!hasTarget) {
                return;
            }
        }
        target_mtx = c.target->baseMtx.m;
        target_pos = c.target->pos;
        if (bitsf(hMin) == 0xc3340000u && bitsf(hMax) == 0x43340000u) {
            hMode = 0;   // -180..180: free
        } else {
            hMode = (hMin == 0.0f && hMax == 0.0f) ? 2 : 1;
        }
        if (delay > 0) {
            --delay;
        }
        if (snapActive && targetChangedSinceSnap(*c.target)) {
            snapActive = false;
        }
        if (snapCount == 0.0f) {
            if (delay == 0) {
                bool reset = c.pad.triggered(kZL) || (c.pad.triggered(kL) && c.pad.isHeld(kR)) ||
                             (c.pad.isHeld(kL) && c.pad.triggered(kR));
                if (!snapActive && reset && !snapIfAligned()) {
                    snapBehind();
                }
                orbitMotion();
                manualRotate(c);
            }
        } else if (c.globals->snapLock != 0) {
            snapCount = snapCount - 1.0f;
            if (snapCount == 0.0f) {
                c.globals->snapLock = 0;
            }
        } else {
            // A running snap: ease the horizontal angle over 15 ticks (quadratic).
            float t = snapCount * fbits(0x3d888889);
            t = t * t;
            float h = hTarget * (1.0f - t);
            h = h + snapH * t;
            hCur = h;
            orbitMotion();
            snapCount = snapCount - 1.0f;
        }
        dpadRotate(c);
        touchRotate(c);
        zoomUpdate(c);
        if (snapCount == 0.0f) {
            approach();
        }
        float v = gyroVs + vCur;
        if (v < vMin) {
            v = vMin;
        } else if (v > vMax) {
            v = vMax;
        }
        outV = v;
        if (hMode == 0) {
            outH = fmodDeg(gyroHs + hCur + 360.0f, 360.0f);
        } else if (hMode == 1) {
            float b = gyroHs + hCur;
            float lo = b + hMin;
            float hi = b + hMax;
            if (b < lo) {
                b = lo;
            } else if (b > hi) {
                b = hi;
            }
            outH = b;
        }
        prevV = outV;
        prevH = outH;
        updateEye(c);
    }

    // 0x209ec8: follow limits (zoom state 0), the up vector, and the eye from the orbit angles.
    void updateEye(const Context& c) {
        if (zoomState == 0) {
            followLimit(0.1f, c.target->pos, followOffset, follow);
        }
        up = upsideDown ? V3{-kUnitY.x, -kUnitY.y, -kUnitY.z} : kUnitY;
        eye = eyeAt(outV, outH);
    }

    // The eye for the orbit angles v, h (degrees) around the current target (0x209ec8's tail).
    V3 eyeAt(float v, float h) const {
        V3 dir = orbitDir(v, h);
        float dist = distance;
        if (distanceDetail) {
            // An ellipsoid of distances (X, Y, Z) instead of a sphere.
            float s16 = h * kDeg2Rad;
            float s21 = RM::cosf(s16) * distX;
            float s0 = RM::sinf(s16) * distZ;
            float s1 = s21 * s21;
            s1 = s1 + s0 * s0;
            float s20 = std::sqrt(s1);
            s16 = v * kDeg2Rad;
            float s17 = RM::cosf(s16) * s20;
            s0 = RM::sinf(s16) * distY;
            s1 = s17 * s17;
            s1 = s1 + s0 * s0;
            dist = std::sqrt(s1);
        }
        return RV::add(RV::scale(dir, dist), target);
    }

    // ---- Enhancement (not in the original): the host's smooth touch drag ----------------------
    // With hostDrag the original's tick-wise touch rotation (touchRotate) is off; the host turns
    // the camera by angles it gathers between ticks, shows them at once (eyeAt on outV / outH plus
    // the pending angles) and hands them over here at the start of the next tick. The limits are
    // the ticket's own: the vertical output stays within vMin..vMax, a fixed horizontal angle
    // (hMode 2) does not turn.
    bool hostDrag = false;

    // Whether the host may turn the camera now (the touch rotation is enabled and not blocked, no
    // swing behind the protagonist is running).
    bool dragAllowed(const CameraGlobals& g) const { return g.g[4] && g.touchCount <= 0 && snapCount == 0.0f; }

    // The part of a pending turn (dh, dv: degrees, screen sense) the limits allow.
    void clampDrag(float& dh, float& dv) const {
        if (hMode == 2) {
            dh = 0;
        }
        const float sign = upsideDown ? -1.0f : 1.0f;
        float v = outV + dv * sign;
        v = std::min(std::max(v, vMin), vMax);
        dv = (v - outV) * sign;
    }

    // The pending turn becomes the camera's: target and current angles move together (the
    // original's touch turn also sets hCur at once; the vertical 6-degree approach is skipped).
    void applyDrag(float dh, float dv) {
        clampDrag(dh, dv);
        const float sign = upsideDown ? -1.0f : 1.0f;
        hTarget = fmodDeg(hTarget + dh * sign + 360.0f, 360.0f);
        hCur = hTarget;
        if (dv != 0.0f) {
            // The ticket's angle may lie beyond its limit (shown at the limit): turn from what is
            // shown, without the original's dead travel back to the limit.
            const float raw = gyroVs + vCur;
            if (raw < vMin || raw > vMax) {
                vCur = outV - gyroVs;
            }
        }
        vCur = vCur + dv * sign;
        vBase = vCur;
        outV = outV + dv * sign;
        outH = fmodDeg(outH + dh * sign + 360.0f, 360.0f);
    }
};

// ---- Interpolator (director +0x148): 0x23f5d8 start, 0x23f7ec update, nerve 0x218c1c ----------------

struct Interpolator {
    bool active = false;
    int step = 0;
    int total = 0;
    V3 fromEye, fromTarget, fromUp{0, 1, 0};   // +0x70, +0x7c, +0x88
    V3 toEye, toTarget, toUp{0, 1, 0};         // +0x178, +0x184, +0x190
    V3 outEye, outTarget, outUp{0, 1, 0};      // +0x18, +0x24, +0x30
    V3 lastEye, lastTarget, lastUp{0, 1, 0};   // +0x120, +0x12c, +0x138
    float fromFov = 0;                         // +0x1a0
    float outFov = 0;                          // +0x19c

    // Start from the last shown view; `steps` < 0 uses the camera's InterpoleStep (in 60 Hz frames).
    void start(int steps, int interpoleStep, float currentFov) {
        fromEye = lastEye;
        fromTarget = lastTarget;
        fromUp = lastUp;
        RV::normalize(fromUp);
        fromFov = currentFov;
        if (steps < 0) {
            steps = interpoleStep;
        }
        total = steps;
        if (steps < 1) {
            active = false;
            total = 0;
            return;
        }
        total = int(float(steps) * 0.5f);
        step = 0;
        active = true;
    }

    // Returns the pose to show this tick (0x23f7ec + nerve + 0x3f02a4).
    Pose update(const Pose& cam) {
        toEye = cam.eye;
        toTarget = cam.target;
        toUp = cam.up;
        RV::normalize(toUp);
        Pose out;
        if (active) {
            // The nerve 0x218c1c runs with this tick's step counter: smoothstep 3t^2 - 2t^3.
            float t;
            if (total > 0) {
                t = float(step) / float(total);
                if (t < 0.0f) {
                    t = 0.0f;
                } else if (t > 1.0f) {
                    t = 1.0f;
                }
            } else {
                t = 1.0f;
            }
            float s = 3.0f;
            s = s - t * 2.0f;
            s = s * t;
            s = s * t;
            auto lerp = [&](float a, float b) {
                float d = b - a;
                return a + d * s;
            };
            outEye = {lerp(fromEye.x, toEye.x), lerp(fromEye.y, toEye.y), lerp(fromEye.z, toEye.z)};
            outTarget = {lerp(fromTarget.x, toTarget.x), lerp(fromTarget.y, toTarget.y),
                         lerp(fromTarget.z, toTarget.z)};
            outUp = rotateUp(s);
            float u = s;
            if (u < 0.0f) {
                u = 0.0f;
            } else if (u > 1.0f) {
                u = 1.0f;
            }
            float f = (1.0f - u) * fromFov;
            f = f + cam.fovy * u;
            outFov = f;
            ++step;
            if (step > total) {
                active = false;   // nerve end: step >= total after the update
            }
            out = {outEye, outTarget, outUp, outFov};
        } else {
            outEye = toEye;
            outTarget = toTarget;
            outUp = toUp;
            out = {toEye, toTarget, toUp, cam.fovy};
        }
        lastEye = outEye;
        lastTarget = outTarget;
        lastUp = outUp;
        RV::normalize(lastUp);
        RV::normalize(out.up);
        return out;
    }

    // Rotate the up vector from fromUp towards toUp by the fraction s. Default path for
    // non-opposed up vectors (dot >= 0); the opening keeps up = +Y.
    V3 rotateUp(float s) const {
        float dot = fromUp.x * toUp.x;
        dot = dot + fromUp.y * toUp.y;
        dot = dot + fromUp.z * toUp.z;
        V3 c = RV::cross(fromUp, toUp);
        float l = c.x * c.x;
        l = l + c.y * c.y;
        l = l + c.z * c.z;
        float angle = RM::atan2f(std::sqrt(l), dot) * kRad2Deg;
        float s17 = (angle * s) * kDeg2Rad;
        V3 axis = c;
        if (l < fbits(0x358637be)) {
            return fromUp;
        }
        RV::normalize(axis);
        float d = dot;
        if (d < -1.0f) {
            d = -1.0f;
        } else if (d > 1.0f) {
            d = 1.0f;
        }
        float a = RM::acosf(d);
        float magnitude = a;
        if (magnitude <= 0.0f) {
            magnitude = -magnitude;
        }
        if (magnitude < fbits(0x3a83126f)) {
            return fromUp;
        }
        float k = s17 / a;
        if (k < 0.0f) {
            k = 0.0f;
        } else if (k > 1.0f) {
            k = 1.0f;
        }
        RM::Quat q = RM::fromAxisAngle(axis, a * k);
        V3 r = fromUp;
        RM::rotate(r, q);
        RV::normalize(r);
        return r;
    }
};

// ---- Director: ticket slots, switching with interpolation, the per-tick view ---------------------------

struct Director {
    StartCamera* startCam = nullptr;   // slot 3 (one-shot)
    PlayerCamera* areaCam = nullptr;   // area slot
    CameraBase* current = nullptr;
    Interpolator interp;
    float fovy = 30.0f;   // +0x134
    Pose view;
    int areaStep = -1;   // ticket entry parameter (-1: the camera's InterpoleStep)

    // 0x234de8 / 0x2b41d4: the highest-priority camera that has not ended.
    CameraBase* select() const {
        if (startCam && !startCam->ended) {
            return startCam;
        }
        return areaCam;
    }

    // One 30 Hz tick: selection / switch (0x24dba4), then the camera tick (0x42aaec).
    void tick(const Context& c) {
        CameraBase* want = select();
        if (want != current) {
            CameraBase* previous = current;
            current = want;
            if (want == areaCam) {
                areaCam->start(c);
                // Between two player cameras (stage camera areas): the hand-over 0x20a5a8, found
                // by the class pair (0x3f4c00).
                if (previous && previous != startCam) {
                    areaCam->takeOver(*static_cast<PlayerCamera*>(previous));
                }
            } else if (want == startCam) {
                startCam->start(c);
            }
            if (previous) {
                // 0x3f4c00: class transfer (none between the Start and the player camera classes).
                interp.start(want == areaCam ? areaStep : -1, want->interpoleStep, fovy);
            }
        }
        if (current == startCam) {
            startCam->calc(c);
        } else if (current == areaCam) {
            areaCam->calc(c);
        }
        Pose p = current->pose();
        fovy = current->fovy;
        Pose o = interp.update(p);
        if (interp.active) {
            fovy = o.fovy;   // 0x3f02f8: the interpolator nerve is still running
        }
        view = o;
        view.fovy = fovy;
    }
};

// 0x42e4bc: view matrix (row-major 3x4, world to view) from eye, target and up; the original keeps
// the previous matrix when eye == target.
inline bool lookAt(M34& view, const V3& eye, const V3& target, const V3& up) {
    if (eye.x == target.x && eye.y == target.y && eye.z == target.z) {
        return false;
    }
    V3 z = RV::sub(eye, target);
    RV::normalize(z);
    V3 x = RV::cross(up, z);
    RV::normalize(x);
    V3 y = RV::cross(z, x);
    float t0 = x.x * eye.x;
    t0 = t0 + x.y * eye.y;
    t0 = t0 + x.z * eye.z;
    t0 = -t0;
    float t1 = y.x * eye.x;
    t1 = t1 + y.y * eye.y;
    t1 = -t1 - y.z * eye.z;
    float t2 = z.x * eye.x;
    t2 = t2 + z.y * eye.y;
    t2 = -t2 - z.z * eye.z;
    view = {{x.x, x.y, x.z, t0, y.x, y.y, y.z, t1, z.x, z.y, z.z, t2}};
    return true;
}

// 0x2f2074: general 3x4 inverse (cofactors); the identity when the determinant is +-0.
inline M34 inverse(const M34& M) {
    const float* m = M.m;
    const float s0 = m[0], s1 = m[1], s2 = m[2];
    const float s3 = m[4], s4 = m[5], s5 = m[6];
    const float s6 = m[8], s7 = m[9], s8 = m[10];
    float s10 = s0 * s4;
    float s11 = s1 * s5;
    float s12 = s2 * s3;
    const float s13 = s6 * s4;
    const float s14 = s3 * s1;
    const float s15 = s0 * s7;
    s10 = s10 * s8;
    s11 = s11 * s6;
    s12 = s12 * s7;
    s10 = s10 - s13 * s2;
    s11 = s11 - s14 * s8;
    s12 = s12 - s15 * s5;
    float determinant = s10 + s11;
    determinant = determinant + s12;
    if (bitsf(determinant) == 0x80000000u || bitsf(determinant) == 0u) {
        return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    }
    const float inv = 1.0f / determinant;
    float c16 = s4 * s8;
    float c17 = s1 * s8;
    float c18 = s1 * s5;
    float c19 = s3 * s8;
    float c20 = s0 * s8;
    float c21 = s0 * s5;
    float c22 = s3 * s7;
    float c23 = s0 * s7;
    float c24 = s0 * s4;
    c16 = c16 - s7 * s5;
    c17 = c17 - s7 * s2;
    c18 = c18 - s4 * s2;
    c19 = c19 - s6 * s5;
    c20 = c20 - s6 * s2;
    c21 = c21 - s3 * s2;
    c22 = c22 - s6 * s4;
    c23 = c23 - s6 * s1;
    c24 = c24 - s3 * s1;
    const float r00 = c16 * inv, r01 = -(c17 * inv), r02 = c18 * inv;
    const float r10 = -(c19 * inv), r11 = c20 * inv, r12 = -(c21 * inv);
    const float r20 = c22 * inv, r21 = -(c23 * inv), r22 = c24 * inv;
    const float m3 = m[3], m7 = m[7], m11 = m[11];
    float t0 = -(r00 * m3);
    t0 = t0 - r01 * m7;
    t0 = t0 - r02 * m11;
    float t1 = -(r10 * m3);
    t1 = t1 - r11 * m7;
    t1 = t1 - r12 * m11;
    float t2 = -(r20 * m3);
    t2 = t2 - r21 * m7;
    t2 = t2 - r22 * m11;
    return {{r00, r01, r02, t0, r10, r11, r12, t1, r20, r21, r22, t2}};
}

// The pad's camera matrix: the player tick 0x15c5e4 inverts the view (0x2f2074) before 0x2d7edc.
inline void cameraToWorld(const M34& view, float cam[12]) {
    M34 w = inverse(view);
    for (int i = 0; i < 12; ++i) {
        cam[i] = w.m[i];
    }
}

}   // namespace captain_toad::RecoveredCamera
