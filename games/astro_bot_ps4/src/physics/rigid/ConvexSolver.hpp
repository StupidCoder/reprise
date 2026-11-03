// Penetration of two convex shapes (the physics library's convex solver as linked in the game;
// findings in .analysis/docs/physics.md, "Convex solver"). A portal search on the Minkowski
// difference A - B (Minkowski portal refinement): the shapes are seen only through their support
// points and an interior point, so any convex primitive can take part.
//
// Units are the library's (world scale 3). Results are in A's frame, except pointB (B's frame).
#pragma once

#include "math/Vector.hpp"
#include "physics/rigid/RigidBody.hpp"

#include <optional>

namespace beacon::physics::rigid {

// A convex shape as the solver sees it.
class SupportShape {
public:
    virtual ~SupportShape() = default;
    // The shape's farthest point along a unit direction (shape space).
    virtual math::Vec3 support(const math::Vec3& dir) const = 0;
    // A point inside the shape.
    virtual math::Vec3 centre() const = 0;
};

// Cylinder around the x axis (support 0xe22a80, centre 0xe22fa0).
class CylinderSupport : public SupportShape {
public:
    CylinderSupport(float halfLength, float radius) : halfLength_(halfLength), radius_(radius) {}
    math::Vec3 support(const math::Vec3& dir) const override;
    math::Vec3 centre() const override { return {}; }

private:
    float halfLength_, radius_;
};

// Sphere (support 0xe22a60, centre 0xe22f90).
class SphereSupport : public SupportShape {
public:
    explicit SphereSupport(float radius) : radius_(radius) {}
    math::Vec3 support(const math::Vec3& dir) const override { return dir * radius_; }
    math::Vec3 centre() const override { return {}; }

private:
    float radius_;
};

// Box of half extents (support 0xe22970: the corner on dir's side, the negative one where a
// component of dir is 0; centre 0xe22f70).
class BoxSupport : public SupportShape {
public:
    explicit BoxSupport(const math::Vec3& half) : half_(half) {}
    math::Vec3 support(const math::Vec3& dir) const override;
    math::Vec3 centre() const override { return {}; }

private:
    math::Vec3 half_;
};

// Capsule around the x axis (support 0xe229f0, centre 0xe22f80).
class CapsuleSupport : public SupportShape {
public:
    CapsuleSupport(float halfLength, float radius) : halfLength_(halfLength), radius_(radius) {}
    math::Vec3 support(const math::Vec3& dir) const override;
    math::Vec3 centre() const override { return {}; }

private:
    float halfLength_, radius_;
};

// Convex hull of points (support 0xe22750: the first point farthest along dir; centre 0xe22e50:
// the points' average).
class HullSupport : public SupportShape {
public:
    HullSupport(const math::Vec3* points, int count) : points_(points), count_(count) {}
    math::Vec3 support(const math::Vec3& dir) const override;
    math::Vec3 centre() const override;

private:
    const math::Vec3* points_;
    int count_;
};

// Hull of four points (support 0xe224c0: the first point farthest along dir; centre 0xe22f20: the
// average). The triangle contacts use it for a triangle extruded to a tetrahedron.
class TetraSupport : public SupportShape {
public:
    explicit TetraSupport(const math::Vec3 (&points)[4]);
    math::Vec3 support(const math::Vec3& dir) const override;
    math::Vec3 centre() const override;

private:
    math::Vec3 points_[4];
};

// A triangle (support 0xe22420: of equal projections the later corner).
class TriangleSupport : public SupportShape {
public:
    TriangleSupport(const math::Vec3& a, const math::Vec3& b, const math::Vec3& c) : points_{a, b, c} {}
    math::Vec3 support(const math::Vec3& dir) const override;
    math::Vec3 centre() const override { return (points_[0] + points_[1] + points_[2]) * (1.0f / 3.0f); }

private:
    math::Vec3 points_[3];
};

struct Pose {
    Mat3 rotation;   // shape space to world
    math::Vec3 translation;
};

struct ConvexContact {
    float distance = 0;   // <= 0 while overlapping (the depth along normal)
    math::Vec3 normal;    // A's frame, unit, from A towards B
    math::Vec3 pointA;    // on A, A's frame
    math::Vec3 pointB;    // on B, B's frame
};

// The two entry points of the library: the full search (0xe20d50: after the first portal, up to
// three more started from its side faces, the shallowest kept; primitive pairs) and the single
// search (0xe1f9a0: triangle contacts). No result when the shapes are apart, their interior points
// coincide, or the search degenerates.
std::optional<ConvexContact> penetrationFull(const SupportShape& a, const Pose& poseA, const SupportShape& b,
                                             const Pose& poseB);
std::optional<ConvexContact> penetrationSingle(const SupportShape& a, const Pose& poseA, const SupportShape& b,
                                               const Pose& poseB);

// Separation of two convex shapes (the library's distance search 0xe1d110, used by the swept
// contacts): a point-to-origin search on A - B in A's frame. Status 0: apart, with the distance
// along the normal (unit, A's frame, from B towards A) between the closest points (A's and B's
// frames); 1: overlapping (the simplex grew to four points, or the closest point fell within
// 1e-6 of the origin); 3: the simplex step failed.
struct Separation {
    int status = 3;
    float distance = 0;
    math::Vec3 normal{1, 0, 0};
    math::Vec3 pointA, pointB;
};
Separation separation(const SupportShape& a, const Pose& poseA, const SupportShape& b, const Pose& poseB);

// A convex shape B moving by `motion` (world) against a fixed convex shape A (0xe1dcb0, the
// library's ray cast against general convex shapes, used by the hull cast): the first time t in
// [0, 1) at which they touch. Status 0: touching at t (> 0; the normal from the search, A's
// frame; the point on A in A's frame; the point on B at its start pose in B's frame); 1: already
// overlapping at the start (t = 0, nothing else reported); 2: no contact within the motion;
// 3: the simplex step failed.
struct ConvexCast {
    int status = 2;
    float t = 0;
    math::Vec3 normal{1, 0, 0};
    math::Vec3 pointA, pointB;
};
ConvexCast castConvex(const SupportShape& a, const Pose& poseA, const SupportShape& b, const Pose& poseB,
                      const math::Vec3& motion);

// One simplex step of that search (0xe1b870; exposed for the verification tool): the points
// w[0, count), v the current closest point (kept when no candidate wins). Returns the new count;
// ids: the input point in each kept slot; weights: their barycentric weights.
int separationStep(const math::Vec3* w, int count, math::Vec3& v, float (&weights)[4], int (&ids)[4]);

}   // namespace beacon::physics::rigid
