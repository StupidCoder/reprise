// Swept (continuous-collision) contacts of the physics library (findings in
// .analysis/docs/physics.md, "Swept contacts"). A body may carry a swept sphere (shape type 6):
// it takes part in detection only in steps where the pair moves fast (relative speed squared above
// 0.25 or relative spin squared above 0.05, library units) and then never makes ordinary points:
// when the sphere at its current pose is apart from the other shape, the sphere swept from its
// current to its predicted centre (pose + dt x velocity) is tested as a capsule, and the current
// closest pair is kept as a candidate (flag 0x10 when the sweep overlaps). The pair turns the
// candidates into its swept list, which the solver uses as gap-limiting rows (ContactSolver).
#pragma once

#include "data/CollisionMesh.hpp"
#include "physics/rigid/ContactCache.hpp"
#include "physics/rigid/ConvexSolver.hpp"

#include <vector>

namespace beacon::physics::rigid {

// A shape as the swept functions see it.
struct SweptPrimitive {
    enum Kind { Sphere, Box, Capsule, Cylinder, Hull, Mesh, SweptSphere } kind = Sphere;
    float radius = 0, halfLength = 0;
    math::Vec3 halfExtents;
    const data::LargeMesh* mesh = nullptr;   // Mesh
    bool mirrored = false;                   // Mesh: its scale turns it inside out
    const math::Vec3* hull = nullptr;        // Hull points (shape frame)
    int hullCount = 0;
};

// One shape pair with at least one swept sphere (0xe13300 / 0xe13930 sphere, 0xe13f60 /
// 0xe14590 box, 0xe14bd0 / 0xe151f0 capsule, 0xe15830 / 0xe16590 cylinder, 0xe15ed0 / 0xe16c60
// hull, 0xe17320 meshes; two swept spheres: nothing). Candidates as cache entries (points in the
// shapes' frames, normal from B towards A): subA = indexA << 32 | 1, subB = indexB << 32 | 1, the
// partner's word or-ed with 0x10 when the sweep overlaps. Cylinders and hulls go through the
// distance search (separation): a candidate when it reports the shapes apart; the sweep overlaps
// when the search reports overlap (against a cylinder that is the pair's A also when the sweep's
// distance is within 1e-5); against hulls the sphere's point is its surface point along the
// normal. Returns false for partners not handled.
bool sweptShapeContacts(const SweptPrimitive& a, const Pose& currentA, const Pose& predictedA, std::uint32_t indexA,
                        const SweptPrimitive& b, const Pose& currentB, const Pose& predictedB, std::uint32_t indexB,
                        std::vector<CachedContact>& out);

// The capsule a sphere sweeps from `from` to `to` (centre midway, half length |to - from| / 2,
// x axis turned onto the motion; no turn below 1e-5).
Pose sweepPose(const math::Vec3& from, const math::Vec3& to, float& halfLength);

}   // namespace beacon::physics::rigid
