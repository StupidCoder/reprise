// Primitive pairs the physics library solves with dedicated math instead of the convex search
// (findings in .analysis/docs/physics.md, "Primitive pairs"): sphere-sphere 0xe8cbe0, box-sphere
// 0xe870d0, capsule-sphere 0xe878c0, capsule-capsule 0xe875a0. Each returns the distance
// (negative: overlapping), the normal in world space from the first shape towards the second, and
// a point on each shape in that shape's frame. Capsules lie along their x axis.
#pragma once

#include "physics/rigid/ConvexSolver.hpp"

namespace beacon::physics::rigid {

ConvexContact sphereSphere(float radiusA, const Pose& a, float radiusB, const Pose& b);
ConvexContact boxSphere(const math::Vec3& half, const Pose& a, float radius, const Pose& b);
ConvexContact capsuleSphere(float halfLength, float radiusA, const Pose& a, float radiusB, const Pose& b);
// Box against capsule (0xe865f0; separating axes, then the closest features in the chosen face's
// frame, as in the published version).
ConvexContact boxCapsule(const math::Vec3& half, const Pose& a, float halfLength, float radius, const Pose& b);
// Box against box (0xe84080; separating axes over faces and edge pairs, then closest features).
ConvexContact boxBox(const math::Vec3& halfA, const Pose& a, const math::Vec3& halfB, const Pose& b);
ConvexContact capsuleCapsule(float halfLengthA, float radiusA, const Pose& a, float halfLengthB, float radiusB,
                             const Pose& b);

}   // namespace beacon::physics::rigid
