#include "physics/rigid/ConvexSolver.hpp"

#include "math/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace beacon::physics::rigid {

using math::Vec3;

namespace {

constexpr int kMaxIterations = 32;          // portal discovery and refinement (CMP 0x20)
constexpr float kCentresApart = 1e-8f;      // squared distance of the interior points
constexpr float kParallel = 1e-16f;         // squared length of v1 x v0
constexpr float kAxisTest = 1e-8f;          // 1 - |n.axis| picking the fallback axis
constexpr float kConverged = 1e-4f;         // support plane advance
constexpr float kShallowEnough = 0.0100000007f;   // squared depth that ends the full search
constexpr float kSideStart = 0.01f;         // least depth of a side search's interior point
constexpr float kSideNormal = 1e-6f;        // squared side normal length
constexpr float kFlatPortal = 1e-8f;        // portal triangle area (cross product length)
constexpr float kCollinear = 0.01f;         // |1 - cos| of two portal edges
constexpr float kSingular = 1e-16f;         // squared barycentric determinant
constexpr float kSegmentNormalDepth = 0.001f;
constexpr float kNormalLength = 1e-6f;

Vec3 unit(const Vec3& v) { return v * (1.0f / std::sqrt(math::dot(v, v))); }

// Length that is 0 for anything not above the smallest normal float (the library's guarded
// reciprocal square root).
float safeLength(const Vec3& v) {
    const float l2 = math::dot(v, v);
    return l2 > 1.17549435e-38f ? std::sqrt(l2) : 0.0f;
}

// Support points of A - B with B placed in A's frame (rotation r, translation t).
struct MinkowskiPair {
    const SupportShape& a;
    const SupportShape& b;
    Mat3 r, rInv;
    Vec3 t;

    // Fills slot i of a portal along n: the vertex v = p - (r q + t), p on A, q on B (B's frame).
    template <class Portal>
    void support(Portal& portal, int i, const Vec3& n) const {
        const Vec3 p = a.support(n);
        const Vec3 q = b.support(unit(apply(rInv, n * -1.0f)));
        portal.p[i] = p;
        portal.q[i] = q;
        portal.v[i] = p - (apply(r, q) + t);
    }
};

// v[0] is an interior point of A - B; v[1..3] a triangle (the portal) that the ray from v[0]
// through the origin passes.
struct Portal {
    Vec3 v[4], p[4], q[4];

    void swap(int i, int j) {
        std::swap(v[i], v[j]);
        std::swap(p[i], p[j]);
        std::swap(q[i], q[j]);
    }
};

// Portal discovery (0xe1ebd0); false when the origin is found outside A - B or after 33 rounds.
bool discoverPortal(const MinkowskiPair& m, Portal& portal) {
    Vec3* v = portal.v;
    Vec3 n = unit(v[0] * -1.0f);
    m.support(portal, 1, n);
    if (math::dot(v[1], n) < 0) {
        return false;
    }
    Vec3 c = math::cross(v[1], v[0]);
    if (math::dot(c, c) < kParallel) {
        // v1 on the line through v0 and the origin: any direction across it.
        if (1.0f - std::fabs(n.x) > kAxisTest) {
            c = math::cross(Vec3{1, 0, 0}, v[0]);
        } else if (1.0f - std::fabs(n.y) > kAxisTest) {
            c = math::cross(Vec3{0, 1, 0}, v[0]);
        } else {
            c = math::cross(Vec3{0, 0, 1}, v[0]);
        }
    }
    n = unit(c);
    m.support(portal, 2, n);
    if (math::dot(v[2], n) < 0) {
        return false;
    }
    n = unit(math::cross(v[1] - v[0], v[2] - v[0]));
    m.support(portal, 3, n);
    if (math::dot(v[3], n) < 0) {
        return false;
    }
    for (int round = 0;; ++round) {
        // A side face of the cone (v0; v1, v2, v3) with the origin in front of it gives way to a
        // support point beyond it; the others stay.
        bool found = false;
        n = unit(math::cross(v[1] - v[0], v[3] - v[0]));
        if (math::dot(n, v[0] * -1.0f) > 0) {
            portal.swap(1, 3);
            m.support(portal, 2, n);
        } else {
            n = unit(math::cross(v[2] - v[0], v[1] - v[0]));
            if (math::dot(n, v[0] * -1.0f) > 0) {
                portal.swap(1, 2);
                m.support(portal, 3, n);
            } else {
                n = unit(math::cross(v[3] - v[0], v[2] - v[0]));
                if (math::dot(n, v[0] * -1.0f) > 0) {
                    portal.swap(2, 3);
                    m.support(portal, 1, n);
                } else {
                    found = true;
                }
            }
        }
        if (round > kMaxIterations) {
            return false;
        }
        if (found) {
            return true;
        }
    }
}

enum class Refined { Converged, Separated, Failed };

// Portal refinement (inlined in both searches): pushes the portal out along its normal until the
// support plane stops advancing (1e-4) or after 33 rounds; Separated when a support plane has the
// origin outside.
Refined refinePortal(const MinkowskiPair& m, Portal& portal) {
    Vec3* v = portal.v;
    for (int round = 0;; ++round) {
        const Vec3 n = unit(math::cross(v[2] - v[1], v[3] - v[1]));
        if (!std::isfinite(n.x)) {
            return Refined::Failed;
        }
        Portal w;
        m.support(w, 0, n);
        const Vec3 x = w.v[0];
        if (math::dot(n, x * -1.0f) > 0) {
            return Refined::Separated;
        }
        if (math::dot(n, x - v[1]) < kConverged || round > kMaxIterations) {
            return Refined::Converged;
        }
        // The new point replaces one corner so that the ray from v0 still passes the portal.
        const Vec3 o = v[0] * -1.0f;
        const float c1 = math::dot(math::cross(x - v[0], v[2] - v[0]), o);
        const float c2 = math::dot(math::cross(x - v[0], v[3] - v[0]), o);
        int slot = 0;
        if (c1 > 0 && c2 <= 0) {
            slot = 1;
        } else {
            const float c3 = math::dot(math::cross(x - v[0], v[1] - v[0]), o);
            if (c3 > 0 && c1 <= 0) {
                slot = 3;
            } else if (c3 <= 0 && c2 > 0) {
                slot = 2;
            }
        }
        if (slot != 0) {
            portal.v[slot] = x;
            portal.p[slot] = w.p[0];
            portal.q[slot] = w.q[0];
        }
    }
}

// Point of triangle (a, b, c) closest to the origin, by its Voronoi regions.
Vec3 closestToOrigin(const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a, ac = c - a;
    const Vec3 ap = a * -1.0f;
    const float d1 = math::dot(ab, ap), d2 = math::dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) {
        return a;
    }
    const Vec3 bp = b * -1.0f;
    const float d3 = math::dot(ab, bp), d4 = math::dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        return a + ab * (d1 / (d1 - d3));
    }
    const Vec3 cp = c * -1.0f;
    const float d5 = math::dot(ab, cp), d6 = math::dot(ac, cp);
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
    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

// Contact from the final portal and its point closest to the origin (shared by both searches;
// they differ in the depth below which a triangle's normal replaces -closest).
std::optional<ConvexContact> contactFromPortal(const Portal& portal, const Vec3& closest, float faceNormalDepth) {
    const Vec3* v = portal.v;
    const Vec3 e1 = v[2] - v[1], e2 = v[3] - v[1];
    if (safeLength(math::cross(e1, e2)) < kFlatPortal) {
        return ConvexContact{0, {1, 0, 0}, portal.p[1], portal.q[1]};
    }
    const float depth = safeLength(closest);
    // A portal whose corners lie on a line: interpolate along the two outer corners.
    const Vec3 u1 = unit(e1), u2 = unit(v[3] - v[2]), u3 = unit(v[1] - v[3]);
    int i = 0, j = 0;
    if (std::fabs(1.0f - math::dot(u3, u1 * -1.0f)) < kCollinear) {
        i = 1, j = 2;
    } else if (std::fabs(1.0f - math::dot(u2, u1 * -1.0f)) < kCollinear) {
        i = 2, j = 3;
    } else if (std::fabs(1.0f - math::dot(u3, u2 * -1.0f)) < kCollinear) {
        i = 3, j = 1;
    }
    ConvexContact out;
    Vec3 normal;
    if (i != 0) {
        const Vec3 a = v[i], b = v[j];
        const float t = std::clamp(math::dot(b - a, closest - a) / math::dot(b - a, b - a), 0.0f, 1.0f);
        out.pointA = portal.p[i] + (portal.p[j] - portal.p[i]) * t;
        out.pointB = portal.q[i] + (portal.q[j] - portal.q[i]) * t;
        if (depth < kSegmentNormalDepth) {
            // Touching: from the segment towards the interior point.
            const Vec3 ba = a - b;
            const float s = std::max(0.0f, std::min(math::dot(ba, v[0] - b) / math::dot(ba, ba), 1.0f));
            normal = v[0] - (b + ba * s);
        } else {
            normal = closest * -1.0f;
        }
    } else {
        const float d00 = math::dot(e1, e1), d01 = math::dot(e1, e2), d11 = math::dot(e2, e2);
        const float det = d00 * d11 - d01 * d01;
        if (det * det < kSingular) {
            return std::nullopt;
        }
        const Vec3 r = closest - v[1];
        const float d20 = math::dot(e1, r), d21 = math::dot(e2, r);
        const float s = (d11 * d20 - d01 * d21) / det;
        const float t = (d00 * d21 - d01 * d20) / det;
        out.pointA = portal.p[1] + (portal.p[2] - portal.p[1]) * s + (portal.p[3] - portal.p[1]) * t;
        out.pointB = portal.q[1] + (portal.q[2] - portal.q[1]) * s + (portal.q[3] - portal.q[1]) * t;
        normal = depth < faceNormalDepth ? math::cross(e2, e1) : closest * -1.0f;
    }
    if (safeLength(normal) < kNormalLength) {
        out.normal = {1, 0, 0};
        out.distance = 0;
    } else {
        out.normal = unit(normal);
        out.distance = -depth;
    }
    return out;
}

MinkowskiPair relativePair(const SupportShape& a, const Pose& poseA, const SupportShape& b, const Pose& poseB) {
    const Mat3 aInv = transpose(poseA.rotation);
    MinkowskiPair m{a, b, multiply(aInv, poseB.rotation), {}, apply(aInv, poseB.translation - poseA.translation)};
    m.rInv = transpose(m.r);
    return m;
}

}   // namespace

Vec3 CylinderSupport::support(const Vec3& dir) const {
    const float x = dir.x > 0 ? halfLength_ : -halfLength_;
    const float side = dir.y * dir.y + dir.z * dir.z;
    if (side < 1e-6f) {
        return {x, 0, 0};
    }
    const float k = radius_ / std::sqrt(side);
    return {x, dir.y * k, dir.z * k};
}

Vec3 BoxSupport::support(const Vec3& dir) const {
    return {dir.x > 0 ? half_.x : -half_.x, dir.y > 0 ? half_.y : -half_.y, dir.z > 0 ? half_.z : -half_.z};
}

Vec3 CapsuleSupport::support(const Vec3& dir) const {
    return Vec3{dir.x > 0 ? halfLength_ : -halfLength_, 0, 0} + dir * radius_;
}

Vec3 TriangleSupport::support(const Vec3& dir) const {
    const float d0 = math::dot(dir, points_[0]), d1 = math::dot(dir, points_[1]), d2 = math::dot(dir, points_[2]);
    if (d0 <= d1 || d0 <= d2) {
        return d1 <= d2 ? points_[2] : points_[1];
    }
    return points_[0];
}

Vec3 HullSupport::support(const Vec3& dir) const {
    int best = 0;
    float most = math::dot(points_[0], dir);
    for (int i = 1; i < count_; ++i) {
        const float d = math::dot(points_[i], dir);
        if (d > most) {
            most = d;
            best = i;
        }
    }
    return points_[best];
}

Vec3 HullSupport::centre() const {
    Vec3 sum;
    for (int i = 0; i < count_; ++i) {
        sum = sum + points_[i];
    }
    return count_ > 0 ? sum * (1.0f / float(count_)) : Vec3{};
}

TetraSupport::TetraSupport(const Vec3 (&points)[4]) {
    std::copy(points, points + 4, points_);
}

Vec3 TetraSupport::support(const Vec3& dir) const {
    float d[4];
    for (int i = 0; i < 4; ++i) {
        d[i] = math::dot(dir, points_[i]);
    }
    int best = d[1] > d[0] ? 1 : 0;
    for (int i = 2; i < 4; ++i) {
        if (d[i] > d[best]) {
            best = i;
        }
    }
    return points_[best];
}

Vec3 TetraSupport::centre() const {
    return (points_[0] + points_[1] + points_[2] + points_[3]) * 0.25f;
}

std::optional<ConvexContact> penetrationSingle(const SupportShape& a, const Pose& poseA, const SupportShape& b,
                                               const Pose& poseB) {
    const MinkowskiPair m = relativePair(a, poseA, b, poseB);
    Portal portal;
    portal.v[0] = a.centre() - (apply(m.r, b.centre()) + m.t);
    if (math::dot(portal.v[0], portal.v[0]) < kCentresApart || !discoverPortal(m, portal) ||
        refinePortal(m, portal) != Refined::Converged) {
        return std::nullopt;
    }
    const Vec3 closest = closestToOrigin(portal.v[1], portal.v[2], portal.v[3]);
    return contactFromPortal(portal, closest, 1e-5f);
}

std::optional<ConvexContact> penetrationFull(const SupportShape& a, const Pose& poseA, const SupportShape& b,
                                             const Pose& poseB) {
    const MinkowskiPair m = relativePair(a, poseA, b, poseB);
    // The first search starts at the interior points; the others at points below the first
    // portal's side faces (v0, v1, v2), (v0, v2, v3), (v0, v3, v1).
    static constexpr int kSides[3][2] = {{1, 2}, {2, 3}, {3, 1}};
    Portal portals[4];
    Vec3 closest[4];
    float depth2[4] = {1e38f, 1e38f, 1e38f, 1e38f};
    for (int k = 0; k < 4; ++k) {
        Portal& portal = portals[k];
        if (k == 0) {
            portal.v[0] = a.centre() - (apply(m.r, b.centre()) + m.t);
            if (math::dot(portal.v[0], portal.v[0]) < kCentresApart) {
                return std::nullopt;
            }
        } else {
            const Portal& first = portals[0];
            const Vec3 o = first.v[0];
            const Vec3 side =
                math::cross(first.v[kSides[k - 1][1]] - o, first.v[kSides[k - 1][0]] - o);
            if (math::dot(side, side) > kSideNormal) {
                const Vec3 n = unit(side);
                portal.v[0] = n * -std::max(kSideStart, math::dot(o, n));
            } else {
                portal.v[0] = {-std::max(kSideStart, o.x), 0, 0};
            }
        }
        if (!discoverPortal(m, portal)) {
            if (k == 0) {
                return std::nullopt;
            }
            break;
        }
        const Refined r = refinePortal(m, portal);
        if (r != Refined::Converged) {
            if (k == 0) {
                return std::nullopt;
            }
            continue;
        }
        closest[k] = closestToOrigin(portal.v[1], portal.v[2], portal.v[3]);
        depth2[k] = math::dot(closest[k], closest[k]);
        if (depth2[k] < kShallowEnough) {
            break;
        }
    }
    int best = depth2[1] < depth2[0] ? 1 : 0;
    for (int k = 2; k < 4; ++k) {
        if (depth2[k] < depth2[best]) {
            best = k;
        }
    }
    return contactFromPortal(portals[best], closest[best], kSegmentNormalDepth);
}

namespace {

// The simplex of the distance search (0xe1b870): points of A - B with the A and B points they
// came from (both in A's frame) and the barycentric weights of the closest point. The step is the
// signed-volumes method: the origin's barycentric coordinates in the simplex (for a triangle in
// the plane where its projection is largest); a sub-simplex is searched where a coordinate's sign
// is not clearly (beyond 1e-6) that of the whole.
struct Simplex {
    Vec3 w[4], p[4], q[4];
    float weight[4] = {};
    int count = 0;

    // One search: the best candidate so far (squared distance; segment interiors are compared
    // with it but do not lower it, as in the original), the kept points and their count.
    struct Search {
        float best = 1e38f;
        int ids[4] = {0, 1, 2, 3};
        int count = 0;
        Vec3 v;
    };

    static bool sameSign(float whole, float part) {
        constexpr float kEps = 1e-6f;
        return !((whole <= kEps || part <= kEps) && (-kEps <= whole || -kEps <= part));
    }

    void vertex(Search& s, int i) {
        const float d = math::dot(w[i], w[i]);
        if (d < s.best) {
            s.best = d;
            weight[0] = 1;
            s.v = w[i];
            s.count = 1;
            s.ids[0] = i;
        }
    }

    // num / den as the original computes it (reciprocal estimate, refined; here from the exact
    // reciprocal).
    static float quotient(float num, float den) {
        const float r0 = 1.0f / den;
        const float r1 = (r0 + r0) - r0 * (r0 * den);
        const float f = num * r1;
        return (f + f) - f * (den * r1);
    }

    void segment(Search& s, int i, int j) {
        const Vec3 d = w[j] - w[i];
        const float t = -quotient(math::dot(w[i], d), math::dot(d, d));
        if (t <= 0) {
            vertex(s, i);
        } else if (1 <= t) {
            vertex(s, j);
        } else {
            const Vec3 c = w[i] + d * t;
            if (math::dot(c, c) < s.best) {
                weight[0] = 1 - t, weight[1] = t;
                s.v = c;
                s.count = 2;
                s.ids[0] = i, s.ids[1] = j;
            }
        }
    }

    void triangle(Search& s, int a, int b, int c) {
        const Vec3 A = w[a], B = w[b], C = w[c];
        const Vec3 n = math::cross(B - A, C - A);
        const float nn = n.z * n.z + (n.x * n.x + n.y * n.y);
        const float an = A.z * n.z + (A.x * n.x + A.y * n.y);
        const Vec3 P = n * quotient(an, nn);   // the origin moved onto the plane
        // Projected areas in the planes (y, z), (x, z), (x, y), in the original's operation order;
        // the largest one decides (the first on ties).
        const auto coords = [](const Vec3& v, int plane) {
            return plane == 0 ? std::pair{v.y, v.z} : plane == 1 ? std::pair{v.x, v.z} : std::pair{v.x, v.y};
        };
        const auto area = [&](int plane) {
            const auto [as, at] = coords(A, plane);
            const auto [bs, bt] = coords(B, plane);
            const auto [cs, ct] = coords(C, plane);
            return (bt * as + (ct * bs + at * cs)) - (bs * at + (cs * bt + as * ct));
        };
        const float areas[3] = {area(0), area(1), area(2)};
        const float most = std::max(std::fabs(areas[0]), std::max(std::fabs(areas[1]), std::fabs(areas[2])));
        const int plane = std::fabs(areas[0]) == most ? 0 : std::fabs(areas[1]) == most ? 1 : 2;
        const float whole = areas[plane];
        const auto [as, at] = coords(A, plane);
        const auto [bs, bt] = coords(B, plane);
        const auto [cs, ct] = coords(C, plane);
        const auto [ps, pt] = coords(P, plane);
        const float part[3] = {((ct * bs - cs * bt) + (bt * ps - bs * pt)) + (cs * pt - ct * ps),
                               ((as * pt - at * ps) + (at * cs - as * ct)) + (ct * ps - cs * pt),
                               ((at * ps - as * pt) + (bs * pt - bt * ps)) + (bt * as - bs * at)};
        bool inside = true;
        if (!sameSign(whole, part[0])) {
            segment(s, b, c);
            inside = false;
        }
        if (!sameSign(whole, part[1])) {
            segment(s, a, c);
            inside = false;
        }
        if (!sameSign(whole, part[2])) {
            segment(s, a, b);
            inside = false;
        }
        if (inside) {
            const float r = 1.0f / whole;
            const float l0 = r * part[0], l1 = r * part[1], l2 = r * part[2];
            const Vec3 v = A * l0 + B * l1 + C * l2;
            const float d = math::dot(v, v);
            if (d < s.best) {
                s.best = d;
                weight[0] = l0, weight[1] = l1, weight[2] = l2;
                s.v = v;
                s.count = 3;
                s.ids[0] = a, s.ids[1] = b, s.ids[2] = c;
            }
        }
    }

    void tetrahedron(Search& s) {
        // The cofactors as the original computes them: each a triple product summed (x + y) + z.
        const float part[4] = {math::dot(w[3], math::cross(w[1], w[2])), math::dot(w[3], math::cross(w[2], w[0])),
                               math::dot(w[3], math::cross(w[0], w[1])), math::dot(w[2], math::cross(w[1], w[0]))};
        const float whole = ((part[0] + part[1]) + part[2]) + part[3];
        static constexpr int kFaces[4][3] = {{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}};
        bool inside = true;
        for (int j = 0; j < 4; ++j) {
            if (!sameSign(whole, part[j])) {
                triangle(s, kFaces[j][0], kFaces[j][1], kFaces[j][2]);
                inside = false;
            }
        }
        if (inside) {
            const float r = 1.0f / whole;
            Vec3 v;
            for (int i = 0; i < 4; ++i) {
                weight[i] = part[i] * r;
                v = v + w[i] * weight[i];
            }
            const float d = math::dot(v, v);
            if (d < s.best) {
                s.best = d;
                s.v = v;
                s.count = 4;
                for (int i = 0; i < 4; ++i) {
                    s.ids[i] = i;
                }
            }
        }
    }

    // The point of the simplex closest to the origin (v keeps its value when no candidate wins);
    // the simplex shrinks to the points it lies on. False for an empty simplex (never here).
    bool closest(Vec3& v) {
        for (float& f : weight) {
            f = 0;
        }
        Search s;
        s.count = count;
        s.v = v;
        switch (count) {
        case 1:
            weight[0] = 1;
            v = w[0];
            return true;
        case 2:
            // The two-point step returns before the compaction: when the newer point is the
            // closest, one point with weight 1 is kept and that is slot 0, still the older point
            // (its A and B points end up in the result).
            segment(s, 0, 1);
            count = s.count;
            v = s.v;
            return true;
        case 3:
            triangle(s, 0, 1, 2);
            break;
        case 4:
            tetrahedron(s);
            break;
        default:
            return false;
        }
        if (s.count != count) {
            for (int k = 0; k < s.count; ++k) {
                const int i = s.ids[k];
                w[k] = w[i], p[k] = p[i], q[k] = q[i];
            }
            count = s.count;
        }
        v = s.v;
        return true;
    }
};

}   // namespace

int separationStep(const Vec3* w, int count, Vec3& v, float (&weights)[4], int (&ids)[4]) {
    Simplex s;
    for (int i = 0; i < count; ++i) {
        s.w[i] = w[i];
        s.p[i] = Vec3{float(i), 0, 0};
    }
    s.count = count;
    s.closest(v);
    for (int i = 0; i < 4; ++i) {
        weights[i] = s.weight[i];
        ids[i] = i < s.count ? int(s.p[i].x) : -1;
    }
    return s.count;
}

Separation separation(const SupportShape& a, const Pose& poseA, const SupportShape& b, const Pose& poseB) {
    Pose toA;
    toA.rotation = inverse(poseA.rotation);
    toA.translation = apply(toA.rotation, poseA.translation) * -1.0f;
    const Pose rel{multiply(toA.rotation, poseB.rotation), apply(toA.rotation, poseB.translation) + toA.translation};
    const Mat3 dirToB = transpose(rel.rotation);
    // First direction: from B's origin towards A's (unit; its squared length scales the first
    // support direction); none when the origins coincide.
    Vec3 v{1, 0, 0};
    float v2 = 1;
    const float t2 = math::dot(rel.translation, rel.translation);
    if (1e-6f <= t2) {
        v = rel.translation * (-1.0f / std::sqrt(t2));
        v2 = t2;
    }
    Simplex s;
    Separation out;
    // The normal comes from the direction of the last support query: after the iteration limit
    // that is the closest point before the last simplex step (the points are from after it).
    Vec3 queried = v;
    for (int iteration = 0;; ++iteration) {
        const float scale = 1.0f / std::sqrt(v2);
        queried = v;
        const Vec3 pa = a.support(v * -scale);
        const Vec3 pb = rel.translation + apply(rel.rotation, b.support(unit(apply(dirToB, v * scale))));
        const Vec3 w = pa - pb;
        bool repeated = false;
        for (int i = 0; i < s.count; ++i) {
            const Vec3 d = s.w[i] - w;
            repeated = repeated || math::dot(d, d) < 1e-8f;
        }
        if (repeated || (iteration != 0 && v2 - math::dot(w, v) < 1e-6f)) {
            break;
        }
        s.w[s.count] = w, s.p[s.count] = pa, s.q[s.count] = pb;
        ++s.count;
        if (!s.closest(v)) {
            out.status = 3;
            return out;
        }
        if (s.count == 4) {
            out.status = 1;
            return out;
        }
        v2 = math::dot(v, v);
        if (v2 < 1e-12f) {
            out.status = 1;
            return out;
        }
        if (iteration > 9) {
            break;
        }
    }
    Vec3 pa, pb;
    for (int i = 0; i < s.count; ++i) {
        pa = pa + s.p[i] * s.weight[i];
        pb = pb + s.q[i] * s.weight[i];
    }
    const float vv = math::dot(queried, queried);
    out.normal = 1e-12f <= vv ? unit(queried) : Vec3{1, 0, 0};
    out.distance = math::dot(pa - pb, out.normal);
    out.pointA = pa;
    const Mat3 back = inverse(rel.rotation);
    out.pointB = apply(back, pb - rel.translation);
    out.status = 0;
    return out;
}

ConvexCast castConvex(const SupportShape& a, const Pose& poseA, const SupportShape& b, const Pose& poseB,
                      const Vec3& motion) {
    const Mat3 toA = inverse(poseA.rotation);
    const Mat3 rel = multiply(toA, poseB.rotation);
    const Mat3 dirToB = transpose(rel);
    Vec3 at = apply(toA, poseB.translation - poseA.translation);   // B's position in A's frame
    const Vec3 r = apply(toA, motion);
    Vec3 v{1, 0, 0};
    const float t2 = math::dot(at, at);
    if (1e-5f <= t2) {
        v = at * (-1.0f / std::sqrt(t2));
    }
    Simplex s;
    ConvexCast out;
    float lambda = 0, previous = 1e38f, scale = 1;
    Vec3 pointA, pointB;   // the weighted simplex points, refreshed after each step
    const auto simplexPoints = [&]() {
        pointA = {}, pointB = {};
        if (s.count == 1) {
            pointA = s.p[0], pointB = s.q[0];
        } else if (2 <= s.count && s.count <= 4) {
            for (int i = 0; i < s.count; ++i) {
                pointA = pointA + s.p[i] * s.weight[i];
                pointB = pointB + s.q[i] * s.weight[i];
            }
        }
    };
    for (int iteration = 0;; ++iteration) {
        const Vec3 pa = a.support(v * -scale);
        Vec3 pb = at + apply(rel, b.support(unit(apply(dirToB, v * scale))));
        Vec3 w = pa - pb;
        bool repeated = false;
        for (int i = 0; i < s.count; ++i) {
            const Vec3 d = s.w[i] - w;
            repeated = repeated || math::dot(d, d) < 1e-8f;
        }
        if (repeated) {
            simplexPoints();
            break;
        }
        const float vw = math::dot(v, w);
        if (0 < vw) {
            const float vr = math::dot(v, r);
            if (!(0 < vr)) {
                return out;   // moving apart: no contact
            }
            const float step = vw / vr;
            if (!(lambda + step < 1.0f)) {
                return out;
            }
            if (step < 1e-8f) {
                simplexPoints();
                break;
            }
            // Advance B to where the plane through w along v is reached.
            const Vec3 move = r * step;
            at = at + move;
            pb = pb + move;
            w = pa - pb;
            for (int i = 0; i < s.count; ++i) {
                s.q[i] = s.q[i] + move;
                s.w[i] = s.p[i] - s.q[i];
            }
            lambda += step;
        }
        if (iteration != 0 && previous - vw < 1e-6f) {
            simplexPoints();
            break;
        }
        s.w[s.count] = w, s.p[s.count] = pa, s.q[s.count] = pb;
        ++s.count;
        if (!s.closest(v)) {
            out.status = 3;
            return out;
        }
        if (s.count == 4) {
            if (0 < lambda) {
                break;   // with the points of the step before (as the original)
            }
            out.status = 1;
            out.t = 0;
            return out;
        }
        if (15 < iteration) {
            simplexPoints();
            break;
        }
        previous = math::dot(v, v);
        if (previous < 1e-8f) {
            simplexPoints();
            break;
        }
        simplexPoints();
        scale = 1.0f / std::sqrt(previous);
    }
    const float vv = math::dot(v, v);
    out.normal = 1e-16f <= vv ? v * (1.0f / std::sqrt(vv)) : Vec3{1, 0, 0};
    out.t = lambda;
    out.pointA = pointA;
    // B's point back at its start, in B's frame.
    const Vec3 world = apply(poseA.rotation, lambda != 0 ? pointB - r * lambda : pointB) + poseA.translation;
    out.pointB = apply(inverse(poseB.rotation), world - poseB.translation);
    out.status = lambda != 0 ? 0 : 1;
    return out;
}

}   // namespace beacon::physics::rigid
