#include "physics/rigid/ContactCache.hpp"

#include "math/Vector.hpp"
#include "physics/rigid/ContactManifold.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::physics::rigid {

using math::Vec3;

namespace {

constexpr float kSamePoint = 1e-4f;    // squared, both ends (0x1323ce8)
constexpr float kNormalAgree = 0.9f;   // dot^2 (0x1323cec)

}   // namespace

void ContactCache::add(float distance, const Vec3& normal, const Vec3& pointA, const Vec3& pointB,
                       std::uint64_t subA, std::uint64_t subB, const std::array<std::uint32_t, 4>& extra) {
    int id = -1;
    float best = kSamePoint;
    for (int i = 0; i < count_; ++i) {
        const CachedContact& c = points_[std::size_t(i)];
        const Vec3 da = c.pointA - pointA, db = c.pointB - pointB;
        const float a = math::dot(da, da), b = math::dot(db, db);
        if (a < best && b < best) {
            best = std::max(a, b);
            id = i;
        }
    }
    Vec3 n = normal;
    if (id >= 0) {
        const Vec3 old = points_[std::size_t(id)].normal;
        const float d = math::dot(normal, old);
        if (d * d < kNormalAgree) {
            const Vec3 sum = normal + old;
            n = sum * (1.0f / std::sqrt(math::dot(sum, sum)));
            distance *= math::dot(normal, n);
        }
    } else if (count_ < kMaxPoints) {
        id = count_++;
    } else {
        float d[kMaxPoints];
        Vec3 a[kMaxPoints];
        for (int i = 0; i < kMaxPoints; ++i) {
            d[i] = points_[std::size_t(i)].distance;
            a[i] = points_[std::size_t(i)].pointA;
        }
        id = pointToReplace(d, a, distance, pointA);
    }
    CachedContact& c = points_[std::size_t(id)];
    c.distance = distance;
    c.subA = subA;
    c.subB = subB;
    c.extra = extra;
    c.normal = n;
    c.pointA = pointA;
    c.pointB = pointB;
}

void keepSweptCandidate(std::vector<CachedContact>& store, const CachedContact& c) {
    if (store.size() >= 128) {
        return;
    }
    int id = -1;
    float best = 1e-4f;
    for (std::size_t i = 0; i < store.size(); ++i) {
        const Vec3 da = store[i].pointA - c.pointA, db = store[i].pointB - c.pointB;
        const float a = math::dot(da, da), b = math::dot(db, db);
        if (a < best && b < best) {
            best = std::max(a, b);
            id = int(i);
        }
    }
    if (id < 0) {
        store.push_back(c);
    } else if (((c.subA | c.subB) & 0x10) != 0) {
        store[std::size_t(id)] = c;
    }
}

}   // namespace beacon::physics::rigid
