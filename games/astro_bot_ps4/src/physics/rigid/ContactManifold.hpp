// Persistent contact points between two bodies (the physics library's contact manifold, game
// version; findings in .analysis/docs/physics.md, "Contact manifold"). Up to four points per pair,
// each kept in both bodies' local frames so it can be re-checked as the bodies move.
#pragma once

#include "math/Transform.hpp"

#include <array>
#include <cstdint>

namespace beacon::physics::rigid {

// Which sub-shapes produced a point (the library keeps 32 bytes: shape ids and, for meshes, the
// triangle; carried along unchanged).
struct ContactSubData {
    std::uint32_t a[4] = {};
    std::uint32_t b[4] = {};
};

// One solver row of a contact point (0x20 bytes: direction, rhs, inverse effective mass, impulse
// limits, accumulated impulse kept across updates for the warm start).
struct ConstraintRow {
    math::Vec3 normal;
    float rhs = 0;
    float jacDiagInv = 0;
    float lowerLimit = 0, upperLimit = 1e38f;
    float accumImpulse = 0;
};

struct ContactPoint {
    std::uint8_t duration = 0;   // updates the point has survived (capped at 255)
    ContactSubData subData;
    float distance = 1e38f;      // signed separation along the normal (negative: penetrating)
    math::Vec3 localPointA, localPointB;
    // Rows: 0 along the contact normal (world space, from B towards A), 1 and 2 friction.
    ConstraintRow rows[3];
    const math::Vec3& normal() const { return rows[0].normal; }
};

// Which of four kept points a new one replaces (0xe0c900 for manifolds, 0xe0bb50 for the
// per-pair contact cache): never the deepest (the new point counts, then nothing is "deepest"
// among the old ones); of the others the one whose removal leaves the four points (with the new
// one) spanning the most, measured as the largest |d1 x d2|^2 over the three ways of pairing the
// four points into two diagonals. Ties go to the lower index.
int pointToReplace(const float (&distance)[4], const math::Vec3 (&point)[4], float newDistance,
                   const math::Vec3& newPoint);

class ContactManifold {
public:
    static constexpr int kMaxPoints = 4;

    void reset(std::uint16_t bodyA, std::uint16_t bodyB);
    std::uint16_t bodyA() const { return bodyA_; }
    std::uint16_t bodyB() const { return bodyB_; }
    int size() const { return count_; }
    const ContactPoint& point(int i) const { return points_[std::size_t(i)]; }
    ContactPoint& point(int i) { return points_[std::size_t(i)]; }
    std::uint16_t duration() const { return duration_; }

    // A newly detected point (0xe0cee0): an existing point whose local points both lie within
    // 1e-4 (squared) takes it over, keeping its solver state (the normal is averaged with the old
    // one when they differ, dot^2 < 0.9, and the distance projected onto the average); else it is
    // appended, or replaces the point chosen by sort4 when four are kept (that slot is reset).
    void addPoint(float distance, const math::Vec3& normal, const math::Vec3& localA, const math::Vec3& localB,
                  const ContactSubData& subData);

    // Re-checks the points against the bodies' poses (0xe0d6e0): a point separated by more than
    // 0.01 along its normal, or whose two ends drifted more than sqrt(0.002) apart sideways, is
    // removed (the last point takes its place); the others get the new distance and age by one.
    // Units are the library's (world scale 3).
    void refresh(const math::Vec3& posA, const math::Quat& rotA, const math::Vec3& posB, const math::Quat& rotB);

private:

    std::uint16_t bodyA_ = 0, bodyB_ = 0;
    std::uint16_t duration_ = 0;
    int count_ = 0;
    std::array<ContactPoint, kMaxPoints> points_{};
};

}   // namespace beacon::physics::rigid
