// Casts of a sphere (a ray with a radius; radius 0: a plain ray) against one shape, as the
// physics library does for the game's queries (findings in .analysis/docs/physics.md, "Casts").
// The cast table 0x17204f0 has one function per shape type: sphere 0xe4caa0, box 0xea9590,
// capsule 0xea9950, cylinder 0xeaa9d0, hull 0xe3d3c0, large mesh 0xe4ca40; swept spheres
// (type 6) are never hit.
//
// The ray moves from `start` by `direction` (the whole movement: t runs from 0 to 1). A cast
// replaces `result` only when it hits at t below result.t (start it at 1 for a fresh query).
// Result: t, the contact point on the shape's surface and the normal from the shape towards the
// ray (both in the frame the pose maps into); a start already within the radius gives t = 0.
#pragma once

#include "physics/rigid/ConvexSolver.hpp"
#include "physics/rigid/MeshContacts.hpp"

#include <cstdint>

namespace beacon::physics::rigid {

struct CastRay {
    math::Vec3 start, direction;
    float radius = 0;
};

struct CastResult {
    math::Vec3 point, normal;
    float t = 1;
    bool hit = false;
    int island = -1, facet = -1;   // mesh hits: the facet
};

// Ray helpers in the shape's frame (exposed for the verification tool). Each reports the first
// t in [0, 1] where the ray, grown by `radius`, meets the shape (t = 0 when it starts inside).
bool raySphere(float radius, const math::Vec3& start, const math::Vec3& dir, const math::Vec3& centre, float& t);
bool raySegment(float radius, const math::Vec3& start, const math::Vec3& dir, const math::Vec3& a,
                const math::Vec3& b, float& t);   // a capsule from a to b
bool rayBox(float radius, const math::Vec3& start, const math::Vec3& dir, const math::Vec3& half, float& t);

// Closest point on a solid cylinder's surface (axis a to b) to p, with the outward normal
// (0xea8a80).
void cylinderSurface(float radius, const math::Vec3& p, const math::Vec3& a, const math::Vec3& b, math::Vec3& point,
                     math::Vec3& normal);
// A cylinder from a to b grown by `rayRadius` (0xeaa360).
bool rayCylinder(float rayRadius, float radius, const math::Vec3& start, const math::Vec3& dir, const math::Vec3& a,
                 const math::Vec3& b, float& t);

bool castSphere(float radius, const Pose& pose, const CastRay& ray, CastResult& result);
bool castBox(const math::Vec3& half, const Pose& pose, const CastRay& ray, CastResult& result);
// Capsule along its x axis.
bool castCapsule(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result);
// Cylinder along its x axis.
bool castCylinder(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result);

// A convex hull as the casts see it: points and triangles (three vertex indices each, in the
// shape's frame); `mirrored` when the shape's scale turns it inside out (corners 0 and 2 of every
// triangle swapped).
struct HullGeometry {
    const math::Vec3* points = nullptr;
    int pointCount = 0;
    const std::uint32_t* indices = nullptr;
    int indexCount = 0;
    bool mirrored = false;
};

// 0xe3d3c0: the ray's sphere swept against the hull with the convex cast (castConvex); when they
// already overlap at the start, the closest point of the facets the start lies in front of
// (none, e.g. starting deep inside: no hit) with t = 0, regardless of result.t. The pose may
// carry the shape's scale.
bool castHull(const HullGeometry& hull, const Pose& pose, const CastRay& ray, CastResult& result);

// A sphere swept against one triangle (0xe3ce00): t = 0 when the start is within the radius
// (+1e-5) of it (`distance2` the squared distance), else the face moved out by the radius along
// its normal, else the three edges as capsules (`distance2` the radius squared).
bool sphereTriangle(float radius, const math::Vec3& start, const math::Vec3& dir, const math::Vec3& a,
                    const math::Vec3& b, const math::Vec3& c, float& t, float& distance2);

// 0xe3e620 -> 0xe46180 (layout-2 meshes; islands 0xe43f70): the mesh and island trees walked
// with the ray against their boxes grown by the radius (in the mesh frame, the radius divided by
// the smallest scale), dropping boxes entered no earlier than the best hit; the facets the start
// lies in front of are swept in world space (sphereTriangle). Of hits within 1e-5 of each other
// the most head-on facet wins (smallest normal . direction); a start touching a facet (t = 0)
// keeps the nearest. The result is the hit facet's closest point to the swept centre at t, the
// normal from it towards the centre (the facet's normal when the centre is on it).
bool castMesh(const MeshPlacement& mesh, const CastRay& ray, CastResult& result);

// Ray casts (the module's ray query 0xcb8910 -> world 0xcf7900 -> per body 0xdc2d80 -> the ray
// table 0x1720580: sphere 0xe67210, box 0xeaacd0, capsule 0xeaafc0, cylinder 0xeabec0, hull
// 0xeab470, mesh 0xe5bf00); the ray's radius is ignored. Unlike the sphere casts a ray starting
// inside a primitive misses it. The result's point is start + t direction.
//   sphere: the quadratic (t in [0, 1]); normal from the centre (-direction / |direction|^2 when the
//     point is within 1e-5 squared of it);
//   box: slabs in the box's frame (parallel within 1e-5: outside the slab misses); entry axis of
//     the latest entry (ties: y over x, z over both); t > 0; normal -sign(d) along that axis;
//   capsule: the side (0 <= t <= 1, |x| <= half), else the end spheres (the nearer valid one);
//   cylinder: the side (as the capsule; normal from the axis, -direction normalized on it), else
//     the nearer cap plane (t in [0, 1], inside the radius; normal +-x);
//   hull: in its frame (inverse of the pose), front facets only (Moller-Trumbore, barycentric
//     tolerance 1e-5, t in [0, 1]); normal the facet's through the inverse transpose.
bool rayCastSphere(float radius, const Pose& pose, const CastRay& ray, CastResult& result);
bool rayCastBox(const math::Vec3& half, const Pose& pose, const CastRay& ray, CastResult& result);
bool rayCastCapsule(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result);
bool rayCastCylinder(float halfLength, float radius, const Pose& pose, const CastRay& ray, CastResult& result);
bool rayCastHull(const HullGeometry& hull, const Pose& pose, const CastRay& ray, CastResult& result);
// Mesh (0xe5bf00, islands e.g. 0xe66990): the trees walked with the plain ray (boxes entered before
// the best hit), front facets only (Moller-Trumbore, barycentric tolerance 1e-5, t in [0, 1]), the
// nearest strictly; normal: the facet's.
bool rayCastMesh(const MeshPlacement& mesh, const CastRay& ray, CastResult& result);

// Casts of a capsule (the module's capsule query 0xcb9190 -> world 0xcf87d0 -> per body 0xdbc4c0
// -> the convex cast table 0x1720460: sphere 0xe28dc0, box 0xe29410, capsule 0xe29cb0, cylinder
// 0xe2a4c0, hull 0xe2abb0, mesh 0xe2baf0). The capsule lies along its x axis turned by `rotation`
// and moves from `start` by `direction`; the result as for the ray casts, but:
//   moving into the shape: the convex cast (castConvex) of the capsule against the shape; point =
//     the capsule's touching point; normal = minus the capsule's outward normal there (when that
//     point lies on the capsule's axis: (0, 0, -1), unturned);
//   overlapping at the start: the shape's point p nearest the capsule's centre (sphere: towards it;
//     box: clamped, or pushed to the nearest face from inside; capsule: from its axis; cylinder:
//     its closest surface point 0xea8a80; hull: the nearest facet point as for the ray cast) and
//     c, the point of the capsule's axis nearest p: normal from p towards c, point p, or the
//     capsule's surface towards p when p is outside the capsule (t = 0).
struct CapsuleRay {
    math::Vec3 start, direction;
    Mat3 rotation;
    float radius = 0, halfLength = 0;
};
bool capsuleCastSphere(float radius, const Pose& pose, const CapsuleRay& ray, CastResult& result);
bool capsuleCastBox(const math::Vec3& half, const Pose& pose, const CapsuleRay& ray, CastResult& result);
bool capsuleCastCapsule(float halfLength, float radius, const Pose& pose, const CapsuleRay& ray, CastResult& result);
bool capsuleCastCylinder(float halfLength, float radius, const Pose& pose, const CapsuleRay& ray, CastResult& result);
bool capsuleCastHull(const HullGeometry& hull, const Pose& pose, const CapsuleRay& ray, CastResult& result);
// 0xe2baf0 -> 0xe2f560 (islands 0xe2d330, facets 0xe2bdc0): as castMesh, with the tree boxes grown
// by (the larger of the capsule half axis's parts along and across the movement + radius) / the
// smallest scale; per facet the convex cast of the capsule against the triangle (overlapping: the
// triangle's point nearest the capsule's centre, t = 0); the result is the hit facet's point
// nearest the capsule's centre at t, the normal from it towards the centre (undefined when the
// centre lies on the facet: the module then reports no hit).
bool capsuleCastMesh(const MeshPlacement& mesh, const CapsuleRay& ray, CastResult& result);

}   // namespace beacon::physics::rigid
