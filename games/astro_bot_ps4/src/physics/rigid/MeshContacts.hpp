// Contacts of a primitive with a large triangle mesh (the physics library's mesh path as linked in
// the game; findings in .analysis/docs/physics.md, "Mesh contacts"):
//
//   mesh tree (0xe8bc80): the primitive's box in the mesh frame against the tree whose leaves are
//     islands; per touched island its own contact cache, then each point moved to world space
//     (normal, distance) and into the result cache;
//   island (0xea1990 for cylinders): the island tree whose leaves are facets collects the facets
//     the box touches, each tested on its own; then the island's points get sub-shape data and go
//     back to the mesh frame;
//   facet (0xe9f940): the triangle extruded to a tetrahedron (apex below its centre by the mean
//     edge length) against the primitive with the convex solver; the normal is kept only near
//     edges marked non-flat, else it is the facet's.
//
// Results: A is the mesh (points in its frame), B the primitive (points in its frame); normals in
// world space from B towards A; distances in world space (negative: overlapping).
#pragma once

#include "data/CollisionMesh.hpp"
#include "physics/rigid/ContactCache.hpp"
#include "physics/rigid/ConvexSolver.hpp"

#include <vector>

namespace beacon::physics::rigid {

// A placed mesh: `pose.rotation` may carry the mesh's scale; `mirrored` when that scale turns the
// mesh inside out (product of the scale factors < 0: the facets' corner order is reversed).
struct MeshPlacement {
    const data::LargeMesh* mesh = nullptr;
    Pose pose;
    bool mirrored = false;
};

// Cylinder (around its x axis) against a mesh.
void cylinderMeshContacts(const MeshPlacement& mesh, float halfLength, float radius, const Pose& cylinder,
                          ContactCache& out);

// Hull (points in its frame) against a mesh (island 0xe9d800, facet 0xe9c130).
void hullMeshContacts(const MeshPlacement& mesh, const math::Vec3* points, int count, const Pose& hull,
                      ContactCache& out);

// One facet against a hull, everything in the mesh frame, the hull at `pose` (0xe9c130).
void hullFacetContact(const SupportShape& hull, const Pose& pose, int facet, const math::Vec3& normal,
                      const math::Vec3& v0, const math::Vec3& v1, const math::Vec3& v2, float thickness,
                      unsigned edgeFlags, ContactCache& out);

// Sphere against a mesh (island 0xea6940, facet 0xea44f0).
void sphereMeshContacts(const MeshPlacement& mesh, float radius, const Pose& sphere, ContactCache& out);

// One facet against a sphere centred at the origin (0xea44f0): rejected above the plane, below it
// by more than the thickness, or beyond an edge plane (tilted down by tilt, radians, when a marked
// edge has one); contact towards the facet's closest point (-normal inside the face or when the
// centre is within 1e-5 squared of it), the facet normal near flat edges as for cylinders.
void sphereFacetContact(float radius, int facet, const math::Vec3& normal, const math::Vec3& v0,
                        const math::Vec3& v1, const math::Vec3& v2, float thickness, const float (&tilt)[3],
                        unsigned edgeFlags, ContactCache& out);

// Box (half extents) against a mesh (island 0xe91dd0, facet 0xe8dcd0).
void boxMeshContacts(const MeshPlacement& mesh, const math::Vec3& half, const Pose& box, ContactCache& out);

// One facet against a box centred at the origin, in the box's frame (0xe8dcd0): separating axes
// between the box and the facet's solid (the triangle down to its thickness, side planes tilted
// by tilt, radians, when a marked edge has one); then the facet moved clear along the deepest axis
// and up to four closest feature pairs, moved back, as the points; the deepest axis as normal (the
// facet's near edges marked flat, as for the others).
void boxFacetContact(const math::Vec3& half, int facet, const math::Vec3& normal, const math::Vec3& v0,
                     const math::Vec3& v1, const math::Vec3& v2, float thickness, const float (&tilt)[3],
                     unsigned edgeFlags, ContactCache& out);

// Capsule (along its x axis) against a mesh (island 0xe99ad0, facet 0xe94940).
void capsuleMeshContacts(const MeshPlacement& mesh, float halfLength, float radius, const Pose& capsule,
                         ContactCache& out);

// One facet against a capsule centred at the origin, in its frame (0xe94940).
void capsuleFacetContact(float halfLength, float radius, int facet, const math::Vec3& normal, const math::Vec3& v0,
                         const math::Vec3& v1, const math::Vec3& v2, float thickness, const float (&tilt)[3],
                         unsigned edgeFlags, ContactCache& out);

// A swept sphere against a mesh (0xe17320 -> 0xe7b5a0 -> island 0xe7f990 -> facet tests
// 0xe7c260 / 0xe7d9b0): candidates for the pair's swept list (see SweptContacts.hpp). The sphere
// is at `current`, its centre moves to `predicted`'s within the step. Entries: A = the mesh (facet
// point in its frame), B = the sphere (point in its frame), normal in world space, distance the
// separation along it; sub-shape word: 0x02 or 0x12 (the centre path meets the facet within the
// radius) | facet << 8 | island << 16, extra[0] = the facet weights, extra[1] = the facet's user word.
void sweptSphereMeshContacts(const MeshPlacement& mesh, float radius, const Pose& current, const Pose& predicted,
                             std::vector<CachedContact>& out);

// The separation of a sphere at the origin from one facet (0xe7c260; exposed for the
// verification tool): distance, contact normal, facet point, sphere point.
void sweptSphereFacet(float radius, const math::Vec3& normal, const math::Vec3& v0, const math::Vec3& v1,
                      const math::Vec3& v2, unsigned edgeFlags, float& distance, math::Vec3& contactNormal,
                      math::Vec3& onFacet, math::Vec3& onSphere);

// One facet against a cylinder, everything in the cylinder's frame (0xe9f940; exposed for the
// verification tool). edgeFlags: the angle types of the edges (v0 v1), (v1 v2), (v2 v0) in bits
// 0-1, 2-3, 4-5.
void cylinderFacetContact(float halfLength, float radius, int facet, const math::Vec3& normal,
                          const math::Vec3& v0, const math::Vec3& v1, const math::Vec3& v2, float thickness,
                          unsigned edgeFlags, ContactCache& out);

}   // namespace beacon::physics::rigid
