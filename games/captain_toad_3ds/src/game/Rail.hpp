// Recovered rails (original game, ARM11 main code): paths placed in the map that characters
// follow, such as the companion's walk through the opening.
//
// A rail is a list of map points ("RailPoints"), each with two Bezier handles ("ControlPoints":
// [0] the incoming handle, [1] the outgoing one). The game turns it into a chain of short straight
// segments when the stage loads, and characters then move along it by arc length ("walk 16 units
// along the rail") through a rail mover.
//
// Original operation order; compile with -ffp-contract=off. Validated against the rail the game
// built in memory (evidence/companion/rail-dump.json, tools/validate_rail.cpp: all 581 segments
// bit-exact).
//
//   0x32b784 builder: for each span P_i -> P_{i+1} (handles: ControlPoints[1] of P_i, [0] of P_{i+1}),
//            a straight span (handles within +-0.1 of their points) becomes one linear segment; a
//            curved span is tessellated into N linear pieces at uniform t of the cubic Bezier, with
//            N = int((dot of the handle directions + 1) * 50 * 0.5), at least 1, plus
//            int(min(300 / length, 1) * 20).
//   0x28327c segment from four points (linear when both handles coincide within +-0.1)
//   0x21e96c cubic Bezier in polynomial form
//   0x299238 Simpson arc length (10 steps, floored to 1/1024)
//   0x220ba4 linear segment
//   0x25ef8c lookup by arc length; 0x3ecd74 length -> parameter (linear: ratio; Bezier: Newton
//            iteration, 0x3ec8a4); 0x3ecf3c position; 0x3fcc14 direction
//   0x280ce0 clamp (open rails) or wrap (closed rails) of a rail coordinate
//   0x2f7788 mover step, 0x2ebd28 "near the end" test
#pragma once

#include <cmath>
#include <vector>

#include "game/Math.hpp"
#include "game/VecLib.hpp"

namespace captain_toad::RecoveredRail {

using captain_toad::RecoveredVec::V3;
namespace RV = RecoveredVec;

// Float from bit pattern, and a float's bits as a signed integer (the original sometimes compares
// floats this way, which only differs from a float compare for negative values or NaN).
inline float fb(uint32_t b) {
    return captain_toad::RecoveredMath::fbits(b);
}
inline int32_t sb(float f) {
    return int32_t(captain_toad::RecoveredMath::bitsf(f));
}

// A cubic Bezier segment in polynomial form: B(t) = a0 + c1 t + c2 t^2 + c3 t^3.
struct Bezier {
    V3 a0, c1, c2, c3;
    float length = 0;

    // 0x3eca44: speed |B'(t)|.
    float speed(float t) const {
        float t3 = t * 3.0f;
        float t2 = t * 2.0f;
        float s16 = t3 * t;
        V3 v = RV::addScaled(c2, c1, t2);
        v = RV::addScaled(c3, v, s16);
        float l = v.x * v.x;
        l = l + v.y * v.y;
        l = l + v.z * v.z;
        return std::sqrt(l);
    }

    // 0x299238: arc length over [a, b] by the composite Simpson rule with 2n intervals, floored to
    // 1/1024. (The odd and even samples inline the speed formula with their own register order.)
    float arcLength(float a, float b, int n) const {
        float fa = speed(a);
        float fb2 = speed(b);
        float s24 = fa + fb2;
        s24 = s24 * 0.5f;
        float h = b - a;
        float twoN = float(n) * 2.0f;
        h = h * (1.0f / twoN);
        float odd = 0.0f;
        float even = 0.0f;
        for (int i = 1; i <= n; ++i) {
            float fi = float(i);
            float k = -1.0f;
            k = k + fi * 2.0f;   // VNMLS: 2i - 1
            float ti = fi * 2.0f;
            float t = a;
            t = t + h * k;
            float te = a;
            te = te + h * ti;
            {
                float s2 = t * 3.0f;
                float s1 = t * 2.0f;
                float s25 = s2 * t;
                V3 v = RV::addScaled(c2, c1, s1);
                v = RV::addScaled(c3, v, s25);
                float l = v.x * v.x;
                l = l + v.y * v.y;
                l = l + v.z * v.z;
                odd = std::sqrt(l) + odd;
            }
            if (i == n) {
                break;
            }
            {
                float s1 = te * 3.0f;
                float s0 = te * 2.0f;
                float s16 = s1 * te;
                V3 v = RV::addScaled(c2, c1, s0);
                v = RV::addScaled(c3, v, s16);
                float l = v.x * v.x;
                l = l + v.y * v.y;
                l = l + v.z * v.z;
                even = std::sqrt(l) + even;
            }
        }
        s24 = s24 + even * 2.0f;
        float third = h * fb(0x3eaaaaab);   // h / 3
        s24 = s24 + odd * 4.0f;
        float r = s24 * third;
        r = r * 1024.0f;
        r = std::floor(r);           // 0x278c40 floorf
        return r * fb(0x3a800000);   // * 1/1024
    }

    // 0x3ecbc0: position at parameter t.
    V3 eval(float t) const {
        float t2 = t * t;
        float t3 = t2 * t;
        V3 p = RV::addScaled(c1, a0, t);
        p = RV::addScaled(c2, p, t2);
        return RV::addScaled(c3, p, t3);
    }

    // 0x3ec844: derivative at parameter t (the travel direction).
    V3 derivative(float t) const {
        float s2 = t * 3.0f;
        float s0 = t * 2.0f;
        float s16 = s2 * t;
        V3 d = RV::addScaled(c2, c1, s0);
        return RV::addScaled(c3, d, s16);
    }

    // 0x3ec8a4: arc length -> parameter (Newton iteration, at most 5 steps, tolerance 0.01).
    float param(float len) const {
        float t = len / length;
        float s = arcLength(0.0f, t, 10);
        float error = len - s;
        if (error <= 0.0f) {
            error = -error;
        }
        if (!(sb(error) > sb(0.01f))) {
            return t;
        }
        for (int i = 0; i < 5; ++i) {
            float t3 = t * 3.0f;
            float t2 = t * 2.0f;
            float s19 = t3 * t;
            V3 v = RV::addScaled(c2, c1, t2);
            v = RV::addScaled(c3, v, s19);
            float l = v.x * v.x;
            l = l + v.y * v.y;
            l = l + v.z * v.z;
            float currentSpeed = std::sqrt(l);
            if (sb(currentSpeed) < sb(0.001f)) {
                currentSpeed = 0.001f;
            }
            float next = (len - s) / currentSpeed;
            next = next + t;
            if (next < 0.0f) {
                next = 0.0f;
            } else if (next > 1.0f) {
                next = 1.0f;
            }
            t = next;
            float s2 = arcLength(0.0f, t, 10);
            float d = len - s2;
            if (d <= 0.0f) {
                d = -d;
            }
            if (!(sb(0.01f) < sb(d))) {
                return t;
            }
            s = s2;
        }
        if (!(s < 0.0f) && !(sb(t) > sb(1.0f))) {
            return t;
        }
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
        return t;
    }
};

// 0x21e96c: Bezier from its four control points, converted to polynomial form.
inline Bezier makeBezier(const V3& p0, const V3& p1, const V3& p2, const V3& p3) {
    V3 d0 = RV::sub(p1, p0);
    V3 d1 = RV::sub(p2, p1);
    V3 d2 = RV::sub(p3, p2);
    V3 e0 = RV::sub(d1, d0);
    V3 e1 = RV::sub(d2, d1);
    V3 f = RV::sub(e1, e0);
    Bezier b;
    b.a0 = p0;
    b.c1 = RV::scale(d0, 3.0f);
    b.c2 = RV::scale(e0, 3.0f);
    b.c3 = f;
    b.length = b.arcLength(0.0f, 1.0f, 10);
    return b;
}

// A straight segment.
struct Linear {
    V3 start;
    V3 delta;
    float length = 0;

    // 0x3ecd74 (linear branch): arc length -> parameter.
    float param(float len) const {
        float L = length;
        float a = L;
        if (a <= 0.0f) {
            a = -a;
        }
        if (a < 0.001f) {
            return 0.0f;
        }
        float s1 = len;
        if (s1 < 0.0f) {
            s1 = 0.0f;
        } else if (s1 > L) {
            s1 = L;
        }
        return s1 / L;
    }

    // 0x3ecf50
    V3 eval(float t) const { return RV::addScaled(delta, start, t); }
};

// 0x220ba4
inline Linear makeLinear(const V3& a, const V3& b) {
    Linear line;
    line.start = a;
    line.delta = RV::sub(b, a);
    float s = line.delta.x * line.delta.x;
    s = s + line.delta.y * line.delta.y;
    s = s + line.delta.z * line.delta.z;
    line.length = std::sqrt(s);
    return line;
}

// One rail segment (linear or Bezier) with the cumulative length at its end.
struct Segment {
    bool bezier = false;
    Bezier bz;
    Linear ln;
    float cum = 0;

    // 0x283264
    float length() const { return bezier ? bz.length : ln.length; }
};

// True when two points coincide within +-0.1 on every axis (a handle "at" its point).
inline bool near01(const V3& a, const V3& b) {
    auto within = [](float d) {
        return !(-0.1f > d) && !(d > 0.1f);
    };
    return within(a.x - b.x) && within(a.y - b.y) && within(a.z - b.z);
}

// 0x28327c
inline Segment makeSegment(const V3& p0, const V3& p1, const V3& p2, const V3& p3) {
    Segment s;
    if (near01(p0, p1) && near01(p3, p2)) {
        s.bezier = false;
        s.ln = makeLinear(p0, p3);
    } else {
        s.bezier = true;
        s.bz = makeBezier(p0, p1, p2, p3);
    }
    return s;
}

// A map rail point: position and handles (ControlPoints[0] = in, [1] = out).
struct RailPoint {
    V3 pos, in, out;
};

struct Rail {
    std::vector<Segment> segs;
    std::vector<int> spanOf;      // map span of each segment
    std::vector<int> spanStart;   // first segment of each span
    std::vector<RailPoint> points;
    bool closed = false;

    // 0x295b98: total length.
    float total() const { return segs.empty() ? 0.0f : segs.back().cum; }

    // 0x32b784: build the segment chain from the map points.
    void build(const std::vector<RailPoint>& railPoints, bool isClosed) {
        points = railPoints;
        closed = isClosed;
        segs.clear();
        spanOf.clear();
        spanStart.clear();
        const int n = int(railPoints.size());
        if (n <= 0) {
            return;
        }
        if (n == 1) {
            Segment s = makeSegment(railPoints[0].pos, railPoints[0].pos, railPoints[0].pos, railPoints[0].pos);
            s.cum = 0;
            segs = {s};
            spanOf = {0};
            spanStart = {0};
            return;
        }
        const int spans = closed ? n : n - 1;
        float cum = 0.0f;
        for (int i = 0; i < spans; ++i) {
            const RailPoint& a = railPoints[i];
            const RailPoint& b = railPoints[(i + 1) % n];
            spanStart.push_back(int(segs.size()));

            // Straight span: one linear segment.
            if (near01(a.pos, a.out) && near01(b.pos, b.in)) {
                Segment s = makeSegment(a.pos, a.pos, b.pos, b.pos);
                cum = s.length() + cum;
                s.cum = cum;
                segs.push_back(s);
                spanOf.push_back(i);
                continue;
            }

            // Curved span: tessellate the Bezier. More pieces for sharper bends (handle directions
            // pointing the same way) and for short spans.
            Segment curve = makeSegment(a.pos, a.out, b.in, b.pos);
            V3 u = RV::sub(b.in, b.pos);
            V3 v = RV::sub(a.out, a.pos);
            RV::normalize(u);
            RV::normalize(v);
            float d = v.x * u.x;
            d = d + v.y * u.y;
            d = d + v.z * u.z;
            d = d + 1.0f;
            d = d * 50.0f;
            d = d * 0.5f;
            int count = int(d);
            if (count <= 1) {
                count = 1;
            }
            float q = 300.0f / curve.length();
            float m = q < 1.0f ? q : 1.0f;
            count = count + int(m * 20.0f);
            float dt = 1.0f / float(count);
            float t = 0.0f;
            V3 previous = a.pos;
            for (int k = 0; k < count; ++k) {
                t = t + dt;
                V3 p = curve.bezier ? curve.bz.eval(t) : curve.ln.eval(t);
                Segment s = makeSegment(previous, previous, p, p);
                cum = s.length() + cum;
                s.cum = cum;
                segs.push_back(s);
                spanOf.push_back(i);
                previous = p;
            }
        }
    }

    // 0x25ef8c: the segment containing rail coordinate s, and the distance into it.
    int lookup(float s, float& local) const {
        const float totalLength = total();
        if (closed) {
            float r = std::fmod(s, totalLength);
            if (r < 0.0f) {
                r = totalLength + r;
            }
            s = r;
        } else if (s < 0.0f) {
            s = 0.0f;
        } else if (s > totalLength) {
            s = totalLength;
        }
        int index = -1;
        float distance = 0.0f;
        for (int i = 0; i < int(segs.size()); ++i) {
            if (!(s > segs[i].cum)) {
                distance = s;
                if (i > 0) {
                    distance = distance - segs[i - 1].cum;
                }
                index = i;
                break;
            }
        }
        if (index < 0) {
            index = int(segs.size()) - 1;   // (not reached for valid coordinates)
        }
        float L = segs[index].length();
        if (distance < 0.0f) {
            distance = 0.0f;
        } else if (distance > L) {
            distance = L;
        }
        local = distance;
        return index;
    }

    // 0x280c74: position and unit travel direction at rail coordinate s.
    void evaluate(float s, V3& pos, V3& dir) const {
        float local;
        int i = lookup(s, local);
        const Segment& segment = segs[i];
        float t = segment.bezier ? segment.bz.param(local) : segment.ln.param(local);
        pos = segment.bezier ? segment.bz.eval(t) : segment.ln.eval(t);
        // 0x3fcc14: direction; a degenerate derivative falls back to the chord, then to +Z.
        dir = segment.bezier ? segment.bz.derivative(t) : segment.ln.delta;
        float l2 = dir.x * dir.x;
        l2 = l2 + dir.y * dir.y;
        l2 = l2 + dir.z * dir.z;
        if (l2 < fb(0x358637be)) {
            V3 a = segment.bezier ? segment.bz.a0 : segment.ln.start;
            V3 e = segment.bezier
                       ? RV::add(RV::add(RV::add(segment.bz.a0, segment.bz.c1), segment.bz.c2), segment.bz.c3)
                       : RV::add(segment.ln.start, segment.ln.delta);
            dir = RV::sub(e, a);
            float m2 = dir.x * dir.x;
            m2 = m2 + dir.y * dir.y;
            m2 = m2 + dir.z * dir.z;
            if (m2 < fb(0x358637be)) {
                dir = {0.0f, 0.0f, 1.0f};
            }
        }
        RV::normalize(dir);
    }

    // 0x280ce0: wrap (closed rail) or clamp (open rail) a coordinate.
    float clampCoord(float s) const {
        const float totalLength = total();
        if (closed) {
            float r = std::fmod(s, totalLength);
            if (r < 0.0f) {
                r = totalLength + r;
            }
            return r;
        }
        if (s < 0.0f) {
            return 0.0f;
        }
        if (s > totalLength) {
            return totalLength;
        }
        return s;
    }
};

// Rail mover (actor +0x5c -> +4): coordinate +0x1c, speed +0x20, direction flag +0x24.
struct Mover {
    const Rail* rail = nullptr;
    V3 pos;
    V3 dir;
    float coord = 0;
    float speed = 0;
    bool forward = true;

    void place(float s) {
        coord = rail->clampCoord(s);
        rail->evaluate(coord, pos, dir);
    }

    // 0x2f75ac -> 0x2f7788: move by v along the rail.
    void move(float v) {
        speed = v;
        float d = forward ? speed : -speed;
        coord = coord + d;
        coord = rail->clampCoord(coord);
        rail->evaluate(coord, pos, dir);
    }

    // 0x2ebd28: within `margin` of the end in the travel direction (open rails only).
    bool nearEnd(float margin) const {
        if (rail->closed) {
            return false;
        }
        if (forward) {
            float e = rail->total() - margin;
            return !(e > coord);
        }
        return !(coord > margin);
    }
};

}   // namespace captain_toad::RecoveredRail
