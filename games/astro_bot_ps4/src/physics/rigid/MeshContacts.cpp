#include "physics/rigid/MeshContacts.hpp"

#include "math/Vector.hpp"
#include "physics/rigid/MeshTree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace beacon::physics::rigid {

using math::Vec3;

namespace {

using meshtree::apart;
using meshtree::Box;
using meshtree::Corners;
using meshtree::corners;

// A tree walk filter keeping the nodes whose box touches `query`.
auto touching(const Box& query) {
    return [query](const Box& b) { return !apart(b, query); };
}

constexpr float kOnEdge = 1e-4f;            // barycentric weight counted as zero
constexpr float kAtCorner = 0.9999f;        // barycentric weight counted as one
constexpr double kOnFarEdge = 0.9999000000025262;   // weight sum, compared in double precision

Vec3 absVec(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }

Vec3 column(const Mat3& m, int c) {
    return c == 0 ? Vec3{m.r[0].x, m.r[1].x, m.r[2].x}
                  : c == 1 ? Vec3{m.r[0].y, m.r[1].y, m.r[2].y} : Vec3{m.r[0].z, m.r[1].z, m.r[2].z};
}

float safeLength(const Vec3& v) {
    const float l2 = math::dot(v, v);
    return l2 > 1.17549435e-38f ? std::sqrt(l2) : 0.0f;
}

Vec3 unit(const Vec3& v) { return v * (1.0f / std::sqrt(math::dot(v, v))); }

Vec3 transformPoint(const Pose& p, const Vec3& v) { return apply(p.rotation, v) + p.translation; }

Pose inversePose(const Pose& p) {
    Pose inv;
    inv.rotation = inverse(p.rotation);
    inv.translation = apply(inv.rotation, p.translation) * -1.0f;
    return inv;
}

Pose compose(const Pose& a, const Pose& b) {   // a * b
    return {multiply(a.rotation, b.rotation), apply(a.rotation, b.translation) + a.translation};
}

// Box of a cylinder (x axis) placed by p.
Box cylinderBox(const Pose& p, float halfLength, float radius) {
    const Vec3 half = absVec(column(p.rotation, 0)) * halfLength + absVec(column(p.rotation, 1)) * radius +
                      absVec(column(p.rotation, 2)) * radius;
    return {p.translation - half, p.translation + half};
}

std::uint16_t quantizeWeight(float w) {
    const float c = std::min(w, 1.0f);
    return c < 0 ? 0 : std::uint16_t(int(c * 65535.0f));
}

// The primitive a mesh is tested against.
struct Primitive {
    enum Kind { Sphere, Box, Capsule, Cylinder, Hull } kind = Sphere;
    float halfLength = 0, radius = 0;   // capsule, cylinder (x axis); sphere radius
    Vec3 half;                          // box
    const math::Vec3* hull = nullptr;   // hull points (hull primitive)
    int hullCount = 0;
};

// A hull's box half extents (hull creation 0xde67e0: the largest |coordinate| per axis).
Vec3 hullHalf(const Primitive& p) {
    Vec3 h;
    for (int i = 0; i < p.hullCount; ++i) {
        h = {std::max(h.x, std::fabs(p.hull[i].x)), std::max(h.y, std::fabs(p.hull[i].y)),
             std::max(h.z, std::fabs(p.hull[i].z))};
    }
    return h;
}

// The primitive's box placed by pose (0xe8bc80: |rotation| times the half extents; a capsule's
// along its axis are its half length plus radius).
Box primitiveBox(const Primitive& p, const Pose& pose) {
    if (p.kind == Primitive::Hull || p.kind == Primitive::Box) {
        const Vec3 h = p.kind == Primitive::Hull ? hullHalf(p) : p.half;
        const Vec3 half = absVec(column(pose.rotation, 0)) * h.x + absVec(column(pose.rotation, 1)) * h.y +
                          absVec(column(pose.rotation, 2)) * h.z;
        return {pose.translation - half, pose.translation + half};
    }
    if (p.kind == Primitive::Cylinder) {
        return cylinderBox(pose, p.halfLength, p.radius);
    }
    if (p.kind == Primitive::Capsule) {
        return cylinderBox(pose, p.halfLength + p.radius, p.radius);
    }
    const Vec3 half = (absVec(column(pose.rotation, 0)) + absVec(column(pose.rotation, 1)) +
                       absVec(column(pose.rotation, 2))) *
                      p.radius;
    return {pose.translation - half, pose.translation + half};
}

// Edge tilt in radians: the sphere island divides by 255 and scales by pi/2, the box and capsule
// islands scale by one step (pi/2/255).
float edgeTilt(const data::MeshEdge& e) { return float(e.raw[3]) / 255.0f * 1.57079637f; }
float edgeTiltStep(const data::MeshEdge& e) { return float(e.raw[3]) * 0.00615998590f; }

// Island against a hull (0xe9d800): facets stay in the mesh frame (no mirrored corner order), each
// tested by 0xe9c130 with the hull at its mesh-frame pose; sub-shape data as for the others.
void hullIslandContacts(const data::MeshIsland& island, const Primitive& prim, const Pose& rel, ContactCache& out) {
    const Box query = primitiveBox(prim, rel);
    std::vector<unsigned> facets;
    meshtree::walk(island.tree, {island.min, island.max}, touching(query), [&](const unsigned* leaves, int n) {
        facets.insert(facets.end(), leaves, leaves + n);
    });
    if (facets.empty()) {
        return;
    }
    const HullSupport hull(prim.hull, prim.hullCount);
    ContactCache cache;
    for (unsigned fi : facets) {
        const data::MeshFacet& f = island.facets[fi];
        const unsigned flags = island.edges[f.edge[0]].angleType() | island.edges[f.edge[1]].angleType() << 2 |
                               island.edges[f.edge[2]].angleType() << 4;
        hullFacetContact(hull, rel, int(fi), f.normal, island.vertices[f.vertex[0]], island.vertices[f.vertex[1]],
                         island.vertices[f.vertex[2]], f.thickness, flags, cache);
    }
    for (int i = 0; i < cache.size(); ++i) {
        const CachedContact& e = cache[i];
        const unsigned fi = unsigned(e.subA >> 8) & 0xff;
        const data::MeshFacet& f = island.facets[fi];
        const Vec3 a = island.vertices[f.vertex[0]], b = island.vertices[f.vertex[1]];
        const Vec3 d = island.vertices[f.vertex[2]];
        const Vec3 p = e.pointA;
        const Vec3 nrm = math::cross(b - a, d - a);
        const float area = math::dot(nrm, nrm);
        const Vec3 k = math::cross(a - p, nrm);
        const float s = math::dot(d - p, k) / area;
        const float t = math::dot(p - b, k) / area;
        const std::uint64_t subA = (e.subA & ~std::uint64_t(0xff0f)) | (std::uint64_t(fi) << 8) | 2;
        const std::uint64_t subB =
            std::uint64_t(f.user) << 32 | std::uint64_t(quantizeWeight(t)) << 16 | quantizeWeight(s);
        out.add(e.distance, e.normal, e.pointA, e.pointB, subA, subB);
    }
}

// Island against a primitive (0xea1990 cylinder, 0xea6940 sphere). rel: the primitive in the
// mesh frame.
void primitiveIslandContacts(const data::MeshIsland& island, bool mirrored, const Primitive& prim, const Pose& rel,
                             ContactCache& out) {
    const Pose toCylinder = inversePose(rel);
    const Mat3 normalToCylinder = transpose(rel.rotation);   // inverse-transpose of toCylinder
    const Box query = primitiveBox(prim, rel);
    std::vector<unsigned> facets;
    meshtree::walk(island.tree, {island.min, island.max}, touching(query), [&](const unsigned* leaves, int n) {
        facets.insert(facets.end(), leaves, leaves + n);
    });
    if (facets.empty()) {
        return;
    }
    ContactCache cache;
    for (unsigned fi : facets) {
        const data::MeshFacet& f = island.facets[fi];
        const Corners c = corners(f, mirrored);
        const Vec3 n = unit(apply(normalToCylinder, f.normal));
        const Vec3 v0 = transformPoint(toCylinder, island.vertices[std::size_t(c.v[0])]);
        const Vec3 v1 = transformPoint(toCylinder, island.vertices[std::size_t(c.v[1])]);
        const Vec3 v2 = transformPoint(toCylinder, island.vertices[std::size_t(c.v[2])]);
        const unsigned flags = island.edges[std::size_t(c.e[0])].angleType() |
                               island.edges[std::size_t(c.e[1])].angleType() << 2 |
                               island.edges[std::size_t(c.e[2])].angleType() << 4;
        const data::MeshEdge* e[3] = {&island.edges[std::size_t(c.e[0])], &island.edges[std::size_t(c.e[1])],
                                      &island.edges[std::size_t(c.e[2])]};
        if (prim.kind == Primitive::Sphere) {
            const float tilts[3] = {edgeTilt(*e[0]), edgeTilt(*e[1]), edgeTilt(*e[2])};
            sphereFacetContact(prim.radius, int(fi), n, v0, v1, v2, f.thickness, tilts, flags, cache);
        } else if (prim.kind == Primitive::Box) {
            const float tilts[3] = {edgeTiltStep(*e[0]), edgeTiltStep(*e[1]), edgeTiltStep(*e[2])};
            boxFacetContact(prim.half, int(fi), n, v0, v1, v2, f.thickness, tilts, flags, cache);
        } else if (prim.kind == Primitive::Capsule) {
            const float tilts[3] = {edgeTiltStep(*e[0]), edgeTiltStep(*e[1]), edgeTiltStep(*e[2])};
            capsuleFacetContact(prim.halfLength, prim.radius, int(fi), n, v0, v1, v2, f.thickness, tilts, flags,
                                cache);
        } else {
            cylinderFacetContact(prim.halfLength, prim.radius, int(fi), n, v0, v1, v2, f.thickness, flags, cache);
        }
    }
    // Back to the mesh frame, with the facet and where on it (barycentric weights of corners 1
    // and 2 in 1/65535; taken from the point still in the cylinder's frame against the mesh-frame
    // corners, as the original does) as sub-shape data.
    const Mat3 normalToMesh = transpose(inverse(rel.rotation));
    for (int i = 0; i < cache.size(); ++i) {
        const CachedContact& e = cache[i];
        const unsigned fi = unsigned(e.subA >> 8) & 0xff;
        const data::MeshFacet& f = island.facets[fi];
        const Corners c = corners(f, mirrored);
        const Vec3 a = island.vertices[std::size_t(c.v[0])], b = island.vertices[std::size_t(c.v[1])];
        const Vec3 d = island.vertices[std::size_t(c.v[2])];
        const Vec3 p = e.pointA;
        const Vec3 nrm = math::cross(b - a, d - a);
        const float area = math::dot(nrm, nrm);
        const Vec3 k = math::cross(a - p, nrm);
        const float s = math::dot(d - p, k) / area;
        const float t = math::dot(p - b, k) / area;
        const std::uint64_t subA = (e.subA & ~std::uint64_t(0xff0f)) | (std::uint64_t(fi) << 8) | 2;
        const std::uint64_t subB =
            std::uint64_t(f.user) << 32 | std::uint64_t(quantizeWeight(t)) << 16 | quantizeWeight(s);
        out.add(e.distance, apply(normalToMesh, e.normal), transformPoint(rel, p), e.pointB, subA, subB);
    }
}

}   // namespace

void hullFacetContact(const SupportShape& hull, const Pose& pose, int facet, const Vec3& normal, const Vec3& v0,
                      const Vec3& v1, const Vec3& v2, float thickness, unsigned edgeFlags, ContactCache& out) {
    const Vec3 top = apply(pose.rotation, hull.support(apply(transpose(pose.rotation), normal))) + pose.translation;
    if (math::dot(normal, v0) > math::dot(normal, top) + thickness) {
        return;
    }
    const float meanEdge = (safeLength(v0 - v1) + safeLength(v1 - v2) + safeLength(v2 - v0)) / 3.0f;
    const Vec3 apex = (v0 + v1 + v2) * (1.0f / 3.0f) - normal * meanEdge;
    const Vec3 centre = (v1 + v0 + v2 + apex) * 0.25f;
    const Vec3 points[4] = {v0 - centre, v1 - centre, v2 - centre, apex - centre};
    const TetraSupport tetra(points);
    // The tetrahedron is A here (the cylinder routine has the primitive as A).
    const auto c = penetrationSingle(tetra, Pose{Mat3{}, centre}, hull, pose);
    if (!c || c->distance >= 0) {
        return;
    }
    const Vec3 onFacet = centre + c->pointA;
    Vec3 n = c->normal;
    const Vec3 down = normal * -1.0f;
    if (math::dot(down, n) < 0) {
        return;
    }
    const Vec3 nrm = math::cross(v1 - v0, v2 - v0);
    const float area = math::dot(nrm, nrm);
    const Vec3 k = math::cross(v0 - onFacet, nrm);
    const float s = math::dot(v2 - onFacet, k) / area;
    const float t = math::dot(onFacet - v1, k) / area;
    unsigned mask;
    bool interior = false;
    if (s < kOnEdge) {
        mask = t < kOnEdge ? 0x33 : t > kAtCorner ? 0x3c : 0x30;
    } else if (t < kOnEdge) {
        mask = s > kAtCorner ? 0x0f : 0x03;
    } else {
        mask = 0x0c;
        interior = true;
    }
    if ((edgeFlags & mask) == 0 && (!interior || double(s + t) > kOnFarEdge)) {
        n = down;
    }
    out.add(c->distance, n, onFacet, c->pointB, std::uint64_t(facet << 8) & 0xffff, 0);
}

void sphereFacetContact(float radius, int facet, const Vec3& normal, const Vec3& v0, const Vec3& v1, const Vec3& v2,
                        float thickness, const float (&tilt)[3], unsigned edgeFlags, ContactCache& out) {
    // Sphere centre at the origin. Height of the centre above the facet's plane.
    const float height = math::dot(v0 * -1.0f, normal);
    if (height >= radius || math::dot(normal * thickness - v0, normal * -1.0f) >= radius) {
        return;
    }
    // Outward edge planes, tilted down by the edges' tilt when a marked edge has one.
    Vec3 m[3] = {unit(math::cross(v1 - v0, normal)), unit(math::cross(v2 - v1, normal)),
                 unit(math::cross(v0 - v2, normal))};
    if ((tilt[0] > 0 && (edgeFlags & 0x01)) || (tilt[1] > 0 && (edgeFlags & 0x04)) ||
        (tilt[2] > 0 && (edgeFlags & 0x10))) {
        for (int i = 0; i < 3; ++i) {
            m[i] = m[i] * std::cos(tilt[i]) - normal * std::sin(tilt[i]);
        }
    }
    const Vec3 corner[3] = {v0, v1, v2};
    for (int i = 0; i < 3; ++i) {
        if (math::dot(corner[i] * -1.0f, m[i]) >= radius) {
            return;
        }
    }
    // Point of the facet closest to the centre (Voronoi regions; inside the face: flagged).
    const Vec3 ab = v1 - v0, ac = v2 - v0;
    Vec3 c;
    bool face = false;
    {
        const Vec3 ap = v0 * -1.0f;
        const float d1 = math::dot(ab, ap), d2 = math::dot(ac, ap);
        const Vec3 bp = v1 * -1.0f;
        const float d3 = math::dot(ab, bp), d4 = math::dot(ac, bp);
        const Vec3 cp = v2 * -1.0f;
        const float d5 = math::dot(ab, cp), d6 = math::dot(ac, cp);
        const float vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
        if (d1 <= 0 && d2 <= 0) {
            c = v0;
        } else if (d3 >= 0 && d4 <= d3) {
            c = v1;
        } else if (vc <= 0 && d1 >= 0 && d3 <= 0) {
            c = v0 + ab * (d1 / (d1 - d3));
        } else if (d6 >= 0 && d5 <= d6) {
            c = v2;
        } else if (vb <= 0 && d2 >= 0 && d6 <= 0) {
            c = v0 + ac * (d2 / (d2 - d6));
        } else if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
            c = v1 + (v2 - v1) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        } else {
            const float denom = 1.0f / (va + vb + vc);
            c = v0 + ab * (vb * denom) + ac * (vc * denom);
            face = true;
        }
    }
    const float q = math::dot(c, c);
    if (!face && q >= radius * radius) {
        return;
    }
    const Vec3 down = normal * -1.0f;
    Vec3 dir = face || q < 1e-5f ? down : c * (1.0f / std::sqrt(q));
    const Vec3 deepest = dir * radius;
    // Near an edge or corner whose edges are all flat, the facet's own normal.
    const Vec3 nrm = math::cross(ab, ac);
    const float area = math::dot(nrm, nrm);
    const Vec3 k = math::cross(v0 - c, nrm);
    const float s = math::dot(v2 - c, k) / area;
    const float t = math::dot(c - v1, k) / area;
    unsigned mask;
    bool interior = false;
    if (s < kOnEdge) {
        mask = t < kOnEdge ? 0x33 : t > kAtCorner ? 0x3c : 0x30;
    } else if (t < kOnEdge) {
        mask = s > kAtCorner ? 0x0f : 0x03;
    } else {
        mask = 0x0c;
        interior = true;
    }
    if ((edgeFlags & mask) == 0 && (!interior || double(s + t) > kOnFarEdge)) {
        dir = down;
    }
    out.add(-safeLength(deepest - c), dir, c, deepest, std::uint64_t(facet << 8) & 0xffff, 0);
}

namespace {

// Unit vector as the library's vector code makes it: an estimate of 1/sqrt refined by two Newton
// steps, the second folded into the scaling (the estimate here is the exact value, the hardware's
// is a 12-bit approximation; both end within rounding of each other).
Vec3 libraryUnit(const Vec3& v) {
    const float l2 = math::dot(v, v);
    const float r0 = 1.0f / std::sqrt(l2);
    const float r1 = r0 * 0.5f * (3.0f - r0 * r0 * l2);
    const float t = 3.0f - r1 * l2 * r1;
    return {v.x * r1 * 0.5f * t, v.y * r1 * 0.5f * t, v.z * r1 * 0.5f * t};
}

// Sum of |a_k h_k| (a box's extent along a), summed (x + y) + z.
float boxRadius(const Vec3& a, const Vec3& h) {
    return (std::fabs(a.x * h.x) + std::fabs(a.y * h.y)) + std::fabs(a.z * h.z);
}

// The library's inline sine and cosine (quadrant reduction by two parts of pi/2, then odd and
// even polynomials), as the box and capsule facet routines compute the edge tilts.
void librarySinCos(float x, float& s, float& c) {
    const float q = x * 0.636619747f + std::copysign(0.5f, x);
    const int quadrant = int(q);
    const float k = std::trunc(q);
    const float r = (x - k * 1.57079625f) - k * 7.54978942e-08f;
    const float r2 = r * r;
    const float odd = r + r * r2 * (r2 * (r2 * -1.95072702e-04f + 8.33207555e-03f) + -0.166666523f);
    const float even = r2 * (r2 * (r2 * -1.36022491e-03f + 4.16566953e-02f) + -0.499999017f) + 1.0f;
    s = (quadrant & 1) == 0 ? odd : even;
    if (quadrant & 2) {
        s = -s;
    }
    c = ((quadrant + 1) & 1) == 0 ? odd : even;
    if ((quadrant + 1) & 2) {
        c = -c;
    }
    if (x == 0) {
        s = x;
    }
    if (std::isinf(x)) {
        s = c = std::numeric_limits<float>::quiet_NaN();
    }
}

// Up to four closest feature pairs (facet point, primitive point), kept as the box and capsule
// facet routines keep them: a pair is taken only when no farther than the last one taken (plus
// 1e-5); a pair that repeats a kept one (both points within 1e-5 squared) is dropped when the kept
// one is nearer; when full, the farthest kept pair is replaced.
struct ClosestPairs {
    Vec3 onFacet[4], onPrimitive[4];
    float distance2[4] = {};
    int count = 0;
    float limit = 1e38f;

    void add(const Vec3& pf, const Vec3& pp) {
        const Vec3 d = pf - pp;
        add(pf, pp, math::dot(d, d));
    }

    void add(const Vec3& pf, const Vec3& pp, float d2) {
        if (d2 > limit) {
            return;
        }
        int slot = count;
        if (count > 0) {
            float farthest = -1e38f;
            int farIndex = -1;
            for (int i = 0; i < count; ++i) {
                const Vec3 a = onFacet[i] - pf, b = onPrimitive[i] - pp;
                if (math::dot(a, a) < 1e-5f && math::dot(b, b) < 1e-5f && distance2[i] < d2) {
                    return;
                }
                if (farthest < distance2[i]) {
                    farIndex = i;
                }
                farthest = std::max(distance2[i], farthest);
            }
            if (count >= 4) {
                slot = farIndex;
            }
        }
        if (slot == count) {
            ++count;
        }
        limit = d2 + 1e-5f;
        onFacet[slot] = pf;
        onPrimitive[slot] = pp;
        distance2[slot] = d2;
    }
};

// Closest point of triangle abc to p (Voronoi regions, in the order the library tests them).
Vec3 closestOnTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& p) {
    const Vec3 ab = b - a, ac = c - a;
    const float d1 = math::dot(ab, p - a), d2 = math::dot(ac, p - a);
    if (d1 <= 0 && d2 <= 0) {
        return a;
    }
    const float d3 = math::dot(ab, p - b), d4 = math::dot(ac, p - b);
    if (d3 >= 0 && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        return a + ab * (d1 / (d1 - d3));
    }
    const float d5 = math::dot(ab, p - c), d6 = math::dot(ac, p - c);
    if (d6 >= 0 && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        return a + ac * (d2 / (d2 - d6));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    const float denom = 1.0f / ((vc + va) + vb);
    return (ac * (vc * denom) + a) + ab * (vb * denom);
}

float clamp01(float t) { return std::max(0.0f, std::min(t, 1.0f)); }

// Closest points of segments p + e s and q + f t (s, t in [0, 1]): first t for the lines' closest
// pair (0 when nearly parallel), clamped, then the point on the second for it, then back on the
// first.
void closestOnSegments(const Vec3& p, const Vec3& e, const Vec3& q, const Vec3& f, Vec3& onFirst, Vec3& onSecond) {
    const Vec3 rel = p - q;
    const float a = math::dot(e, e), b = math::dot(e, f), c = math::dot(f, f);
    const float d = math::dot(e, rel), g = math::dot(f, rel);
    const float den = b * b - a * c;
    float s = 0;
    if (1e-5f < den * den) {
        s = (c * d - b * g) / den;
    }
    s = clamp01(s);
    const float t = clamp01((g + b * s) / c);
    onSecond = q + f * t;
    onFirst = p + e * clamp01((b * t - d) / a);
}

const Vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

// The facet as a solid for the box and capsule routines: the triangle on top; the outward side
// planes, tilted down by the edges' tilt when a marked edge has one (then the solid narrows to the
// point where the side planes meet, cut off at the thickness); else straight down by the thickness.
struct FacetSolid {
    Vec3 top[3], bottom[3], side[3];

    FacetSolid(const Vec3& normal, const Vec3& v0, const Vec3& v1, const Vec3& v2, float thickness,
               const float (&tilt)[3], unsigned edgeFlags)
        : top{v0, v1, v2},
          side{libraryUnit(math::cross(v1 - v0, normal)), libraryUnit(math::cross(v2 - v1, normal)),
               libraryUnit(math::cross(v0 - v2, normal))} {
        if ((tilt[0] > 0 && (edgeFlags & 0x01)) || (tilt[1] > 0 && (edgeFlags & 0x04)) ||
            (tilt[2] > 0 && (edgeFlags & 0x10))) {
            for (int i = 0; i < 3; ++i) {
                float s, c;
                librarySinCos(tilt[i], s, c);
                side[i] = side[i] * c - normal * s;
            }
            const Vec3 c12 = math::cross(side[1], side[2]), c20 = math::cross(side[2], side[0]);
            const Vec3 c01 = math::cross(side[0], side[1]);
            const Vec3 apex = (c12 * math::dot(side[0], v0) + c20 * math::dot(side[1], v1) +
                               c01 * math::dot(side[2], v2)) *
                              (1.0f / math::dot(side[0], c12));
            const float depth = math::dot(normal * -1.0f, apex - v0);
            if (thickness < depth) {
                const float s = thickness / depth;
                for (int i = 0; i < 3; ++i) {
                    bottom[i] = top[i] + (apex - top[i]) * s;
                }
            } else {
                bottom[0] = bottom[1] = bottom[2] = apex;
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                bottom[i] = top[i] - normal * thickness;
            }
        }
    }

    void project(const Vec3& axis, float& lo, float& hi) const {
        lo = hi = math::dot(axis, top[0]);
        for (const Vec3* p : {&top[1], &top[2], &bottom[0], &bottom[1], &bottom[2]}) {
            const float d = math::dot(axis, *p);
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
    }
};

// The deepest separating-axis candidate so far. Each tested axis gives the primitive's overlap
// past the facet's range on either side; the larger one wins when deeper than the best (strictly),
// and the axis then points from the facet towards the primitive; "wrong side" records whether that
// direction faces against the facet's back (the routine then gives no contact).
struct AxisSearch {
    enum Kind { None, FacetFace, PrimitiveFeature, EdgePair };
    Vec3 down;
    float best = -1e38f;
    Vec3 axis;
    Kind kind = None;
    bool wrongSide = false;

    // above: facet range minimum minus primitive maximum; below: primitive minimum minus facet
    // maximum. Returns true when separated.
    bool test(const Vec3& a, float above, float below, Kind k) {
        const float facing = math::dot(a, down);
        if (best < above) {
            axis = a;
            kind = k;
            wrongSide = facing < 0;
            best = above;
        }
        if (best < below) {
            axis = a * -1.0f;
            kind = k;
            wrongSide = facing > 0;
            best = below;
        }
        return above > 0 || below > 0;
    }
};

// The kept pairs as contacts: the facet point moved back by shift; near an edge or corner whose
// edges are all flat the facet's own normal instead of the axis (once taken it stays for the
// remaining points); distance minus the distance between the two points.
void emitPairs(ClosestPairs& pairs, const Vec3& shift, const Vec3& axis, const Vec3& normal, const Vec3& v0,
               const Vec3& v1, const Vec3& v2, unsigned edgeFlags, int facet, ContactCache& out) {
    Vec3 n = axis;
    const Vec3 nrm = math::cross(v1 - v0, v2 - v0);
    const float area = math::dot(nrm, nrm);
    for (int i = 0; i < pairs.count; ++i) {
        if (!(pairs.distance2[i] < 1e-5f + pairs.limit)) {
            continue;
        }
        const Vec3 p = pairs.onFacet[i] - shift;
        const Vec3 k = math::cross(v0 - p, nrm);
        const float s = math::dot(v2 - p, k) / area;
        const float t = math::dot(p - v1, k) / area;
        unsigned mask;
        bool interior = false;
        if (s < kOnEdge) {
            mask = t < kOnEdge ? 0x33 : t > kAtCorner ? 0x3c : 0x30;
        } else if (t < kOnEdge) {
            mask = s > kAtCorner ? 0x0f : 0x03;
        } else {
            mask = 0x0c;
            interior = true;
        }
        if ((edgeFlags & mask) == 0 && (!interior || double(s + t) > kOnFarEdge)) {
            n = normal * -1.0f;
        }
        out.add(-safeLength(pairs.onPrimitive[i] - p), n, p, pairs.onPrimitive[i], std::uint64_t(facet << 8) & 0xffff,
                0);
    }
}

}   // namespace

void boxFacetContact(const Vec3& half, int facet, const Vec3& normal, const Vec3& v0, const Vec3& v1, const Vec3& v2,
                     float thickness, const float (&tilt)[3], unsigned edgeFlags, ContactCache& out) {
    const FacetSolid solid(normal, v0, v1, v2, thickness, tilt, edgeFlags);
    // Separating axes (box at the origin): the facet's normal first.
    const float offset = math::dot(normal, v0 * -1.0f);
    const float r = boxRadius(normal, half);
    const float lowest = offset - r, highest = offset + r;
    if (lowest > 0 || highest < -thickness) {
        return;
    }
    AxisSearch search{normal * -1.0f};
    if (search.best < lowest) {
        search.axis = search.down;
        search.best = lowest;
        search.kind = AxisSearch::FacetFace;
    }
    const auto test = [&](const Vec3& a, float radius, AxisSearch::Kind k) {
        float lo, hi;
        solid.project(a, lo, hi);
        return search.test(a, lo - radius, -radius - hi, k);
    };
    if (0 <= highest || thickness <= 0.05f) {
        // The box's axes, then the facet's edges against them.
        for (int k = 0; k < 3; ++k) {
            if (test(kAxes[k], math::component(half, k), AxisSearch::PrimitiveFeature)) {
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            const Vec3 e = libraryUnit(solid.top[(i + 1) % 3] - solid.top[i]);
            for (int k = 0; k < 3; ++k) {
                if (std::fabs(math::dot(e, kAxes[k])) <= kAtCorner) {
                    const Vec3 s = libraryUnit(math::cross(e, kAxes[k]));
                    if (test(s, boxRadius(s, half), AxisSearch::EdgePair)) {
                        return;
                    }
                }
            }
        }
    }
    if (thickness > 0.05f) {
        // Thick facets: the side planes, and their edges against the box's axes (top only).
        for (int i = 0; i < 3; ++i) {
            const Vec3& m = solid.side[i];
            const Vec3 extent{std::fabs(m.x) * half.x, std::fabs(m.y) * half.y, std::fabs(m.z) * half.z};
            if (math::dot(m, solid.top[i] * -1.0f) - ((extent.z + extent.x) + extent.y) > 0) {
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            const Vec3 edge = libraryUnit(math::cross(solid.side[(i + 1) % 3], solid.side[i]));
            for (int k = 0; k < 3; ++k) {
                if (std::fabs(math::dot(edge, kAxes[k])) <= kAtCorner) {
                    const Vec3 s = libraryUnit(math::cross(edge, kAxes[k]));
                    const float radius = boxRadius(s, half);
                    const float p0 = math::dot(s, solid.top[0]), p1 = math::dot(s, solid.top[1]);
                    const float p2 = math::dot(s, solid.top[2]);
                    if (std::max(std::max(p0, p1), p2) < -radius || radius < std::min(std::min(p0, p1), p2)) {
                        return;
                    }
                }
            }
        }
    }
    if (search.wrongSide) {
        return;
    }

    // The box face most facing along the axis (first largest of +x +y +z -x -y -z).
    const Vec3& axis = search.axis;
    int face = 0;
    float sign = 1, most = axis.x;
    const float candidates[6] = {axis.x, axis.y, axis.z, -axis.x, -axis.y, -axis.z};
    for (int i = 1; i < 6; ++i) {
        if (most < candidates[i]) {
            most = candidates[i];
            face = i % 3;
            sign = i < 3 ? 1.0f : -1.0f;
        }
    }
    const int j = (face + 1) % 3, k = (face + 2) % 3;
    const Vec3 ei = kAxes[face] * (sign * math::component(half, face));
    const Vec3 ej = kAxes[j] * (sign * math::component(half, j)), ek = kAxes[k] * (sign * math::component(half, k));
    const Vec3 corner[4] = {ei + ej + ek, ek + ei - ej, ei - ej - ek, ei + ej - ek};

    // The facet moved clear of the box along the axis, then its closest features.
    const Vec3 shift = axis * (std::fabs(search.best) * 1.1f);
    const Vec3 moved[3] = {shift + v0, shift + v1, shift + v2};
    ClosestPairs pairs;
    if (search.kind == AxisSearch::EdgePair) {
        for (int i = 0; i < 3; ++i) {
            const Vec3 e = moved[(i + 1) % 3] - moved[i];
            for (int q = 0; q < 4; ++q) {
                Vec3 onFacet, onBox;
                closestOnSegments(moved[i], e, corner[q], corner[(q + 1) % 4] - corner[q], onFacet, onBox);
                pairs.add(onFacet, onBox);
            }
        }
    } else if (search.kind == AxisSearch::PrimitiveFeature) {
        for (const Vec3& p : moved) {
            const Vec3 onBox{std::min(std::max(p.x, -half.x), half.x), std::min(std::max(p.y, -half.y), half.y),
                             std::min(std::max(p.z, -half.z), half.z)};
            pairs.add(p, onBox);
        }
    } else if (search.kind == AxisSearch::FacetFace) {
        for (const Vec3& c : corner) {
            pairs.add(closestOnTriangle(moved[0], moved[1], moved[2], c), c);
        }
    }
    emitPairs(pairs, shift, axis, normal, v0, v1, v2, edgeFlags, facet, out);
}

void capsuleFacetContact(float halfLength, float radius, int facet, const Vec3& normal, const Vec3& v0, const Vec3& v1,
                         const Vec3& v2, float thickness, const float (&tilt)[3], unsigned edgeFlags,
                         ContactCache& out) {
    const FacetSolid solid(normal, v0, v1, v2, thickness, tilt, edgeFlags);
    const Vec3 ends[2] = {{halfLength, 0, 0}, {-halfLength, 0, 0}};
    const Vec3 along = ends[1] - ends[0];
    const Vec3 edge[3] = {libraryUnit(v1 - v0), libraryUnit(v2 - v1), libraryUnit(v0 - v2)};
    // The facet's normal, measured from the triangle's centre.
    const Vec3 centre = (v2 + v1 + v0) * (1.0f / 3.0f);
    const float h0 = math::dot(normal, ends[0] - centre), h1 = math::dot(normal, ends[1] - centre);
    const float lowest = std::min(h0, h1) - radius, highest = std::max(h0, h1) + radius;
    if (lowest > 0 || highest < -thickness) {
        return;
    }
    AxisSearch search{normal * -1.0f};
    if (search.best < lowest) {
        search.axis = search.down;
        search.best = lowest;
        search.kind = AxisSearch::FacetFace;
    }
    const auto test = [&](const Vec3& a) {
        float lo, hi;
        solid.project(a, lo, hi);
        const float p0 = math::dot(ends[0], a), p1 = math::dot(ends[1], a);
        return search.test(a, lo - (std::max(p0, p1) + radius), (std::min(p0, p1) - radius) - hi,
                           AxisSearch::EdgePair);
    };
    // Axes from a vector that may vanish: tested only when its squared length is at least 1e-5.
    const auto testIfLong = [&](const Vec3& v) { return math::dot(v, v) >= 1e-5f && test(libraryUnit(v)); };
    if (0 <= highest || thickness <= 0.05f) {
        // The capsule's axis against the edges; from each end towards each edge; from the axis
        // towards each corner; from each end to each corner.
        for (const Vec3& e : edge) {
            if (std::fabs(e.x) <= kAtCorner && test(libraryUnit(Vec3{0, -e.z, e.y}))) {
                return;
            }
        }
        for (const Vec3& end : ends) {
            for (int i = 0; i < 3; ++i) {
                if (testIfLong(math::cross(math::cross(end - solid.top[i], edge[i]), edge[i]))) {
                    return;
                }
            }
        }
        for (const Vec3& corner : solid.top) {
            if (testIfLong(math::cross(math::cross(corner - ends[0], along), along))) {
                return;
            }
        }
        for (const Vec3& end : ends) {
            for (const Vec3& corner : solid.top) {
                if (testIfLong(corner - end)) {
                    return;
                }
            }
        }
    }
    if (thickness > 0.05f) {
        // Thick facets: the side planes, the corners between them (mean of two side normals), and
        // the side planes' edges against the capsule's axis (top only).
        const auto beyond = [&](const Vec3& m, const Vec3& at) {
            return std::min(math::dot(m, ends[0] - at), math::dot(m, ends[1] - at)) - radius > 0;
        };
        for (int i = 0; i < 3; ++i) {
            if (beyond(solid.side[i], solid.top[i])) {
                return;
            }
        }
        for (int i = 1; i <= 3; ++i) {
            if (beyond(libraryUnit(solid.side[i % 3] + solid.side[i - 1]), solid.top[i % 3])) {
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            const Vec3 e = libraryUnit(math::cross(solid.side[(i + 1) % 3], solid.side[i]));
            if (std::fabs(e.x) <= kAtCorner) {
                const Vec3 s = libraryUnit(Vec3{0, e.z, -e.y});
                const float p0 = math::dot(s, solid.top[0]), p1 = math::dot(s, solid.top[1]);
                const float p2 = math::dot(s, solid.top[2]);
                const float c0 = math::dot(s, ends[0]), c1 = math::dot(s, ends[1]);
                if (std::max(std::max(p0, p1), p2) < std::min(c0, c1) - radius ||
                    std::max(c0, c1) + radius < std::min(std::min(p0, p1), p2)) {
                    return;
                }
            }
        }
    }
    if (search.wrongSide) {
        return;
    }

    // The facet moved clear along the axis; the capsule's axis against its edges, then each end
    // against the triangle; the capsule point on its surface towards the facet point.
    const Vec3 shift = search.axis * (std::fabs(search.best) * 1.1f);
    const Vec3 moved[3] = {shift + v0, shift + v1, shift + v2};
    ClosestPairs pairs;
    for (int i = 0; i < 3; ++i) {
        Vec3 onAxis, onFacet;
        closestOnSegments(ends[0], along, moved[i], moved[(i + 1) % 3] - moved[i], onAxis, onFacet);
        const Vec3 gap = onFacet - onAxis;
        const float d2 = math::dot(gap, gap);
        if (d2 <= pairs.limit) {
            pairs.add(onFacet, onAxis + libraryUnit(gap) * radius, d2);
        }
    }
    for (const Vec3& end : ends) {
        const Vec3 onFacet = closestOnTriangle(moved[0], moved[1], moved[2], end);
        const Vec3 gap = onFacet - end;
        const float d2 = math::dot(gap, gap);
        if (d2 <= pairs.limit) {
            pairs.add(onFacet, end + libraryUnit(gap) * radius, d2);
        }
    }
    emitPairs(pairs, shift, search.axis, normal, v0, v1, v2, edgeFlags, facet, out);
}

namespace {

// Separation of a sphere at the origin from a facet (0xe7c260), as a search over axes: the facet
// normal (from the facet's centre), the direction to each corner, the perpendicular to each edge
// line; each oriented along the normal, the separation is -r minus the facet's largest
// projection, the largest separation wins (strictly; start -1e38). Results: the separation, the
// contact normal (the axis reversed; for an edge whose two flag bits are clear, -n), the facet
// point (the plane foot, the corner, the edge-line foot) and the sphere point (-axis r).
struct FacetSeparation {
    float distance = -1e38f;
    Vec3 normal, onFacet, onSphere;
};

FacetSeparation sphereFacetSeparation(float r, const Vec3& n, const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                      unsigned edgeFlags) {
    FacetSeparation s;
    const Vec3 v[3] = {v0, v1, v2};
    const Vec3 down = n * -1.0f;
    const Vec3 c = (v1 + v0 + v2) * (-1.0f / 3.0f);
    const float h = math::dot(n, c);
    if (-1e38f < h - r) {
        s.distance = h - r;
        s.normal = down;
        s.onFacet = down * h;
        s.onSphere = down * r;
    }
    const auto separation = [&](const Vec3& axis) {
        const float most = std::max(math::dot(axis, v[2]), std::max(math::dot(axis, v[0]), math::dot(axis, v[1])));
        return -r - most;
    };
    const auto oriented = [&](const Vec3& towards) {
        Vec3 axis = libraryUnit(towards);
        if (math::dot(n, axis) < 0) {
            axis = axis * -1.0f;
        }
        return axis;
    };
    for (const Vec3& corner : v) {
        const Vec3 a = corner * -1.0f;
        if (!(1e-10f <= math::dot(a, a))) {
            continue;
        }
        const Vec3 axis = oriented(a);
        const float sep = separation(axis);
        if (s.distance < sep) {
            s.distance = sep;
            s.normal = axis * -1.0f;
            s.onFacet = corner;
            s.onSphere = s.normal * r;
        }
    }
    for (int i = 0; i < 3; ++i) {
        const Vec3& a = v[i];
        const Vec3 e = v[(i + 1) % 3] - a;
        const Vec3 q = a + e * (math::dot(e, a * -1.0f) / math::dot(e, e));
        const Vec3 toCentre = q * -1.0f;
        if (!(1e-10f <= math::dot(toCentre, toCentre))) {
            continue;
        }
        const Vec3 axis = oriented(toCentre);
        const float sep = separation(axis);
        if (s.distance < sep) {
            s.distance = sep;
            s.normal = (edgeFlags & (3u << (2 * i))) ? axis * -1.0f : down;
            s.onSphere = axis * -r;
            s.onFacet = q;
        }
    }
    return s;
}

// The sphere centre's path (start + delta) against a facet (0xe7d9b0). Facing the path (normal .
// -delta > 0): the crossing of the plane within the segment, then `value` = its distance to the
// triangle (closest point by Voronoi regions). Otherwise: all corners must lie ahead along delta
// within its length, and `value` = the start's height over the plane. False when neither holds.
bool centrePathMeetsFacet(const Vec3& start, const Vec3& delta, const Vec3& a, const Vec3& b, const Vec3& c,
                          float& value) {
    const Vec3 ab = b - a, ac = c - a;
    const Vec3 nrm = math::cross(ab, ac);
    const float facing = math::dot(nrm, delta * -1.0f);
    if (0 < facing) {
        const float t = math::dot(nrm, start - a) / facing;
        if (t < 0 || t > 1.0f) {
            return false;
        }
        const Vec3 p = start + delta * t;
        value = safeLength(p - closestOnTriangle(a, b, c, p));
        return true;
    }
    const float pa = math::dot(a - start, delta), pb = math::dot(b - start, delta), pc = math::dot(c - start, delta);
    if (std::min(std::min(pa, pb), pc) < 0) {
        return false;
    }
    if (safeLength(delta) < std::max(std::max(pa, pb), pc)) {
        return false;
    }
    value = math::dot(nrm, start - a) / safeLength(nrm);
    return true;
}

// One island against the swept sphere (0xe7f990): facets into the sphere's frame (as for the
// other primitives), each with its separation; kept when not deeper than the radius, the contact
// normal not along the facet normal (dot < 0.25) and the facet point within the sweep's bound;
// flag 0x10 (word 0x12, else 2) when the centre path meets the facet within the radius.
void sweptIsland(const data::MeshIsland& island, bool mirrored, float radius, float reach, const Pose& rel,
                 const Vec3& next, const Vec3& mid, const Box& query, std::vector<CachedContact>& out) {
    std::vector<unsigned> facets;
    meshtree::walk(island.tree, {island.min, island.max}, touching(query), [&](const unsigned* leaves, int n) {
        facets.insert(facets.end(), leaves, leaves + n);
    });
    if (facets.empty()) {
        return;
    }
    const Pose toSphere = inversePose(rel);
    const Mat3 normalToSphere = transpose(rel.rotation);
    const Vec3 midS = transformPoint(toSphere, mid);
    const Mat3& m = toSphere.rotation;
    const Vec3 bound{reach * std::fabs(m.r[0].x) + reach * std::fabs(m.r[0].z) + reach * std::fabs(m.r[0].y),
                     reach * std::fabs(m.r[1].x) + reach * std::fabs(m.r[1].z) + reach * std::fabs(m.r[1].y),
                     reach * std::fabs(m.r[2].x) + reach * std::fabs(m.r[2].z) + reach * std::fabs(m.r[2].y)};
    const float bound2 = math::dot(bound, bound);
    const Vec3 start = transformPoint(toSphere, rel.translation);
    const Vec3 delta = transformPoint(toSphere, next) - start;
    for (unsigned fi : facets) {
        const data::MeshFacet& f = island.facets[fi];
        const Corners c = corners(f, mirrored);
        const Vec3 n = libraryUnit(apply(normalToSphere, f.normal));
        const Vec3 v0 = transformPoint(toSphere, island.vertices[std::size_t(c.v[0])]);
        const Vec3 v1 = transformPoint(toSphere, island.vertices[std::size_t(c.v[1])]);
        const Vec3 v2 = transformPoint(toSphere, island.vertices[std::size_t(c.v[2])]);
        const unsigned flags = island.edges[std::size_t(c.e[0])].angleType() |
                               island.edges[std::size_t(c.e[1])].angleType() << 2 |
                               island.edges[std::size_t(c.e[2])].angleType() << 4;
        const FacetSeparation s = sphereFacetSeparation(radius, n, v0, v1, v2, flags);
        if (!(-radius < s.distance) || !(math::dot(n, s.normal) < 0.25f)) {
            continue;
        }
        const Vec3 off = s.onFacet - midS;
        if (!(math::dot(off, off) < bound2)) {
            continue;
        }
        const Vec3 nrm = math::cross(v1 - v0, v2 - v0);
        const float area = math::dot(nrm, nrm);
        const Vec3 k = math::cross(v0 - s.onFacet, nrm);
        const float ws = math::dot(v2 - s.onFacet, k) / area;
        const float wt = math::dot(s.onFacet - v1, k) / area;
        float value = 0;
        const bool met = centrePathMeetsFacet(start, delta, v0, v1, v2, value);
        const std::uint64_t word = met && value < radius ? 0x12 : 0x02;
        CachedContact e;
        e.distance = s.distance;
        e.normal = s.normal;
        e.pointA = s.onFacet;
        e.pointB = s.onSphere;
        e.subA = word | std::uint64_t(fi) << 8;
        e.extra = {std::uint32_t(quantizeWeight(ws)) | std::uint32_t(quantizeWeight(wt)) << 16, f.user, 0, 0};
        keepSweptCandidate(out, e);
    }
}

}   // namespace

void sweptSphereFacet(float radius, const Vec3& normal, const Vec3& v0, const Vec3& v1, const Vec3& v2,
                      unsigned edgeFlags, float& distance, Vec3& contactNormal, Vec3& onFacet, Vec3& onSphere) {
    const FacetSeparation s = sphereFacetSeparation(radius, normal, v0, v1, v2, edgeFlags);
    distance = s.distance;
    contactNormal = s.normal;
    onFacet = s.onFacet;
    onSphere = s.onSphere;
}

void sweptSphereMeshContacts(const MeshPlacement& placement, float radius, const Pose& current, const Pose& predicted,
                             std::vector<CachedContact>& out) {
    const data::LargeMesh& mesh = *placement.mesh;
    const Pose toMesh = inversePose(placement.pose);
    const Pose rel = compose(toMesh, current);
    const Vec3 next = transformPoint(toMesh, predicted.translation);
    const Vec3 ext = absVec(column(rel.rotation, 2)) * radius + absVec(column(rel.rotation, 0)) * radius +
                     absVec(column(rel.rotation, 1)) * radius;
    const Vec3 move = next - rel.translation;
    const float move2 = math::dot(move, move);
    const float reach = safeLength(ext) + (1e-5f <= move2 ? std::sqrt(move2) * 0.5f : 0.0f);
    const Vec3 mid = (rel.translation + next) * 0.5f;
    const Box query{mid - Vec3{reach, reach, reach}, mid + Vec3{reach, reach, reach}};
    const Box root{mesh.centre - mesh.halfExtent, mesh.centre + mesh.halfExtent};
    const Mat3 normalToWorld = transpose(inverse(current.rotation));
    std::vector<CachedContact> islandCandidates;
    meshtree::walk(mesh.tree, root, touching(query), [&](const unsigned* leaves, int n) {
        for (int i = 0; i < n; ++i) {
            const data::MeshIsland& island = mesh.islands[leaves[i]];
            if (apart({island.min, island.max}, query)) {
                continue;
            }
            islandCandidates.clear();
            sweptIsland(island, placement.mirrored, radius, reach, rel, next, mid, query, islandCandidates);
            // Into the mesh's results (0xe7b5a0): normal to world, distance along it between the two
            // points in world space, the facet point into the mesh frame, the island into the word.
            for (const CachedContact& e : islandCandidates) {
                CachedContact o = e;
                o.normal = libraryUnit(apply(normalToWorld, e.normal));
                o.distance = math::dot(transformPoint(current, e.pointA) - transformPoint(current, e.pointB), o.normal);
                o.pointA = transformPoint(rel, e.pointA);
                o.subA = (e.subA & 0xffff) | std::uint64_t(leaves[i]) << 16;
                keepSweptCandidate(out, o);
            }
        }
    });
}

void cylinderFacetContact(float halfLength, float radius, int facet, const Vec3& normal, const Vec3& v0,
                          const Vec3& v1, const Vec3& v2, float thickness, unsigned edgeFlags, ContactCache& out) {
    const CylinderSupport cylinder(halfLength, radius);
    // Nothing to do when the whole cylinder lies below the facet's plane by more than its thickness.
    if (math::dot(normal, v0) > math::dot(normal, cylinder.support(normal)) + thickness) {
        return;
    }
    const float meanEdge = (safeLength(v0 - v1) + safeLength(v1 - v2) + safeLength(v2 - v0)) / 3.0f;
    const Vec3 apex = (v0 + v1 + v2) * (1.0f / 3.0f) - normal * meanEdge;
    const Vec3 centre = (v1 + v0 + v2 + apex) * 0.25f;
    const Vec3 points[4] = {v0 - centre, v1 - centre, v2 - centre, apex - centre};
    const TetraSupport tetra(points);
    const auto c = penetrationSingle(cylinder, Pose{}, tetra, Pose{Mat3{}, centre});
    if (!c || c->distance >= 0) {
        return;
    }
    const Vec3 onFacet = centre + c->pointB;
    Vec3 n = c->normal * -1.0f;
    const Vec3 down = normal * -1.0f;
    if (math::dot(n, down) < 0) {
        return;
    }
    // Where on the facet: weights of corners 1 (s) and 2 (t).
    const Vec3 e1 = v1 - v0, e2 = v2 - v0;
    const Vec3 nrm = math::cross(e1, e2);
    const float area = math::dot(nrm, nrm);
    const Vec3 k = math::cross(v0 - onFacet, nrm);
    const float s = math::dot(v2 - onFacet, k) / area;
    const float t = math::dot(onFacet - v1, k) / area;
    // Near an edge or corner whose edges are all flat, the facet's own normal.
    unsigned mask;
    bool interior = false;
    if (s < kOnEdge) {
        mask = t < kOnEdge ? 0x33 : t > kAtCorner ? 0x3c : 0x30;
    } else if (t < kOnEdge) {
        mask = s > kAtCorner ? 0x0f : 0x03;
    } else {
        mask = 0x0c;
        interior = true;
    }
    if ((edgeFlags & mask) == 0 && (!interior || double(s + t) > kOnFarEdge)) {
        n = down;
    }
    out.add(c->distance, n, onFacet, c->pointA, std::uint64_t(facet << 8) & 0xffff, 0);
}

namespace {

void primitiveMeshContacts(const MeshPlacement& placement, const Primitive& prim, const Pose& cylinder,
                           ContactCache& out) {
    const data::LargeMesh& mesh = *placement.mesh;
    const Pose rel = compose(inversePose(placement.pose), cylinder);
    const Mat3 normalToWorld = transpose(inverse(placement.pose.rotation));
    const Box query = primitiveBox(prim, rel);
    const Box root{mesh.centre - mesh.halfExtent, mesh.centre + mesh.halfExtent};
    ContactCache islandCache;
    meshtree::walk(mesh.tree, root, touching(query), [&](const unsigned* leaves, int n) {
        for (int i = 0; i < n; ++i) {
            const unsigned index = leaves[i];
            const data::MeshIsland& island = mesh.islands[index];
            islandCache.clear();
            if (apart({island.min, island.max}, query)) {
                continue;
            }
            if (prim.kind == Primitive::Hull) {
                hullIslandContacts(island, prim, rel, islandCache);
            } else {
                primitiveIslandContacts(island, placement.mirrored, prim, rel, islandCache);
            }
            for (int k = 0; k < islandCache.size(); ++k) {
                const CachedContact& e = islandCache[k];
                const Vec3 nw = unit(apply(normalToWorld, e.normal));
                const Vec3 pa = transformPoint(placement.pose, e.pointA);
                const Vec3 pb = transformPoint(cylinder, e.pointB);
                const std::uint64_t subA =
                    (e.subA & 0xffffffff00000000ull) + (std::uint64_t(index) << 16) | (e.subA & 0xffff);
                out.add(math::dot(pa - pb, nw), nw, e.pointA, e.pointB, subA, e.subB);
            }
        }
    });
}

}   // namespace

void cylinderMeshContacts(const MeshPlacement& mesh, float halfLength, float radius, const Pose& cylinder,
                          ContactCache& out) {
    Primitive p;
    p.kind = Primitive::Cylinder;
    p.halfLength = halfLength;
    p.radius = radius;
    primitiveMeshContacts(mesh, p, cylinder, out);
}

void sphereMeshContacts(const MeshPlacement& mesh, float radius, const Pose& sphere, ContactCache& out) {
    Primitive p;
    p.radius = radius;
    primitiveMeshContacts(mesh, p, sphere, out);
}

void hullMeshContacts(const MeshPlacement& mesh, const Vec3* points, int count, const Pose& hull, ContactCache& out) {
    Primitive p;
    p.kind = Primitive::Hull;
    p.hull = points;
    p.hullCount = count;
    primitiveMeshContacts(mesh, p, hull, out);
}

void boxMeshContacts(const MeshPlacement& mesh, const Vec3& half, const Pose& box, ContactCache& out) {
    Primitive p;
    p.kind = Primitive::Box;
    p.half = half;
    primitiveMeshContacts(mesh, p, box, out);
}

void capsuleMeshContacts(const MeshPlacement& mesh, float halfLength, float radius, const Pose& capsule,
                         ContactCache& out) {
    Primitive p;
    p.kind = Primitive::Capsule;
    p.halfLength = halfLength;
    p.radius = radius;
    primitiveMeshContacts(mesh, p, capsule, out);
}

}   // namespace beacon::physics::rigid
