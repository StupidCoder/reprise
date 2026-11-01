// Recovered rail guide of the companion: the second character of the opening, who walks ahead of the
// player along a rail through the ruins, waits for him at points along the way, turns to face him
// and waves ("ByeBye") when he lingers. In the original she is a PlayerNpc actor whose delegating
// state 0x4f65a8 hands control to a rail controller (0x335ce1c0, vtable 0x4c79c0).
// Original operation order; compile with -ffp-contract=off.
// Evidence: evidence/runs/companion-*; validated per tick by tools/validate_companion.cpp
// (evidence/companion/validation.txt).
//
// How she decides where to go:
//   - The rail's map points are possible stopping places. A "target" point index only ever grows.
//   - Rule A (every tick, 0x1c7f58): find the furthest rail point within 500 units of the player
//     (and within 195 vertically); the target becomes that point + 1 (unless it is the first or
//     the last point). So she always stays one point ahead of where the player has got to.
//   - Rule B (while walking, 0x2ad70c): the same search without the +1; otherwise advance the target
//     by one when she is already closer to the next point than to the current one.
//
// Her states (nerve objects 0x4f65bc..0x4f65d0; controller tick 0x238028 runs the nerve, then rule
// A unless she has finished):
//   Wait   0x4278f4 clip "Wait"    Walk when not at the target point; ByeBye after +0x28 ticks;
//                                  Turn when the player is more than 40 degrees off her facing.
//   Walk   0x4276b0 clip "Walk"    The rail mover advances 16 units per tick; she turns towards the
//                                  rail direction (10 degrees per tick). End near the rail's end.
//                                  Rule B. After at least 60 walking ticks at the target point,
//                                  Wait (for 14 ticks).
//   Turn   0x427864 clip "Turn"    Turn towards the player (10 degrees per tick, done within 1).
//   ByeBye 0x4277b4 clip "ByeBye"  Wave until the clip ends, then Wait for 180 ticks.
//   End    0x427978                Reached near the rail's end when the player lingers: she turns
//                                  in place (the Walk clip continues) at most 10 degrees per tick
//                                  towards her placement front (+0x1c, from the map's Rotate Y;
//                                  0x2c7edc / 0x28b804); once facing it, clip "WaitHappy" and the
//                                  controller ends (vtable +0x10).
//
// Her pose: a quaternion (0x3f4474 matrix), set-front 0x2f7be4 (look rotation 0x2ef328 about
// up = -gravity (0,1,0), matrix -> quaternion 0x2fd018), turning 0x2ad884, signed angle 0x29e408.
#pragma once

#include <string>
#include <vector>

#include "game/Math.hpp"
#include "game/Rail.hpp"
#include "game/VecLib.hpp"

namespace captain_toad::RecoveredCompanion {

using captain_toad::RecoveredVec::M34;
using captain_toad::RecoveredVec::V3;
namespace RV = RecoveredVec;
namespace RM = RecoveredMath;

inline float fb(uint32_t b) {
    return RM::fbits(b);
}
inline int32_t sb(float f) {
    return int32_t(RM::bitsf(f));
}

// Degree/radian factors as the original's float constants.
constexpr float kD2R = 0.01745329238474369f;
constexpr float kR2D = 57.2957763671875f;

// Position and orientation of the companion (actor pose +4, quaternion +0x34).
struct Pose {
    V3 pos;
    float q[4] = {0, 0, 0, 1};   // x, y, z, w

    // 0x3f4474: rotation matrix with the position as translation.
    M34 matrix() const {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        float yy = y * y;
        float xx = x * x;
        float zz = z * z;
        float wz = w * z;
        float xy = x * y;
        float xz = x * z;
        float yz = y * z;
        float wx = w * x;
        float wy = w * y;
        float yy2 = yy * 2.0f;
        float xx2 = xx * 2.0f;
        float zz2 = zz * 2.0f;
        float wz2 = wz * 2.0f;
        float xy2 = xy * 2.0f;
        float xz2 = xz * 2.0f;
        float yz2 = yz * 2.0f;
        float wx2 = wx * 2.0f;
        float wy2 = wy * 2.0f;
        float a = 1.0f - yy2;
        float b = 1.0f - xx2;
        float m01 = xy2 - wz2;
        float m10 = xy2 + wz2;
        float m02 = xz2 + wy2;
        float m20 = xz2 - wy2;
        float m12 = yz2 - wx2;
        float m21 = yz2 + wx2;
        float m00 = a - zz2;
        float m22 = b - yy2;
        float m11 = b - zz2;
        return {{m00, m01, m02, pos.x, m10, m11, m12, pos.y, m20, m21, m22, pos.z}};
    }

    void setMatrix(const M34& M) {   // 0x2fd018
        const RM::Quat r = RM::fromMatrix(M);
        q[0] = r.x;
        q[1] = r.y;
        q[2] = r.z;
        q[3] = r.w;
    }

    // 0x2ef328 look rotation with up = (0,1,0) and front f, then matrix -> quaternion (0x2fd018);
    // the tail of 0x2c7edc (no parallel check).
    void setLook(const V3& f) {
        const V3 up{0.0f, 1.0f, 0.0f};
        V3 u = up;
        RV::normalize(u);
        V3 X = RV::cross(up, f);
        RV::normalize(X);
        V3 Z = RV::cross(X, u);
        M34 m{{X.x, u.x, Z.x, 0, X.y, u.y, Z.y, 0, X.z, u.z, Z.z, 0}};
        setMatrix(m);
    }

    // 0x2f7be4: face direction f, keeping "up" opposite to gravity (0,-1,0). A front almost
    // parallel to gravity resets the orientation.
    void setFront(const V3& f) {
        const V3 g{0.0f, -1.0f, 0.0f};
        auto big = [](float v) {
            if (v <= 0.0f) {
                v = -v;
            }
            return v > 0.025f;
        };
        float c0 = f.y * g.z;
        c0 = c0 - f.z * g.y;
        float c1 = f.z * g.x;
        c1 = c1 - f.x * g.z;
        float c2 = f.x * g.y;
        c2 = c2 - f.y * g.x;
        if (!big(c0) && !big(c1) && !big(c2)) {
            q[0] = 0;
            q[1] = 0;
            q[2] = 0;
            q[3] = 1;
            return;
        }
        V3 up{-g.x, -g.y, -g.z};
        // 0x2ef328 look rotation: X = normalize(up x front), Y = normalize(up), Z = X x Y.
        V3 u = up;
        RV::normalize(u);
        V3 X = RV::cross(up, f);
        RV::normalize(X);
        V3 Z = RV::cross(X, u);
        M34 m{{X.x, u.x, Z.x, 0, X.y, u.y, Z.y, 0, X.z, u.z, Z.z, 0}};
        setMatrix(m);
    }
};

// Projection onto the plane normal to `up`: v + up * -(up . v) (inlined in 0x29e408 / 0x2ad46c).
inline V3 projectOut(const V3& up, const V3& v) {
    float s = up.x * v.x;
    s = s + up.y * v.y;
    s = -s - up.z * v.z;
    return RV::addScaled(up, v, s);
}

// 0x29e408: signed angle (degrees) from the pose's front to `dir`, about the pose's up axis.
inline float signedAngle(const Pose& p, const V3& dir) {
    M34 m = p.matrix();
    V3 front{m.m[2], m.m[6], m.m[10]};
    V3 up{m.m[1], m.m[5], m.m[9]};
    V3 a = projectOut(up, front);
    V3 b = projectOut(up, dir);
    float d = a.x * b.x;
    d = d + a.y * b.y;
    d = d + a.z * b.z;
    V3 c = RV::cross(a, b);
    float l = c.x * c.x;
    l = l + c.y * c.y;
    l = l + c.z * c.z;
    float angle = RM::atan2f(std::sqrt(l), d) * kR2D;
    V3 c2 = RV::cross(a, b);
    float s = up.x * c2.x;
    s = s + up.y * c2.y;
    s = s + up.z * c2.z;
    if (s < 0.0f) {
        angle = -angle;
    }
    return angle;
}

// 0x2ad884: turn towards `dir` by at most maxDeg; true when the remaining error is within doneDeg.
inline bool turn(Pose& p, const V3& dir, float maxDeg, float doneDeg) {
    float a = signedAngle(p, dir);
    if (a > 0.0f) {
        if (maxDeg < a) {
            a = maxDeg;
        }
    } else if (a < 0.0f) {
        float n = -maxDeg;
        if (a < n) {
            a = n;
        }
    }
    M34 m = p.matrix();
    V3 front{m.m[2], m.m[6], m.m[10]};
    V3 up{m.m[1], m.m[5], m.m[9]};
    RM::Quat q = RM::fromAxisAngle(up, a * kD2R);
    V3 f = front;
    RM::rotate(f, q);
    p.setFront(f);
    float e = a;
    if (e <= 0.0f) {
        e = -e;
    }
    return !(e > doneDeg);
}

// 0x29e590: turn towards a point.
inline bool turnToPoint(Pose& p, const V3& point, float maxDeg, float doneDeg) {
    V3 d = RV::sub(point, p.pos);
    float l = d.x * d.x;
    l = l + d.y * d.y;
    l = l + d.z * d.z;
    if (l < fb(0x358637be)) {
        d = {0, 0, 1};
    } else {
        RV::normalize(d);
    }
    return turn(p, d, maxDeg, doneDeg);
}

// 0x2dc0b8: normalise, or zero a vector shorter than 1e-3 (returns true when zeroed).
inline bool normalizeOrZero(V3& v) {
    float l = v.x * v.x;
    l = l + v.y * v.y;
    l = l + v.z * v.z;
    if (l < fb(0x358637be)) {
        v = {0, 0, 0};
        return true;
    }
    RV::normalize(v);
    return false;
}

// 0x28b804: turn front f towards d within the plane normal to gravity g, limited to the angle
// whose cosine is c. Returns true when f reached d.
inline bool frontTowards(V3& f, const V3& d, float c, const V3& g) {
    float s = g.x * d.x;
    s = s + g.y * d.y;
    s = -s - g.z * d.z;
    V3 dp{d.x + g.x * s, d.y + g.y * s, d.z + g.z * s};   // 0x2fd388
    normalizeOrZero(dp);
    s = g.x * f.x;
    s = s + g.y * f.y;
    s = -s - g.z * f.z;
    V3 fp{f.x + g.x * s, f.y + g.y * s, f.z + g.z * s};
    normalizeOrZero(fp);
    auto len2 = [](const V3& v) {
        float l = v.x * v.x;
        l = l + v.y * v.y;
        l = l + v.z * v.z;
        return l;
    };
    const float eps = fb(0x358637be);
    if (len2(fp) < eps) {
        fp = {-dp.x, -dp.y, -dp.z};
    }
    const float ld = len2(dp);
    if (ld < eps) {
        return false;
    }
    if (RM::bitsf(c) >= 0xbf800000u) {   // limit at or beyond 180 degrees
        f = dp;
        return true;
    }
    if (len2(fp) < eps || ld < eps) {
        return false;
    }
    float dot = fp.x * dp.x;
    dot = dot + fp.y * dp.y;
    dot = dot + fp.z * dp.z;
    if (dot > c) {
        f = dp;
        RV::normalize(f);
        return true;
    }
    float one = 1.0f;
    one = one - c * c;
    const float sn = std::sqrt(one);
    V3 t{dp.x + fp.x * -dot, dp.y + fp.y * -dot, dp.z + fp.z * -dot};
    if (len2(t) < eps) {   // opposite: nudge sideways
        V3 side = RV::cross(fp, g);
        RV::normalize(side);
        f = {fp.x + side.x * 0.02f, fp.y + side.y * 0.02f, fp.z + side.z * 0.02f};
        RV::normalize(f);
        return false;
    }
    RV::normalize(t);
    f = RV::scale(fp, c);
    f = {f.x + t.x * sn, f.y + t.y * sn, f.z + t.z * sn};
    RV::normalize(f);
    return false;
}

// 0x2c7edc: turn the pose towards direction d by at most maxDeg (gravity (0,-1,0)). True when
// it faces d.
inline bool turnToDirection(Pose& p, const V3& d, float maxDeg) {
    M34 m = p.matrix();
    V3 f{m.m[2], m.m[6], m.m[10]};
    const bool reached = frontTowards(f, d, RM::cosf(maxDeg * kD2R), {0.0f, -1.0f, 0.0f});
    p.setLook(f);
    return reached;
}

struct Controller {
    enum State { Wait, Walk, Turn, ByeBye, End };

    State state = Wait;
    int step = 0;
    bool done = false;
    int target = 0;           // +0x18 target rail point index
    int waitTicks = 180;      // +0x28 ticks before waving in Wait
    V3 endDir;                // +0x1c
    std::vector<V3> points;   // +0x10 rail point positions (+0x14 count)
    captain_toad::RecoveredRail::Rail rail;
    captain_toad::RecoveredRail::Mover mover;
    Pose pose;
    std::string clip = "Wait";   // the clip the current state started (0x2fce24)
    int clipStarted = 0;         // incremented on every start
    bool clipEnded = false;      // supplied by the animation player (0x2fa1b8)

    void setState(State s) {
        state = s;
        step = 0;
    }

    void startClip(const char* c) {
        clip = c;
        ++clipStarted;
        clipEnded = false;
    }

    // 0x2ad698: within 16 units of rail point i.
    bool atPoint(int i) const {
        V3 point = points[i];
        float dx = point.x - pose.pos.x;
        float dy = point.y - pose.pos.y;
        float dz = point.z - pose.pos.z;
        float l = dx * dx;
        l = l + dy * dy;
        l = l + dz * dz;
        return !(sb(std::sqrt(l)) > sb(16.0f));
    }

    // The furthest rail point within 500 units of the player and 195 vertically (rules A and B).
    int furthestNear(const V3& player) const {
        int last = 0;
        for (int i = 0; i < int(points.size()); ++i) {
            const V3& point = points[i];
            float dx = point.x - player.x;
            float dy = point.y - player.y;
            float dz = point.z - player.z;
            float l = dx * dx;
            l = l + dy * dy;
            l = l + dz * dz;
            if (sb(std::sqrt(l)) > sb(500.0f)) {
                continue;
            }
            float a = dy;
            if (a <= 0.0f) {
                a = -a;
            }
            if (!(sb(a) > sb(195.0f))) {
                last = i;
            }
        }
        return last;
    }

    // 0x1c7f58: rule A, stay one point ahead of the player.
    void ruleA(const V3& player) {
        int last = furthestNear(player);
        const int n = int(points.size());
        if (last != 0 && n - 1 > last) {
            last = last + 1;
        }
        if (last > target) {
            target = last;
        }
    }

    // 0x2ad70c: rule B, used while walking.
    void ruleB(const V3& player) {
        const int n = int(points.size());
        if (!(n - 1 > target)) {
            return;
        }
        int last = furthestNear(player);
        if (last > target) {
            target = last;
            return;
        }
        // The closer-point test uses the companion's own position (actor pose +4).
        const V3 a = points[target];
        const V3 b = points[target + 1];
        const V3 p = pose.pos;
        float bx = p.x - b.x;
        float by = p.y - b.y;
        float bz = p.z - b.z;
        float lb = bx * bx;
        lb = lb + by * by;
        lb = lb + bz * bz;
        float ax = p.x - a.x;
        float ay = p.y - a.y;
        float az = p.z - a.z;
        float la = ax * ax;
        la = la + ay * ay;
        la = la + az * az;
        if (std::sqrt(lb) < std::sqrt(la)) {
            target = target + 1;
        }
    }

    // 0x2ad46c: true when the player is more than 40 degrees off her facing (about +Y).
    bool facingFar(const V3& player) const {
        M34 m = pose.matrix();
        V3 front{m.m[2], m.m[6], m.m[10]};
        V3 d = RV::sub(player, pose.pos);
        float l = d.x * d.x;
        l = l + d.y * d.y;
        l = l + d.z * d.z;
        if (l < fb(0x358637be)) {
            d = {0, 0, 1};
        } else {
            RV::normalize(d);
        }
        const V3 up{0, 1, 0};
        V3 a = projectOut(up, d);
        V3 b = projectOut(up, front);
        float dot = a.x * b.x;
        dot = dot + a.y * b.y;
        dot = dot + a.z * b.z;
        V3 c = RV::cross(a, b);
        float cl = c.x * c.x;
        cl = cl + c.y * c.y;
        cl = cl + c.z * c.z;
        float angle = RM::atan2f(std::sqrt(cl), dot) * kR2D;
        V3 c2 = RV::cross(a, b);
        float s = up.x * c2.x;
        s = s + up.y * c2.y;
        s = s + up.z * c2.z;
        if (s < 0.0f) {
            angle = -angle;
        }
        if (angle <= 0.0f) {
            angle = -angle;
        }
        return sb(angle) > sb(40.0f);
    }

    // One controller tick (0x238028). Returns true when the controller has ended.
    bool tick(const V3& player) {
        const State before = state;
        execute(player);
        if (state == before) {
            ++step;
        }
        if (done) {
            return true;
        }
        ruleA(player);
        return done;
    }

    // The current state's nerve.
    void execute(const V3& player) {
        switch (state) {
        case Wait:
            if (step == 0) {
                startClip("Wait");
            }
            if (!atPoint(target)) {
                setState(Walk);
                return;
            }
            if (step >= waitTicks) {
                setState(ByeBye);
                return;
            }
            if (facingFar(player)) {
                setState(Turn);
            }
            return;

        case Walk:
            if (step == 0) {
                startClip("Walk");
            }
            mover.move(16.0f);
            pose.pos = mover.pos;
            turn(pose, mover.dir, 10.0f, 1.0f);
            if (mover.nearEnd(16.0f)) {
                setState(End);
                return;
            }
            ruleB(player);
            if (!atPoint(target)) {
                return;
            }
            if (step < 60) {
                return;
            }
            waitTicks = 14;
            setState(Wait);
            return;

        case Turn: {
            if (step == 0) {
                startClip("Turn");
            }
            bool facing = turnToPoint(pose, player, 10.0f, 1.0f);
            if (!atPoint(target)) {
                setState(Walk);
                return;
            }
            if (facing) {
                setState(ByeBye);
            }
            return;
        }

        case ByeBye:
            if (step == 0) {
                startClip("ByeBye");
            }
            if (!atPoint(target)) {
                setState(Walk);
                return;
            }
            if (facingFar(player)) {
                setState(Turn);   // a Wait set below overrides it
            }
            if (clipEnded) {
                waitTicks = 180;
                setState(Wait);
            }
            return;

        case End:   // 0x427978
            if (done) {
                return;
            }
            if (turnToDirection(pose, endDir, 10.0f)) {
                startClip("WaitHappy");
                done = true;   // controller vtable +0x10: ends
            }
            return;
        }
    }
};

}   // namespace captain_toad::RecoveredCompanion
