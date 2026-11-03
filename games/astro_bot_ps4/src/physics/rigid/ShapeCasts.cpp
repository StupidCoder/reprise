#include "physics/rigid/ShapeCasts.hpp"

#include "math/Vector.hpp"
#include "physics/rigid/MeshTree.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::physics::rigid {

using math::Vec3;

namespace {

constexpr float kTiny = 1e-5f;           // squared lengths / denominators treated as zero
constexpr float kFltMin = 1.17549435e-38f;

float axis(const Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
float& axis(Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

// Length as the library takes it: 0 for squared lengths at or below the smallest normal float.
float guardedLength(const Vec3& v) {
    const float vv = math::dot(v, v);
    return kFltMin < vv ? std::sqrt(vv) : 0.0f;
}

// The ray in the frame of `pose` (rotation without scale).
void toLocal(const Pose& pose, const CastRay& ray, Vec3& start, Vec3& dir) {
    const Mat3 rt = transpose(pose.rotation);
    start = apply(rt, ray.start) + apply(rt, -pose.translation);   // as the library sums it
    dir = apply(rt, ray.direction);
}

void record(CastResult& result, float t, const Vec3& point, const Vec3& normal) {
    result.t = t;
    result.point = point;
    result.normal = normal;
    result.hit = true;
    result.island = result.facet = -1;
}

}   // namespace

// 0xea8510: the quadratic of the ray against a sphere; inside at the start: t = 0.
bool raySphere(float radius, const Vec3& start, const Vec3& dir, const Vec3& centre, float& t) {
    const Vec3 m = start - centre;
    const float c = math::dot(m, m) - radius * radius;
    if (c < 0) {
        t = 0;
        return true;
    }
    const float dd = math::dot(dir, dir);
    if (dd < kTiny) {
        return false;
    }
    const float b = math::dot(m, dir);
    const float disc = b * b - dd * c;
    if (disc < 0) {
        return false;
    }
    const float hit = (-b - std::sqrt(disc)) / dd;
    if (hit < 0 || 1.0f < hit) {
        return false;
    }
    t = hit;
    return true;
}

// 0xea85f0: a capsule from a to b. Inside at the start: t = 0; else the side (an infinite
// cylinder about the axis, kept when the axial position falls within the segment), else the
// nearer of the two end spheres.
bool raySegment(float radius, const Vec3& start, const Vec3& dir, const Vec3& a, const Vec3& b, float& t) {
    const float dd = math::dot(dir, dir);
    if (dd < kTiny) {
        return false;
    }
    const float r2 = radius * radius;
    const Vec3 seg = b - a;
    const float ss = math::dot(seg, seg);
    const auto endSphere = [&](const Vec3& centre, float& out) {
        const Vec3 m = start - centre;
        const float bm = math::dot(m, dir);
        const float disc = bm * bm - dd * (math::dot(m, m) - r2);
        if (disc < 0) {
            return false;
        }
        out = (-bm - std::sqrt(disc)) / dd;
        return 0 <= out && out <= 1.0f;
    };
    if (ss < kTiny) {
        const Vec3 m = start - a;
        const float c = math::dot(m, m) - r2;
        if (c < 0) {
            t = 0;
            return true;
        }
        const float bm = math::dot(m, dir);
        const float disc = bm * bm - dd * c;
        if (disc < 0) {
            return false;
        }
        const float hit = (-bm - std::sqrt(disc)) / dd;
        if (hit < 0 || 1.0f < hit) {
            return false;
        }
        t = hit;
        return true;
    }
    const Vec3 m = start - a;
    const float along = std::max(0.0f, std::min(math::dot(seg, m) / ss, 1.0f));
    const Vec3 off = a + seg * along - start;
    if (math::dot(off, off) < r2) {
        t = 0;
        return true;
    }
    const float len = std::sqrt(ss);
    const Vec3 u = seg * (1.0f / len);
    const float mu = math::dot(m, u), du = math::dot(dir, u);
    const float qa = dd - du * du;
    const float qb = math::dot(m, dir) - mu * du;
    const float disc = qb * qb - qa * (math::dot(m, m) - mu * mu - r2);
    if (disc < 0) {
        return false;
    }
    if (kTiny <= std::fabs(qa)) {
        const float side = (-qb - std::sqrt(disc)) / qa;
        if (0 <= side && side <= 1.0f) {
            const float axial = mu + du * side;
            if (0 <= axial && axial * axial <= ss) {
                t = side;
                return true;
            }
        }
    }
    float best = 999.0f, end = 0;
    if (endSphere(a, end)) {
        best = std::min(end, best);
    }
    if (endSphere(b, end)) {
        best = std::min(best, end);
    }
    if (1.0f <= best) {
        return false;
    }
    t = best;
    return true;
}

// 0xea9140: a box grown by the radius (rounded edges and corners). Within the radius of the box
// at the start: t = 0; else the slabs of the box grown by the radius, then, at that point, the
// number of axes beyond the box decides: one, a face (the slab t); two, the edge as a capsule;
// three, the corner as a sphere.
bool rayBox(float radius, const Vec3& start, const Vec3& dir, const Vec3& half, float& t) {
    const Vec3 nearest = math::max(math::min(start, half), -half);
    const Vec3 off = start - nearest;
    if (math::dot(off, off) < radius * radius) {
        t = 0;
        return true;
    }
    const Vec3 lo = -half - Vec3{radius, radius, radius}, hi = half + Vec3{radius, radius, radius};
    bool inside = true;
    for (int i = 0; i < 3; ++i) {
        inside = inside && axis(lo, i) < axis(start, i) && axis(start, i) < axis(hi, i);
    }
    float enter = 0;
    if (!inside) {
        float tmin = -1e38f, tmax = 1e38f;
        for (int i = 0; i < 3; ++i) {
            const float s = axis(start, i), d = axis(dir, i);
            if (std::fabs(d) < kTiny) {
                if (s < axis(lo, i) || axis(hi, i) < s) {
                    return false;
                }
                continue;
            }
            // The library's quotient: a refined reciprocal, then one more refinement step.
            const float r0 = 1.0f / d;
            const float r1 = (r0 + r0) - r0 * d * r0;
            const float x1 = (axis(lo, i) - s) * r1, x2 = (axis(hi, i) - s) * r1;
            const float t1 = (x1 + x1) - x1 * d * r1, t2 = (x2 + x2) - x2 * d * r1;
            tmin = std::max(tmin, std::min(t1, t2));
            tmax = std::min(tmax, std::max(t1, t2));
        }
        if (!(tmin <= tmax && 0 <= tmin && tmin <= 1.0f)) {
            return false;
        }
        enter = tmin;
    }
    const Vec3 p = start + dir * enter;
    Vec3 cornerA, cornerB;   // edge ends: below -> -h else h; above -> h else -h
    int outside = 0;
    for (int i = 0; i < 3; ++i) {
        const float h = axis(half, i), v = axis(p, i);
        const bool below = v < -h, above = h < v;
        outside += int(below) + int(above);
        axis(cornerA, i) = below ? -h : h;
        axis(cornerB, i) = above ? h : -h;
    }
    if (outside == 3) {
        const float dd = math::dot(dir, dir);
        if (dd < kTiny) {
            return false;
        }
        const Vec3 m = start - cornerB;
        const float b = math::dot(dir, m);
        const float disc = b * b - dd * (math::dot(m, m) - radius * radius);
        if (disc < 0) {
            return false;
        }
        const float hit = (-b - std::sqrt(disc)) / dd;
        if (hit < 0 || 1.0f < hit) {
            return false;
        }
        t = hit;
        return true;
    }
    if (outside == 2) {
        float hit = 0;
        if (!raySegment(radius, start, dir, cornerA, cornerB, hit) || 1.0f < hit || hit < 0) {
            return false;
        }
        t = hit;
        return true;
    }
    if (outside == 1) {
        t = enter;
        return true;
    }
    return false;
}

// 0xe4caa0.
bool castSphere(float radius, const Pose& pose, const CastRay& ray, CastResult& result) {
    const Vec3 centre = pose.translation;
    float t = 0;
    if (!raySphere(radius + ray.radius, ray.start, ray.direction, centre, t) || result.t <= t) {
        return false;
    }
    const Vec3 p = ray.start + ray.direction * t;
    const Vec3 d = p - centre;
    const float len = guardedLength(d);
    const Vec3 n = kTiny <= len ? d * (1.0f / len) : Vec3{1, 0, 0};
    if (0 < t) {
        record(result, t, p - n * ray.radius, n);
        return true;
    }
    // Overlapping at the start: the sphere's surface point, or the ray's when that is nearer.
    Vec3 q = centre + n * radius;
    if (ray.radius < guardedLength(q - ray.start)) {
        q = ray.start + n * ray.radius;
    }
    record(result, t, q, math::dot(n, ray.start - q) < 0 ? -n : n);
    return true;
}

// 0xea9590.
bool castBox(const Vec3& half, const Pose& pose, const CastRay& ray, CastResult& result) {
    Vec3 start, dir;
    toLocal(pose, ray, start, dir);
    float t = 0;
    if (!rayBox(ray.radius, start, dir, half, t) || result.t <= t) {
        return false;
    }
    const Vec3 p = start + dir * t;
    bool inside = true;
    for (int i = 0; i < 3; ++i) {
        inside = inside && -axis(half, i) < axis(p, i) && axis(p, i) < axis(half, i);
    }
    Vec3 q;
    if (inside) {
        // Out to the nearest face (the first axis of the smallest gap).
        float gap[3];
        for (int i = 0; i < 3; ++i) {
            gap[i] = axis(half, i) - std::fabs(axis(p, i));
        }
        const float least = std::min(gap[0], std::min(gap[1], gap[2]));
        const int face = gap[0] <= least ? 0 : gap[1] <= least ? 1 : 2;
        q = p;
        axis(q, face) += std::copysign(least, axis(p, face));
    } else {
        q = math::max(math::min(p, half), -half);
    }
    const Vec3 d = p - q;
    const float dd = math::dot(d, d);
    const Vec3 n = 1e-10f <= dd ? d * (1.0f / std::sqrt(dd)) : Vec3{1, 0, 0};
    if (ray.radius * ray.radius < dd) {
        bool startInside = true;
        for (int i = 0; i < 3; ++i) {
            startInside = startInside && std::fabs(axis(start, i)) <= axis(half, i);
        }
        if (startInside) {
            q = p - n * ray.radius;
        }
    }
    record(result, t, apply(pose.rotation, q) + pose.translation, apply(pose.rotation, n));
    return true;
}

// The contact on a rounded shape once the hit point p and the shape's surface point q with its
// normal n are known (capsule 0xea9950, cylinder 0xeaa9d0): within the radius (+1e-5) the surface
// point, with the normal from p when they are apart; else the ray's surface point towards q.
static void surfaceContact(float rayRadius, float t, const Vec3& p, const Vec3& q, Vec3 n, CastResult& result) {
    const Vec3 w = q - p;
    const float len = guardedLength(w);
    if (len <= rayRadius + kTiny) {
        if (kTiny <= len) {
            n = -(w * (1.0f / len));
        }
        record(result, t, q, n);
        return;
    }
    const Vec3 u = w * (1.0f / len);
    record(result, t, p + u * rayRadius, -u);
}

// 0xea9950.
bool castCapsule(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result) {
    Vec3 start, dir;
    toLocal(pose, ray, start, dir);
    float t = 0;
    if (!raySegment(radius + ray.radius, start, dir, {-halfLength, 0, 0}, {halfLength, 0, 0}, t) || result.t <= t) {
        return false;
    }
    const Vec3 p = ray.start + ray.direction * t;
    const Vec3 a = apply(pose.rotation, {-halfLength, 0, 0}) + pose.translation;
    const Vec3 b = apply(pose.rotation, {halfLength, 0, 0}) + pose.translation;
    const Vec3 seg = b - a;
    const float along = std::max(0.0f, std::min(math::dot(p - a, seg) / math::dot(seg, seg), 1.0f));
    const Vec3 c = a + seg * along;
    const Vec3 e = p - c;
    const float ee = math::dot(e, e);
    const Vec3 n = ee <= 1e-10f ? apply(pose.rotation, {0, 0, 1}) : e * (1.0f / std::sqrt(ee));
    surfaceContact(ray.radius, t, p, c + n * radius, n, result);
    return true;
}

// 0xea8a80.
void cylinderSurface(float radius, const Vec3& p, const Vec3& a, const Vec3& b, Vec3& point, Vec3& normal) {
    const Vec3 seg = b - a;
    const float len = guardedLength(seg);
    if (len < kTiny) {
        point = a + seg * 0.5f;   // degenerate: the normal is left as it was
        return;
    }
    const Vec3 u = seg * (1.0f / len);
    const float along = math::dot(p - a, u);
    const Vec3 radial = p - (a + u * along);
    const float rho = guardedLength(radial);
    float toA, toB;   // distances to the two caps
    if (rho <= radius) {
        toA = std::fabs(along);
        toB = std::fabs(along - len);
    } else {
        const float out = (rho - radius) * (rho - radius);
        toA = std::sqrt(along * along + out);
        toB = std::sqrt((along - len) * (along - len) + out);
    }
    if (std::min(toA, toB) <= std::fabs(radius - rho)) {
        // A cap is nearer than the side.
        const bool atB = toB <= toA;
        const Vec3 centre = atB ? b : a;
        point = kTiny <= rho ? centre + radial * (std::min(radius, rho) / rho) : centre;
        normal = atB ? u : -u;
        return;
    }
    const Vec3 c = a + u * std::max(0.0f, std::min(along, len));
    if (kTiny <= rho) {
        normal = radial * (1.0f / rho);
        point = c + normal * radius;
        return;
    }
    // On the axis.
    if (0 <= along && along <= len) {
        normal = {0, 0, 1};   // the library's fixed choice (not turned with the cylinder)
        point = c + normal * radius;
    } else {
        normal = 0 <= along ? u : -u;
        point = c;
    }
}

namespace {

// The library's inline sine / cosine polynomials (0xea9df0).
void rimSinCos(float x, float& s, float& c) {
    const float x2 = x * x;
    s = x + x * x2 * (x2 * (x2 * -0.000195073f + 0.008332076f) + -0.166666523f);
    c = x2 * (x2 * (x2 * -0.001360225f + 0.041656695f) + -0.499999017f) + 1.0f;
}

Vec3 rotateAbout(const Vec3& axis, float halfAngle, const Vec3& v) {
    float s, c;
    rimSinCos(halfAngle, s, c);
    const Vec3 q = axis * s;
    const Vec3 t = math::cross(q, v) * 2.0f;
    return v + t * c + math::cross(q, t);
}

// The point of a cylinder's rim (centre, axis `axis`, radius) nearest to p.
Vec3 rimPoint(float radius, const Vec3& centre, const Vec3& axis, const Vec3& p) {
    Vec3 radial = p - centre;
    radial = radial - axis * math::dot(radial, axis);
    return centre + radial * (radius / std::sqrt(math::dot(radial, radial)));
}

// 0xea9df0: the ray (radius r) against the rim of a cap, a torus, approximated: spheres at the
// rim point nearest the hit point p and 27 degrees either side of it; from the first one hit,
// up to ten refinements with the sphere at the rim point nearest the new hit until the hit
// moves less than sqrt(1e-5).
bool rayRim(float r, float radius, const Vec3& p, const Vec3& start, const Vec3& dir, const Vec3& centre,
            const Vec3& axis, float& t) {
    const Vec3 nearest = rimPoint(radius, centre, axis, p);
    const Vec3 arm = nearest - centre;
    float hit = 0;
    if (!raySphere(r, start, dir, nearest, hit) &&
        !raySphere(r, start, dir, centre + rotateAbout(axis, 0.23561947f, arm), hit) &&
        !raySphere(r, start, dir, centre + rotateAbout(axis, -0.23561947f, arm), hit)) {
        return false;
    }
    Vec3 at = start + dir * hit;
    for (int i = 0; i < 10; ++i) {
        if (!raySphere(r, start, dir, rimPoint(radius, centre, axis, at), hit)) {
            return false;
        }
        const Vec3 next = start + dir * hit;
        const Vec3 moved = at - next;
        if (math::dot(moved, moved) < kTiny) {
            t = std::max(0.0f, std::min(hit, 1.0f));
            return true;
        }
        at = next;
    }
    return false;
}

}   // namespace

// 0xeaa360: inside at the start (within r of the surface, or in the solid): t = 0; else the side
// of the cylinder grown by r (axis extended by r at both ends): within the grown ends at least r
// away from them a hit, near an end the rim; outside the ends (or with the ray along the axis)
// the cap plane entered first: within the radius a hit, up to radius + r the rim.
bool rayCylinder(float r, float radius, const Vec3& start, const Vec3& dir, const Vec3& a, const Vec3& b, float& t) {
    const float dd = math::dot(dir, dir);
    if (dd < kTiny) {
        return false;
    }
    const Vec3 seg = b - a;
    const float ss = math::dot(seg, seg);
    if (ss < kTiny) {
        return false;
    }
    const float len = std::sqrt(ss);
    const Vec3 u = seg * (1.0f / len);
    Vec3 q, n;
    cylinderSurface(radius, start, a, b, q, n);
    const Vec3 off = start - q;
    if (math::dot(off, off) < r * r) {
        t = 0;
        return true;
    }
    const float axial = math::dot(start - a, u);
    if (0 <= axial && axial <= len) {
        const Vec3 radial = start - (a + u * axial);
        if (math::dot(radial, radial) < radius * radius) {
            t = 0;
            return true;
        }
    }
    const Vec3 a2 = a - u * r, b2 = b + u * r;
    const Vec3 m = start - a2;
    const float mu = math::dot(m, u), du = math::dot(dir, u);
    const float grown = (r + radius) * (r + radius);
    const float qa = dd - du * du;
    const float qb = math::dot(m, dir) - mu * du;
    const float disc = qb * qb - qa * (math::dot(m, m) - mu * mu - grown);
    if (disc < 0) {
        return false;
    }
    const float extent = r + r + len;
    if (kTiny <= std::fabs(qa)) {
        float side = (-qb - std::sqrt(disc)) / qa;
        const float s = mu + du * side;
        if (0 <= s && s <= extent) {
            const Vec3 p = start + dir * side;
            if (s < r) {
                if (!rayRim(r, radius, p, start, dir, a, -u, side)) {
                    return false;
                }
                t = side;
                return true;
            }
            if (extent - r < s) {
                if (!rayRim(r, radius, p, start, dir, b, u, side)) {
                    return false;
                }
                t = side;
                return true;
            }
            if (0 <= side && side <= 1.0f) {
                t = side;
                return true;
            }
        }
    }
    // The cap planes (axial 0 and |w|^2 along w = b2 - a2).
    const Vec3 w = b2 - a2;
    const float den = math::dot(w, dir);
    if (std::fabs(den) < kTiny) {
        return false;
    }
    const float wm = math::dot(w, m);
    const float t0 = (0.0f - wm) / den, t1 = (extent * extent - wm) / den;
    const bool atB = t1 <= t0;
    const float tc = atB ? t1 : t0;
    const Vec3 rel = (atB ? start - b2 : m) + dir * tc;
    const float rr = math::dot(rel, rel);
    if (grown < rr) {
        return false;
    }
    if (rr <= radius * radius) {
        if (0 <= tc && tc <= 1.0f) {
            t = tc;
            return true;
        }
        return false;
    }
    float rim = 0;
    if (!rayRim(r, radius, start + dir * tc, start, dir, atB ? b : a, atB ? u : -u, rim)) {
        return false;
    }
    t = rim;
    return true;
}

// 0xeaa9d0 (the ray stays in world space; the axis ends are moved there).
bool castCylinder(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result) {
    const Vec3 a = pose.translation + apply(pose.rotation, {-halfLength, 0, 0});
    const Vec3 b = pose.translation + apply(pose.rotation, {halfLength, 0, 0});
    float t = 0;
    if (!rayCylinder(ray.radius, radius, ray.start, ray.direction, a, b, t) || result.t <= t) {
        return false;
    }
    const Vec3 p = ray.start + ray.direction * t;
    Vec3 q, n;
    cylinderSurface(radius, p, a, b, q, n);
    surfaceContact(ray.radius, t, p, q, n, result);
    return true;
}

namespace {

// The closest point of triangle abc to p (Voronoi regions, as 0xe3d3c0 tests them).
Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a, ac = c - a;
    const Vec3 ap = p - a;
    const float d1 = math::dot(ab, ap), d2 = math::dot(ac, ap);
    if (!(0 < d1 || 0 < d2)) {
        return a;
    }
    const Vec3 bp = p - b;
    const float d3 = math::dot(ab, bp), d4 = math::dot(ac, bp);
    if (!(d3 < 0 || d3 < d4)) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (!(0 < d3 || d1 < 0 || 0 < vc)) {
        return a + ab * (d1 / (d1 - d3));
    }
    const Vec3 cp = p - c;
    const float d5 = math::dot(ab, cp), d6 = math::dot(ac, cp);
    if (!(d6 < 0 || d6 < d5)) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (!(0 < d6 || d2 < 0 || 0 < vb)) {
        return a + ac * (d2 / (d2 - d6));
    }
    const float va = d3 * d6 - d5 * d4;
    if (!(0 < va || d4 - d3 < 0 || d5 - d6 < 0)) {
        const float e1 = d4 - d3;
        return b + (c - b) * (e1 / (e1 + (d5 - d6)));
    }
    const float denom = 1.0f / (vb + va + vc);
    return ac * (vc * denom) + a + ab * (vb * denom);
}

}   // namespace

bool castHull(const HullGeometry& hull, const Pose& pose, const CastRay& ray, CastResult& result) {
    const HullSupport shape(hull.points, hull.pointCount);
    const SphereSupport sphere(ray.radius);
    const ConvexCast cast = castConvex(shape, pose, sphere, Pose{Mat3{}, ray.start}, ray.direction);
    if (cast.status == 1) {
        // Overlapping at the start: the nearest facet point among the facets facing the start.
        if (hull.indexCount < 3) {
            return false;
        }
        float best = 1e38f;
        Vec3 nearest;
        bool found = false;
        const auto corner = [&](int i) { return apply(pose.rotation, hull.points[hull.indices[i]]) + pose.translation; };
        for (int f = 0; f + 2 < hull.indexCount; f += 3) {
            const Vec3 a = corner(f + (hull.mirrored ? 2 : 0));
            const Vec3 b = corner(f + 1);
            const Vec3 c = corner(f + (hull.mirrored ? 0 : 2));
            const Vec3 n = math::cross(b - a, c - a);
            if (math::dot(ray.start - a, n * (1.0f / std::sqrt(math::dot(n, n)))) < 0) {
                continue;
            }
            const Vec3 q = closestOnTriangle(ray.start, a, b, c);
            const Vec3 d = q - ray.start;
            const float dd = math::dot(d, d);
            if (dd <= best) {
                best = dd;
                nearest = q;
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        Vec3 normal{1, 0, 0};
        if (1e-10f < best) {
            const Vec3 d = nearest - ray.start;
            normal = -(d * (1.0f / std::sqrt(math::dot(d, d))));
        }
        record(result, 0, nearest, normal);
        return true;
    }
    if (cast.status != 0 || !(cast.t < result.t)) {
        return false;
    }
    // The point on the ray's sphere (relative to its centre) gives both the normal and the point.
    const Vec3 onSphere = cast.pointB;
    const Vec3 normal = -(onSphere * (1.0f / std::sqrt(math::dot(onSphere, onSphere))));
    record(result, cast.t, onSphere + (ray.start + ray.direction * cast.t), normal);
    return true;
}

bool sphereTriangle(float radius, const Vec3& start, const Vec3& dir, const Vec3& a, const Vec3& b, const Vec3& c,
                    float& t, float& distance2) {
    const Vec3 q = closestOnTriangle(start, a, b, c);
    const Vec3 off = q - start;
    const float dd = math::dot(off, off);
    if (dd <= radius * radius + kTiny) {
        t = 0;
        distance2 = dd;
        return true;
    }
    const Vec3 n0 = math::cross(b - a, c - a);
    const Vec3 lift = n0 * (radius / std::sqrt(math::dot(n0, n0)));
    const Vec3 a2 = a + lift, b2 = b + lift, c2 = c + lift;   // the face moved out by the radius
    const Vec3 ab = b2 - a2, ac = c2 - a2;
    const Vec3 back = -dir;
    const Vec3 n = math::cross(ab, ac);
    const float den = math::dot(back, n);
    if (0 < den) {
        const Vec3 ap = start - a2;
        const float hit = math::dot(ap, n) / den;
        if (0 <= hit && hit <= 1.0f) {
            const Vec3 e = math::cross(back, ap);
            const float v = math::dot(ac, e) / den;
            if (-1e-5f <= v && v <= 1.00001001f) {
                const float w = -math::dot(ab, e) / den;
                if (-1e-5f <= w && v + w <= 1.00001001f) {
                    t = hit;
                    distance2 = radius * radius;
                    return true;
                }
            }
        }
    }
    float best = 999.0f, edge = 0;
    if (raySegment(radius, start, dir, a, b, edge) && edge < best) {
        best = edge;
    }
    if (raySegment(radius, start, dir, b, c, edge) && edge < best) {
        best = edge;
    }
    if (raySegment(radius, start, dir, c, a, edge) && edge < best) {
        best = edge;
    }
    if (1.0f <= best) {
        return false;
    }
    t = best;
    distance2 = radius * radius;
    return true;
}

namespace {

// The ray against a box grown by `grow` (mesh frame): the entry t when the slabs overlap.
bool rayEntersBox(const meshtree::Box& box, float grow, const Vec3& start, const Vec3& dir, float& entry) {
    float tmin = -1e38f, tmax = 1e38f;
    for (int i = 0; i < 3; ++i) {
        const float centre = (axis(box.max, i) + axis(box.min, i)) * 0.5f;
        const float half = grow + (axis(box.max, i) - axis(box.min, i)) * 0.5f;
        const float lo = centre - half, hi = centre + half;
        const float s = axis(start, i), d = axis(dir, i);
        if (std::fabs(d) < kTiny) {
            if (s < lo || hi < s) {
                return false;
            }
            continue;
        }
        const float r0 = 1.0f / d;
        const float r1 = (r0 + r0) - r0 * d * r0;
        const float x1 = (hi - s) * r1, x2 = (lo - s) * r1;
        const float t1 = (x1 + x1) - d * r1 * x1, t2 = (x2 + x2) - d * r1 * x2;
        tmin = std::max(tmin, std::min(t1, t2));
        tmax = std::min(tmax, std::max(t1, t2));
    }
    entry = tmin;
    return tmin <= tmax;
}

}   // namespace

namespace {

struct MeshBest {
    bool hit = false;
    int island = 0, facet = 0;
    float t = 1, distance2 = 3.40282347e38f, facing = 1;
};

// The walk shared by the mesh casts (0xe46180 / 0xe2f560): the mesh tree and each island's tree
// against the movement's boxes grown by `grow` (mesh frame), boxes entered no earlier than the best
// hit dropped (island trees of capsule casts: entered later); the facets the start lies in front
// of go to `test(a, b, c, t, distance2)` (world space; distance2 in: the limit, out: the start's
// squared distance when touching); of hits within 1e-5 the most head-on facet wins, a touching
// start (t = 0) keeps the nearest.
template <typename Test>
MeshBest walkMesh(const MeshPlacement& placement, const Vec3& worldStart, const Vec3& worldDir, float radius,
                  float limitT, bool laterIslandNodes, const Test& test) {
    const data::LargeMesh& mesh = *placement.mesh;
    const Mat3& m = placement.pose.rotation;
    const Mat3 inv = inverse(m);
    const Vec3 start = apply(inv, worldStart) + apply(inv, -placement.pose.translation);
    const Vec3 dir = apply(inv, worldDir);
    float smallest = 1e38f;
    for (int c = 0; c < 3; ++c) {
        const Vec3 col{c == 0 ? m.r[0].x : c == 1 ? m.r[0].y : m.r[0].z, c == 0 ? m.r[1].x : c == 1 ? m.r[1].y : m.r[1].z,
                       c == 0 ? m.r[2].x : c == 1 ? m.r[2].y : m.r[2].z};
        smallest = std::min(smallest, math::length(col));
    }
    const float grow = radius / smallest;
    MeshBest best;
    best.t = limitT;
    const auto keep = [&](const meshtree::Box& box) {
        float entry = 0;
        return rayEntersBox(box, grow, start, dir, entry) && entry < best.t;
    };
    const auto keepIsland = [&](const meshtree::Box& box) {
        float entry = 0;
        return rayEntersBox(box, grow, start, dir, entry) && (laterIslandNodes ? entry <= best.t : entry < best.t);
    };
    const auto world = [&](const data::MeshIsland& island, int vertex) {
        return apply(m, island.vertices[std::size_t(vertex)]) + placement.pose.translation;
    };
    const meshtree::Box root{mesh.centre - mesh.halfExtent, mesh.centre + mesh.halfExtent};
    meshtree::walk(mesh.tree, root, keep, [&](const unsigned* islands, int count) {
        for (int k = 0; k < count; ++k) {
            const data::MeshIsland& island = mesh.islands[islands[k]];
            const meshtree::Box box{island.min, island.max};
            if (!keep(box)) {
                continue;
            }
            meshtree::walk(island.tree, box, keepIsland, [&](const unsigned* facets, int n) {
                for (int j = 0; j < n; ++j) {
                    const data::MeshFacet& f = island.facets[facets[j]];
                    const meshtree::Corners cs = meshtree::corners(f, placement.mirrored);
                    const Vec3 a = world(island, cs.v[0]), b = world(island, cs.v[1]), c = world(island, cs.v[2]);
                    const Vec3 n0 = math::cross(b - a, c - a);
                    const Vec3 normal = n0 * (1.0f / std::sqrt(math::dot(n0, n0)));
                    if (!(0 < math::dot(worldStart - a, normal))) {
                        continue;
                    }
                    float t = 0, d2 = 0;
                    if (!test(a, b, c, t, d2) || best.distance2 < d2) {
                        continue;
                    }
                    const float facing = math::dot(normal, worldDir);
                    if ((d2 < best.distance2 && t == 0) || t < best.t + -1e-5f ||
                        (t < best.t + 1e-5f && facing <= best.facing)) {
                        best = {true, int(islands[k]), int(facets[j]), t, d2, facing};
                    }
                }
            });
        }
    });
    return best;
}

// The best facet's corners in world space.
void bestCorners(const MeshPlacement& placement, const MeshBest& best, Vec3& a, Vec3& b, Vec3& c) {
    const data::MeshIsland& island = placement.mesh->islands[std::size_t(best.island)];
    const meshtree::Corners cs = meshtree::corners(island.facets[std::size_t(best.facet)], placement.mirrored);
    const auto world = [&](int vertex) {
        return apply(placement.pose.rotation, island.vertices[std::size_t(vertex)]) + placement.pose.translation;
    };
    a = world(cs.v[0]), b = world(cs.v[1]), c = world(cs.v[2]);
}

}   // namespace

bool castMesh(const MeshPlacement& placement, const CastRay& ray, CastResult& result) {
    const MeshBest best = walkMesh(placement, ray.start, ray.direction, ray.radius, result.t, false,
                                   [&](const Vec3& a, const Vec3& b, const Vec3& c, float& t, float& d2) {
                                       return sphereTriangle(ray.radius, ray.start, ray.direction, a, b, c, t, d2);
                                   });
    if (!best.hit) {
        return false;
    }
    Vec3 a, b, c;
    bestCorners(placement, best, a, b, c);
    const Vec3 p = ray.start + ray.direction * best.t;
    const Vec3 q = closestOnTriangle(p, a, b, c);
    const Vec3 d = p - q;
    const float dd = math::dot(d, d);
    Vec3 normal;
    if (1e-10f <= dd) {
        normal = d * (1.0f / std::sqrt(dd));
    } else {
        const Vec3 n0 = math::cross(b - a, c - a);
        normal = n0 * (1.0f / std::sqrt(math::dot(n0, n0)));
    }
    record(result, best.t, q, normal);
    result.island = best.island;
    result.facet = best.facet;
    return true;
}

// ---- Ray casts ----

namespace {

Vec3 normalized(const Vec3& v) { return v * (1.0f / std::sqrt(math::dot(v, v))); }

// The ray in the shape's frame (rotation without scale; the library sums Rt s + Rt (-T)).
void rayLocal(const Pose& pose, const CastRay& ray, Vec3& start, Vec3& dir) { toLocal(pose, ray, start, dir); }

void recordRay(CastResult& result, const CastRay& ray, float t, const Vec3& normal) {
    record(result, t, ray.start + ray.direction * t, normal);
}

}   // namespace

bool rayCastSphere(float radius, const Pose& pose, const CastRay& ray, CastResult& result) {
    const Vec3 m = ray.start - pose.translation;
    const float c = math::dot(m, m) - radius * radius;
    if (c < 0) {
        return false;   // starts inside
    }
    const float a = math::dot(ray.direction, ray.direction);
    if (!(kTiny <= std::fabs(a))) {
        return false;
    }
    const float b = math::dot(m, ray.direction);
    const float disc = b * b - a * c;
    if (!(0 <= disc)) {
        return false;
    }
    const float t = (-b - std::sqrt(disc)) / a;
    if (!(0 <= t && t <= 1.0f) || result.t <= t) {
        return false;
    }
    const Vec3 n = ray.start + ray.direction * t - pose.translation;
    const float nn = math::dot(n, n);
    recordRay(result, ray, t, kTiny < nn ? n * (1.0f / std::sqrt(nn)) : ray.direction * (1.0f / -a));
    return true;
}

bool rayCastBox(const Vec3& half, const Pose& pose, const CastRay& ray, CastResult& result) {
    Vec3 s, d;
    rayLocal(pose, ray, s, d);
    if (-half.x < s.x && s.x < half.x && -half.y < s.y && s.y < half.y && -half.z < s.z && s.z < half.z) {
        return false;   // starts inside
    }
    float tNear[3], tFar[3];
    for (int i = 0; i < 3; ++i) {
        const float h = axis(half, i), si = axis(s, i), di = axis(d, i);
        if (std::fabs(di) < kTiny) {
            if (h < si || si < -h) {
                return false;
            }
            tNear[i] = -1e38f, tFar[i] = 1e38f;
            continue;
        }
        const float r0 = 1.0f / di;
        const float r = (r0 + r0) - r0 * di * r0;
        const float x1 = (-h - si) * r, x2 = (h - si) * r;
        const float t1 = (x1 + x1) - x1 * di * r, t2 = (x2 + x2) - x2 * di * r;
        tNear[i] = std::min(t1, t2), tFar[i] = std::max(t1, t2);
    }
    const float enter = std::max(std::max(tNear[0], tNear[1]), tNear[2]);
    const float exit = std::min(std::min(tFar[0], tFar[1]), tFar[2]);
    if (exit < enter) {
        return false;
    }
    // The entry axis: y when x <= y and z < y, x when y < x and z < x, else z.
    int k = 2;
    if (tNear[0] <= tNear[1]) {
        if (tNear[2] < tNear[1]) {
            k = 1;
        }
    } else if (tNear[2] < tNear[0]) {
        k = 0;
    }
    const float t = tNear[k];
    if (!(0 < t) || !(t < result.t)) {
        return false;
    }
    Vec3 n;
    const float sign = axis(d, k) < 0 ? 1.0f : -1.0f;
    (k == 0 ? n.x : k == 1 ? n.y : n.z) = sign;
    recordRay(result, ray, t, apply(pose.rotation, n));
    return true;
}

namespace {

// The side of a cylinder of `radius` around the x axis (local frame): t of the first crossing of
// the infinite side, if the quadratic has roots; `disc` < 0: no crossing at all.
bool sideQuadratic(float radius, const Vec3& s, const Vec3& d, float& a, float& t, float& disc) {
    const Vec3 s2{0, s.y, s.z}, d2{0, d.y, d.z};
    a = math::dot(d2, d2);
    const float b = math::dot(d2, s2);
    disc = b * b - a * (math::dot(s2, s2) - radius * radius);
    if (disc < 0) {
        return false;
    }
    t = 0;
    if (!(kTiny <= std::fabs(a))) {
        return false;
    }
    t = (-b - std::sqrt(disc)) / a;
    return true;
}

}   // namespace

bool rayCastCapsule(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result) {
    Vec3 s, d;
    rayLocal(pose, ray, s, d);
    const Vec3 onAxis{std::max(-halfLength, std::min(s.x, halfLength)), 0, 0};
    if (!(radius * radius < math::dot(s - onAxis, s - onAxis))) {
        return false;   // starts inside
    }
    float a = 0, t = 0, disc = 0;
    const bool side = sideQuadratic(radius, s, d, a, t, disc);
    if (disc < 0) {
        return false;
    }
    if (side && 0 <= t && t <= 1.0f && t < result.t) {
        const Vec3 p = s + d * t;
        if (std::fabs(p.x) <= halfLength) {
            recordRay(result, ray, t, apply(pose.rotation, normalized(Vec3{0, p.y, p.z})));
            return true;
        }
    }
    // The end spheres.
    const float dd = math::dot(d, d);
    const auto sphere = [&](const Vec3& centre, float& out) {
        const Vec3 m = s - centre;
        const float b = math::dot(d, m);
        const float q = b * b - dd * (math::dot(m, m) - radius * radius);
        if (!(0 <= q)) {
            return false;
        }
        out = (-b - std::sqrt(q)) / dd;
        return true;
    };
    const Vec3 ends[2] = {{halfLength, 0, 0}, {-halfLength, 0, 0}};
    float best = 999.0f, t0 = 0, t1 = 0;
    if (sphere(ends[0], t0) && 0 <= t0 && t0 <= 1.0f) {
        best = std::min(t0, best);
    }
    int end = 0;
    if (sphere(ends[1], t1)) {
        if (0 <= t1 && t1 <= 1.0f) {
            end = t1 < best ? 1 : 0;
            best = std::min(t1, best);
        }
    }
    if (!(best < result.t)) {
        return false;
    }
    const Vec3 p = s + d * best;
    recordRay(result, ray, best, apply(pose.rotation, normalized(p - ends[end])));
    return true;
}

bool rayCastCylinder(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result) {
    Vec3 s, d;
    rayLocal(pose, ray, s, d);
    if (-halfLength <= s.x && s.x <= halfLength && s.y * s.y + s.z * s.z <= radius * radius) {
        return false;   // starts inside
    }
    float a = 0, t = 0, disc = 0;
    const bool side = sideQuadratic(radius, s, d, a, t, disc);
    if (disc < 0) {
        return false;
    }
    if (side && 0 <= t && t <= 1.0f && t < result.t) {
        const Vec3 p = s + d * t;
        if (std::fabs(p.x) <= halfLength) {
            const Vec3 q{0, p.y, p.z};
            const Vec3 n = 0 < math::dot(q, q) ? apply(pose.rotation, normalized(q)) : normalized(-ray.direction);
            recordRay(result, ray, t, n);
            return true;
        }
    }
    // The caps: the nearer plane crossing.
    if (std::fabs(d.x) < kTiny) {
        return false;
    }
    const float tc = std::min((halfLength - s.x) / d.x, (-halfLength - s.x) / d.x);
    if (tc < 0 || 1.0f < tc) {
        return false;
    }
    const Vec3 p = s + d * tc;
    if (radius * radius <= p.y * p.y + p.z * p.z || !(tc < result.t)) {
        return false;
    }
    recordRay(result, ray, tc, apply(pose.rotation, Vec3{0 < p.x ? 1.0f : -1.0f, 0, 0}));
    return true;
}

bool rayCastHull(const HullGeometry& hull, const Pose& pose, const CastRay& ray, CastResult& result) {
    if (hull.indexCount < 3) {
        return false;
    }
    const Mat3 inv = inverse(pose.rotation);
    const Vec3 s = apply(inv, ray.start - pose.translation);
    const Vec3 d = apply(inv, ray.direction);
    const Vec3 back = -d;
    int found = -1;
    float best = result.t;
    for (int f = 0; f + 2 < hull.indexCount; f += 3) {
        const Vec3 a = hull.points[hull.indices[f]], b = hull.points[hull.indices[f + 1]],
                   c = hull.points[hull.indices[f + 2]];
        const Vec3 e1 = b - a, e2 = c - a;
        const Vec3 n = math::cross(e1, e2);
        const float denom = math::dot(n, back);
        if (!(0 < denom)) {
            continue;   // back facing
        }
        const Vec3 m = s - a;
        const float t = math::dot(m, n) / denom;
        if (!(0 <= t && t <= 1.0f)) {
            continue;
        }
        const Vec3 e = math::cross(back, m);
        const float v = math::dot(e, e2) / denom;
        if (!(-1e-5f <= v && v <= 1.00001f)) {
            continue;
        }
        const float w = -math::dot(e, e1) / denom;
        if (-1e-5f <= w && v + w <= 1.00001f && t < best) {
            best = t;
            found = f;
        }
    }
    if (found < 0) {
        return false;
    }
    const Vec3 a = hull.points[hull.indices[found]], b = hull.points[hull.indices[found + 1]],
               c = hull.points[hull.indices[found + 2]];
    const Vec3 n = math::cross(b - a, c - b);
    recordRay(result, ray, best, normalized(apply(transpose(inv), n)));
    return true;
}

bool rayCastMesh(const MeshPlacement& placement, const CastRay& ray, CastResult& result) {
    const data::LargeMesh& mesh = *placement.mesh;
    const Mat3& m = placement.pose.rotation;
    const Mat3 inv = inverse(m);
    const Vec3 start = apply(inv, ray.start) + apply(inv, -placement.pose.translation);
    const Vec3 dir = apply(inv, ray.direction);
    const Vec3 back = -ray.direction;
    float best = result.t;
    int bestIsland = -1, bestFacet = -1;
    Vec3 bestNormal;
    const auto keep = [&](const meshtree::Box& box) {
        float entry = 0;
        return rayEntersBox(box, 0, start, dir, entry) && entry < best;
    };
    const meshtree::Box root{mesh.centre - mesh.halfExtent, mesh.centre + mesh.halfExtent};
    meshtree::walk(mesh.tree, root, keep, [&](const unsigned* islands, int count) {
        for (int k = 0; k < count; ++k) {
            const data::MeshIsland& island = mesh.islands[islands[k]];
            const meshtree::Box box{island.min, island.max};
            if (!keep(box)) {
                continue;
            }
            meshtree::walk(island.tree, box, keep, [&](const unsigned* facets, int n) {
                for (int j = 0; j < n; ++j) {
                    const meshtree::Corners cs = meshtree::corners(island.facets[facets[j]], placement.mirrored);
                    const auto world = [&](int v) {
                        return apply(m, island.vertices[std::size_t(v)]) + placement.pose.translation;
                    };
                    const Vec3 a = world(cs.v[0]), b = world(cs.v[1]), c = world(cs.v[2]);
                    const Vec3 e1 = b - a, e2 = c - a;
                    const Vec3 nrm = math::cross(e1, e2);
                    const float denom = math::dot(nrm, back);
                    if (!(0 < denom)) {
                        continue;   // back facing
                    }
                    const Vec3 q = ray.start - a;
                    const float t = math::dot(q, nrm) / denom;
                    if (!(0 <= t && t <= 1.0f)) {
                        continue;
                    }
                    const Vec3 e = math::cross(back, q);
                    const float v = math::dot(e2, e) / denom;
                    if (!(-1e-5f <= v && v <= 1.00001f)) {
                        continue;
                    }
                    const float w = -math::dot(e1, e) / denom;
                    if (-1e-5f <= w && v + w <= 1.00001f && t < best) {
                        best = t;
                        bestIsland = int(islands[k]);
                        bestFacet = int(facets[j]);
                        bestNormal = nrm;
                    }
                }
            });
        }
    });
    if (bestFacet < 0) {
        return false;
    }
    record(result, best, ray.start + ray.direction * best, normalized(bestNormal));
    result.island = bestIsland;
    result.facet = bestFacet;
    return true;
}

// ---- Capsule casts ----

namespace {

// Moving into the shape: the capsule's touching point (in its frame, at its start) gives the
// normal and the point (the tail shared by the convex cast entries).
bool capsuleMovingHit(const ConvexCast& cast, const CapsuleRay& ray, CastResult& result) {
    if (cast.status != 0 || !(cast.t < result.t)) {
        return false;
    }
    const Vec3 p = cast.pointB;
    const float h = ray.halfLength;
    Vec3 normal{0, 0, -1};
    if (1e-5f <= std::fabs(p.y) || 1e-5f <= std::fabs(p.z)) {
        Vec3 n{0, 0, 1};
        if (std::fabs(p.x) <= h) {
            n = normalized(Vec3{0, p.y, p.z});
        } else if (h < p.x) {
            n = normalized(p - Vec3{h, 0, 0});
        } else if (p.x < h) {
            n = normalized(p + Vec3{h, 0, 0});
        }
        normal = -apply(ray.rotation, n);
    }
    record(result, cast.t, ray.start + ray.direction * cast.t + apply(ray.rotation, p), normal);
    return true;
}

// The capsule's axis from A to B.
void capsuleAxis(const CapsuleRay& ray, Vec3& a, Vec3& b) {
    a = ray.start + apply(ray.rotation, Vec3{-ray.halfLength, 0, 0});
    b = ray.start + apply(ray.rotation, Vec3{ray.halfLength, 0, 0});
}

// Overlapping at the start, the box / capsule / cylinder tail: c = the axis point nearest p (the
// clamped parameter), normal from p towards c ((0, 0, 1) when within 1e-10 squared), the point p
// or, outside the capsule, its surface towards p.
void capsuleTouch(const Vec3& p, const CapsuleRay& ray, CastResult& result) {
    Vec3 a, b;
    capsuleAxis(ray, a, b);
    const Vec3 e = b - a;
    const float s = std::max(0.0f, std::min(math::dot(p - a, e) / math::dot(e, e), 1.0f));
    const Vec3 c = a + e * s;
    const Vec3 d = c - p;
    const float dd = math::dot(d, d);
    const Vec3 normal = 1e-10f < dd ? d * (1.0f / std::sqrt(dd)) : Vec3{0, 0, 1};
    record(result, 0, ray.radius * ray.radius < dd ? c - normal * ray.radius : p, normal);
}

// 0xe23c40 (sphere and hull): the axis point c nearest p by regions; normal from c towards p (the
// capsule's turned z axis when within 1e-10 squared); p moves to the capsule's surface when
// outside it. The callers negate the normal.
void axisNearest(const CapsuleRay& ray, Vec3& p, Vec3& normal) {
    Vec3 a, b;
    capsuleAxis(ray, a, b);
    const Vec3 e = b - a;
    Vec3 c;
    if (math::dot(e, p - b) <= 0) {
        if (math::dot(p - a, -e) <= 0) {
            const Vec3 u = normalized(e);
            c = a + u * math::dot(p - a, u);
        } else {
            c = a;
        }
    } else {
        c = b;
    }
    const Vec3 d = p - c;
    const float dd = math::dot(d, d);
    normal = 1e-10f <= dd ? d * (1.0f / std::sqrt(dd)) : apply(ray.rotation, Vec3{0, 0, 1});
    if (ray.radius * ray.radius < dd) {
        p = c + normal * ray.radius;
    }
}

ConvexCast capsuleConvexCast(const SupportShape& shape, const Pose& pose, const CapsuleRay& ray) {
    const CapsuleSupport capsule(ray.halfLength, ray.radius);
    return castConvex(shape, pose, capsule, Pose{ray.rotation, ray.start}, ray.direction);
}

}   // namespace

bool capsuleCastSphere(float radius, const Pose& pose, const CapsuleRay& ray, CastResult& result) {
    const ConvexCast cast = capsuleConvexCast(SphereSupport(radius), pose, ray);
    if (cast.status == 1) {
        const Vec3 centre = pose.translation;
        const Vec3 d = ray.start - centre;
        const float dd = math::dot(d, d);
        const Vec3 n = 1e-10f < dd ? normalized(d) : Vec3{0, 0, 1};
        Vec3 p = centre + n * radius;
        Vec3 normal = apply(ray.rotation, Vec3{0, 0, 1});
        const Vec3 q = p - ray.start;
        if (1e-10f < math::dot(q, q)) {
            axisNearest(ray, p, normal);
            normal = -normal;
        }
        record(result, 0, p, normal);
        return true;
    }
    return capsuleMovingHit(cast, ray, result);
}

bool capsuleCastBox(const Vec3& half, const Pose& pose, const CapsuleRay& ray, CastResult& result) {
    const ConvexCast cast = capsuleConvexCast(BoxSupport(half), pose, ray);
    if (cast.status == 1) {
        const Mat3 inv = inverse(pose.rotation);
        const Vec3 local = apply(inv, ray.start - pose.translation);
        Vec3 q;
        const bool inside = -half.x < local.x && local.x < half.x && -half.y < local.y && local.y < half.y &&
                            -half.z < local.z && local.z < half.z;
        if (!inside) {
            q = {std::min(std::max(local.x, -half.x), half.x), std::min(std::max(local.y, -half.y), half.y),
                 std::min(std::max(local.z, -half.z), half.z)};
        } else {
            // Inside: out through the nearest face.
            const Vec3 sign{local.x < 0 ? -1.0f : 1.0f, local.y < 0 ? -1.0f : 1.0f, local.z < 0 ? -1.0f : 1.0f};
            const Vec3 gap{half.x - local.x * sign.x, half.y - local.y * sign.y, half.z - local.z * sign.z};
            const float least = std::min(gap.x, std::min(gap.y, gap.z));
            q = local;
            if (gap.x <= least) {
                q.x += gap.x * sign.x;
            } else if (gap.y <= least) {
                q.y += gap.y * sign.y;
            } else {
                q.z += gap.z * sign.z;
            }
        }
        capsuleTouch(apply(pose.rotation, q) + pose.translation, ray, result);
        return true;
    }
    return capsuleMovingHit(cast, ray, result);
}

bool capsuleCastCapsule(float halfLength, float radius, const Pose& pose, const CapsuleRay& ray, CastResult& result) {
    const ConvexCast cast = capsuleConvexCast(CapsuleSupport(halfLength, radius), pose, ray);
    if (cast.status == 1) {
        const Vec3 a = pose.translation + apply(pose.rotation, Vec3{-halfLength, 0, 0});
        const Vec3 b = pose.translation + apply(pose.rotation, Vec3{halfLength, 0, 0});
        const Vec3 e = b - a;
        const float s = std::max(0.0f, std::min(math::dot(ray.start - a, e) / math::dot(e, e), 1.0f));
        const Vec3 c = a + e * s;
        const Vec3 d = ray.start - c;
        const float dd = math::dot(d, d);
        const Vec3 n = 1e-10f < dd ? normalized(d) : normalized(apply(pose.rotation, Vec3{0, 0, 1}));
        capsuleTouch(c + n * radius, ray, result);
        return true;
    }
    return capsuleMovingHit(cast, ray, result);
}

bool capsuleCastCylinder(float halfLength, float radius, const Pose& pose, const CapsuleRay& ray, CastResult& result) {
    const ConvexCast cast = capsuleConvexCast(CylinderSupport(halfLength, radius), pose, ray);
    if (cast.status == 1) {
        const Vec3 a = pose.translation + apply(pose.rotation, Vec3{-halfLength, 0, 0});
        const Vec3 b = pose.translation + apply(pose.rotation, Vec3{halfLength, 0, 0});
        Vec3 p = ray.start, n;
        cylinderSurface(radius, ray.start, a, b, p, n);
        capsuleTouch(p, ray, result);
        return true;
    }
    return capsuleMovingHit(cast, ray, result);
}

bool capsuleCastHull(const HullGeometry& hull, const Pose& pose, const CapsuleRay& ray, CastResult& result) {
    const ConvexCast cast = capsuleConvexCast(HullSupport(hull.points, hull.pointCount), pose, ray);
    if (cast.status == 1) {
        // The nearest point of the facets the capsule's centre lies in front of.
        float best = 1e38f;
        Vec3 nearest;
        bool found = false;
        const auto corner = [&](int i) { return apply(pose.rotation, hull.points[hull.indices[i]]) + pose.translation; };
        for (int f = 0; f + 2 < hull.indexCount; f += 3) {
            const Vec3 a = corner(f + (hull.mirrored ? 2 : 0));
            const Vec3 b = corner(f + 1);
            const Vec3 c = corner(f + (hull.mirrored ? 0 : 2));
            const Vec3 n = math::cross(b - a, c - a);
            if (math::dot(ray.start - a, n * (1.0f / std::sqrt(math::dot(n, n)))) < 0) {
                continue;
            }
            const Vec3 q = closestOnTriangle(ray.start, a, b, c);
            const Vec3 d = q - ray.start;
            const float dd = math::dot(d, d);
            if (dd <= best) {
                best = dd;
                nearest = q;
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        Vec3 normal = apply(ray.rotation, Vec3{0, 0, 1});
        if (1e-10f < best) {
            axisNearest(ray, nearest, normal);
            normal = -normal;
        }
        record(result, 0, nearest, normal);
        return true;
    }
    return capsuleMovingHit(cast, ray, result);
}

bool capsuleCastMesh(const MeshPlacement& placement, const CapsuleRay& ray, CastResult& result) {
    // The tree boxes grow by the capsule's extent: its half axis split along and across the movement.
    const Vec3 axisHalf = apply(ray.rotation, Vec3{ray.halfLength, 0, 0});
    const float dd = math::dot(ray.direction, ray.direction);
    const Vec3 along = ray.direction * (math::dot(ray.direction, axisHalf) / dd);
    const Vec3 across = axisHalf - along;
    const auto length = [](const Vec3& v) {
        const float l2 = math::dot(v, v);
        return 1e-10f < l2 ? std::sqrt(l2) : 0.0f;
    };
    const float extent = std::max(length(across), length(along)) + ray.radius;
    const float limit = (ray.radius + ray.halfLength) * (ray.radius + ray.halfLength);
    const MeshBest best = walkMesh(
        placement, ray.start, ray.direction, extent, result.t, true,
        [&](const Vec3& a, const Vec3& b, const Vec3& c, float& t, float& d2) {
            // 0xe2bdc0: the convex cast of the capsule against the triangle (about its centroid).
            const Vec3 centroid = (a + b + c) * (1.0f / 3.0f);
            const TriangleSupport triangle(a - centroid, b - centroid, c - centroid);
            const ConvexCast cast = capsuleConvexCast(triangle, Pose{Mat3{}, centroid}, ray);
            d2 = limit;
            if (cast.status == 1) {
                const Vec3 q = closestOnTriangle(ray.start, a, b, c) - ray.start;
                d2 = math::dot(q, q);
                t = 0;
                return true;
            }
            if (cast.status != 0) {
                return false;
            }
            t = cast.t;
            return true;
        });
    if (!best.hit) {
        return false;
    }
    Vec3 a, b, c;
    bestCorners(placement, best, a, b, c);
    const Vec3 p = ray.start + ray.direction * best.t;
    const Vec3 q = closestOnTriangle(p, a, b, c);
    const Vec3 d = p - q;
    record(result, best.t, q, d * (1.0f / std::sqrt(math::dot(d, d))));
    result.island = best.island;
    result.facet = best.facet;
    return true;
}

}   // namespace beacon::physics::rigid
