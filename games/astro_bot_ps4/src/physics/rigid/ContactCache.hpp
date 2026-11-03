// Contact points of one detection (the physics library's per-pair contact cache, 0xe0c0e0; the
// results then go into the pair's persistent manifold). At most four points: a point close to a
// kept one (both ends within 1e-4, squared) replaces it, its normal averaged with the old one
// when they disagree (dot^2 < 0.9) and the distance projected onto the average; a fifth point
// replaces the one pointToReplace picks.
//
// Normals point from B towards A; points are each in their own shape's frame.
#pragma once

#include "math/Transform.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace beacon::physics::rigid {

struct CachedContact {
    float distance = 1e38f;
    std::uint64_t subA = 0, subB = 0;          // which sub-shapes touched (meshes: facet, island)
    std::array<std::uint32_t, 4> extra{};      // caller data carried along
    math::Vec3 normal, pointA, pointB;
};

class ContactCache {
public:
    static constexpr int kMaxPoints = 4;

    void clear() { count_ = 0; }
    int size() const { return count_; }
    const CachedContact& operator[](int i) const { return points_[std::size_t(i)]; }

    void add(float distance, const math::Vec3& normal, const math::Vec3& pointA, const math::Vec3& pointB,
             std::uint64_t subA, std::uint64_t subB, const std::array<std::uint32_t, 4>& extra = {});

private:
    std::array<CachedContact, kMaxPoints> points_{};
    int count_ = 0;
};

// The swept-candidate list of a contact cache (0xe0c630, up to 128 entries): a candidate whose
// two points both lie within 1e-4 (squared) of a kept one replaces it when it carries the sweep
// flag (0x10 in the low byte of either sub-shape word), else it is dropped; others are appended.
void keepSweptCandidate(std::vector<CachedContact>& store, const CachedContact& c);

}   // namespace beacon::physics::rigid
