#include "physics/rigid/SweptContacts.hpp"

#include "math/Vector.hpp"
#include "physics/rigid/MeshContacts.hpp"
#include "physics/rigid/PrimitiveContacts.hpp"

#include <cmath>

namespace beacon::physics::rigid {

using math::Vec3;

Pose sweepPose(const Vec3& from, const Vec3& to, float& halfLength) {
    const Vec3 d = to - from;
    const float d2 = math::dot(d, d);
    const float length = d2 > 1.17549435e-38f ? std::sqrt(d2) : 0.0f;
    halfLength = length * 0.5f;
    math::Quat q{0, 0, 0, 1};
    if (1e-5f <= length) {
        const Vec3 dir = d * (1.0f / length);
        const float s = std::sqrt(2.0f * dir.x + 2.0f);   // shortest arc from the x axis
        q = {0, -dir.z / s, dir.y / s, s * 0.5f};
    }
    return {rotationMatrix(q), (from + to) * 0.5f};
}

namespace {

using K = SweptPrimitive::Kind;

// The current contact of the swept sphere `s` with the primitive `p`, in the order the original
// passes them to the pair math; normal from p towards s, points in their own frames.
bool currentContact(const SweptPrimitive& s, const Pose& ps, const SweptPrimitive& p, const Pose& pp, bool sphereFirst,
                    ConvexContact& c, Vec3& onSphere, Vec3& onPrimitive) {
    switch (p.kind) {
    case K::Sphere:
        if (sphereFirst) {
            c = sphereSphere(s.radius, ps, p.radius, pp);
            c.normal = c.normal * -1.0f;
            onSphere = c.pointA, onPrimitive = c.pointB;
        } else {
            c = sphereSphere(p.radius, pp, s.radius, ps);
            onSphere = c.pointB, onPrimitive = c.pointA;
        }
        return true;
    case K::Box:
        c = boxSphere(p.halfExtents, pp, s.radius, ps);
        onSphere = c.pointB, onPrimitive = c.pointA;
        return true;
    case K::Capsule:
        c = capsuleSphere(p.halfLength, p.radius, pp, s.radius, ps);
        onSphere = c.pointB, onPrimitive = c.pointA;
        return true;
    default:
        return false;
    }
}

// Whether the sweep capsule overlaps the primitive (only the sign of the distance is used; the
// sweep goes first against spheres, second against boxes and capsules).
bool sweepHits(float half, float radius, const Pose& sweep, const SweptPrimitive& p, const Pose& pp) {
    switch (p.kind) {
    case K::Sphere:
        return capsuleSphere(half, radius, sweep, p.radius, pp).distance < 0;
    case K::Box:
        return boxCapsule(p.halfExtents, pp, half, radius, sweep).distance < 0;
    case K::Capsule:
        return capsuleCapsule(p.halfLength, p.radius, pp, half, radius, sweep).distance < 0;
    default:
        return false;
    }
}

// A cylinder or hull partner (0xe15830 / 0xe15ed0 with the swept sphere as A, 0xe16590 /
// 0xe16c60 with it as B).
void convexPartner(const SweptPrimitive& s, const Pose& ps, const Pose& predicted, const SweptPrimitive& p,
                   const Pose& pp, bool sweptA, std::uint32_t indexA, std::uint32_t indexB,
                   std::vector<CachedContact>& out) {
    const SphereSupport sphere(s.radius);
    const CylinderSupport cylinder(p.halfLength, p.radius);
    const HullSupport hull(p.hull, p.hullCount);
    const bool againstHull = p.kind == K::Hull;
    const SupportShape& partner = againstHull ? static_cast<const SupportShape&>(hull) : cylinder;
    // The search runs with the sphere first, except against a cylinder that is the pair's A.
    const bool sphereFirst = sweptA || againstHull;
    const Separation c = sphereFirst ? separation(sphere, ps, partner, pp) : separation(partner, pp, sphere, ps);
    if (!(c.distance < 1e38f && c.status == 0 && 0.0f < c.distance)) {
        return;
    }
    Vec3 onSphere = sphereFirst ? c.pointA : c.pointB;
    const Vec3 onPartner = sphereFirst ? c.pointB : c.pointA;
    if (againstHull) {
        onSphere = c.normal * -s.radius;
    }
    // World normal from the second shape of the search towards the first, turned to the pair's
    // B towards A.
    Vec3 normal = math::normalize(apply((sphereFirst ? ps : pp).rotation, c.normal));
    if (sphereFirst != sweptA) {
        normal = normal * -1.0f;
    }
    float half = 0;
    const Pose sweep = sweepPose(ps.translation, predicted.translation, half);
    const CapsuleSupport swept(half, s.radius);
    bool hit;
    if (sweptA) {
        hit = separation(swept, sweep, partner, pp).status == 1;
    } else {
        const Separation h = separation(partner, pp, swept, sweep);
        hit = h.status == 1 || (!againstHull && h.status == 0 && std::fabs(h.distance) < 1e-5f);
    }
    CachedContact e;
    e.pointA = sweptA ? onSphere : onPartner;
    e.pointB = sweptA ? onPartner : onSphere;
    const Pose& poseA = sweptA ? ps : pp;
    const Pose& poseB = sweptA ? pp : ps;
    e.normal = normal;
    e.distance = math::dot(apply(poseA.rotation, e.pointA) + poseA.translation -
                               (apply(poseB.rotation, e.pointB) + poseB.translation),
                           normal);
    const std::uint64_t flagged = hit ? 0x11 : 0x01;
    e.subA = std::uint64_t(indexA) << 32 | (sweptA ? 1 : flagged);
    e.subB = std::uint64_t(indexB) << 32 | (sweptA ? flagged : 1);
    out.push_back(e);
}

}   // namespace

bool sweptShapeContacts(const SweptPrimitive& a, const Pose& currentA, const Pose& predictedA, std::uint32_t indexA,
                        const SweptPrimitive& b, const Pose& currentB, const Pose& predictedB, std::uint32_t indexB,
                        std::vector<CachedContact>& out) {
    const bool sweptA = a.kind == K::SweptSphere, sweptB = b.kind == K::SweptSphere;
    if (sweptA == sweptB) {
        return sweptA;   // two swept spheres: no function in the table
    }
    const SweptPrimitive& s = sweptA ? a : b;
    const SweptPrimitive& p = sweptA ? b : a;
    const Pose& ps = sweptA ? currentA : currentB;
    const Pose& pp = sweptA ? currentB : currentA;
    if (p.kind == K::Mesh) {
        // 0xe17320: the mesh's candidates with the sides as in the pair (the mesh walk has the
        // mesh as A); the mesh's word goes into its side's sub-shape word.
        std::vector<CachedContact> found;
        sweptSphereMeshContacts(MeshPlacement{p.mesh, pp, p.mirrored}, s.radius, ps, sweptA ? predictedA : predictedB,
                                found);
        for (const CachedContact& m : found) {
            CachedContact e;
            e.distance = m.distance;
            const std::uint64_t meshWord = m.subA & 0xffffffffu;
            if (sweptA) {
                e.normal = m.normal * -1.0f;
                e.pointA = m.pointB, e.pointB = m.pointA;
                e.subA = std::uint64_t(indexA) << 32 | 1;
                e.subB = std::uint64_t(indexB) << 32 | meshWord;
                e.extra = {0, 0, m.extra[0], m.extra[1]};
            } else {
                e.normal = m.normal;
                e.pointA = m.pointA, e.pointB = m.pointB;
                e.subA = std::uint64_t(indexA) << 32 | meshWord;
                e.subB = std::uint64_t(indexB) << 32 | 1;
                e.extra = {m.extra[0], m.extra[1], 0, 0};
            }
            out.push_back(e);
        }
        return true;
    }
    if (p.kind == K::Cylinder || p.kind == K::Hull) {
        convexPartner(s, ps, sweptA ? predictedA : predictedB, p, pp, sweptA, indexA, indexB, out);
        return true;
    }
    if (p.kind != K::Sphere && p.kind != K::Box && p.kind != K::Capsule) {
        return false;
    }
    // A plain sphere partner goes first in the math when it is the pair's A (0xe13930); the
    // swept sphere goes first when it is A (0xe13300).
    ConvexContact c;
    Vec3 onSphere, onPrimitive;
    currentContact(s, ps, p, pp, sweptA, c, onSphere, onPrimitive);
    if (!(0.0f < c.distance)) {
        return true;   // touching now: the ordinary shapes of the body take it
    }
    float half = 0;
    const Pose sweep = sweepPose(ps.translation, (sweptA ? predictedA : predictedB).translation, half);
    const bool hit = sweepHits(half, s.radius, sweep, p, pp);
    CachedContact e;
    e.distance = c.distance;
    const std::uint64_t flagged = hit ? 0x11 : 0x01;
    if (sweptA) {
        e.normal = c.normal;   // from the primitive (B) towards the sphere (A)
        e.pointA = onSphere, e.pointB = onPrimitive;
        e.subA = std::uint64_t(indexA) << 32 | 1;
        e.subB = std::uint64_t(indexB) << 32 | flagged;
    } else {
        e.normal = c.normal * -1.0f;   // from the sphere (B) towards the primitive (A)
        e.pointA = onPrimitive, e.pointB = onSphere;
        e.subA = std::uint64_t(indexA) << 32 | flagged;
        e.subB = std::uint64_t(indexB) << 32 | 1;
    }
    out.push_back(e);
    return true;
}

}   // namespace beacon::physics::rigid
