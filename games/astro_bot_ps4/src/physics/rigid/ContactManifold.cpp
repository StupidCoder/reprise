#include "physics/rigid/ContactManifold.hpp"

#include "math/Vector.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::physics::rigid {

using math::Quat;
using math::Vec3;

namespace {

constexpr float kSamePoint = 1e-4f;          // squared, both local points (0x1323d44)
constexpr float kNormalAgree = 0.9f;         // dot^2 (0x1323d48)
constexpr float kSeparation = 0.01f;         // along the normal (0x1323d54)
constexpr float kSideways = 0.002f;          // squared (0x1323d58)

Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = math::cross(u, v) * 2.0f;
    return v + t * q.w + math::cross(u, t);
}

// Largest squared cross product of the two diagonals over the three pairings of four points.
float spread(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    auto sq = [](const Vec3& v) { return math::dot(v, v); };
    const float x = sq(math::cross(a - b, c - d));
    const float y = sq(math::cross(a - c, b - d));
    const float z = sq(math::cross(a - d, b - c));
    return std::max(std::max(x, y), z);
}

}   // namespace

int pointToReplace(const float (&distance)[4], const Vec3 (&point)[4], float newDistance, const Vec3& newPoint) {
    int deepest = -1;
    float least = newDistance;
    for (int i = 0; i < 4; ++i) {
        if (least > distance[i]) {
            deepest = i;
            least = distance[i];
        }
    }
    float area[4] = {};
    for (int i = 0; i < 4; ++i) {
        if (i == deepest) {
            continue;
        }
        Vec3 q[3];
        for (int j = 0, k = 0; j < 4; ++j) {
            if (j != i) {
                q[k++] = point[j];
            }
        }
        area[i] = spread(newPoint, q[0], q[1], q[2]);
    }
    int best = area[1] > area[0] ? 1 : 0;
    for (int i = 2; i < 4; ++i) {
        if (area[i] > area[best]) {
            best = i;
        }
    }
    return best;
}

void ContactManifold::reset(std::uint16_t bodyA, std::uint16_t bodyB) {
    bodyA_ = bodyA;
    bodyB_ = bodyB;
    duration_ = 0;
    count_ = 0;
}

void ContactManifold::addPoint(float distance, const Vec3& normal, const Vec3& localA, const Vec3& localB,
                               const ContactSubData& subData) {
    int id = -1;
    float best = kSamePoint;
    for (int i = 0; i < count_; ++i) {
        const ContactPoint& p = points_[std::size_t(i)];
        const Vec3 da = p.localPointA - localA, db = p.localPointB - localB;
        const float a = math::dot(da, da), b = math::dot(db, db);
        if (b < best && a < best) {
            best = std::max(a, b);
            id = i;
        }
    }
    Vec3 n = normal;
    if (id >= 0) {
        // Same point: keep the solver state; when the normals disagree use their average.
        const Vec3 old = points_[std::size_t(id)].normal();
        const float d = math::dot(normal, old);
        if (d * d < kNormalAgree) {
            const Vec3 sum = normal + old;
            n = sum * (1.0f / math::length(sum));
            distance = math::dot(normal, n) * distance;
        }
    } else {
        if (count_ < kMaxPoints) {
            id = count_++;
        } else {
            float d[4];
            Vec3 a[4];
            for (int i = 0; i < kMaxPoints; ++i) {
                d[i] = points_[std::size_t(i)].distance;
                a[i] = points_[std::size_t(i)].localPointA;
            }
            id = pointToReplace(d, a, distance, localA);
        }
        points_[std::size_t(id)] = ContactPoint{};
    }
    ContactPoint& p = points_[std::size_t(id)];
    p.distance = distance;
    p.subData = subData;
    p.localPointA = localA;
    p.localPointB = localB;
    p.rows[0].normal = n;
}

void ContactManifold::refresh(const Vec3& posA, const Quat& rotA, const Vec3& posB, const Quat& rotB) {
    for (int i = 0; i < count_; ++i) {
        ContactPoint& p = points_[std::size_t(i)];
        const Vec3 a = posA + rotate(rotA, p.localPointA);
        const Vec3 b = posB + rotate(rotB, p.localPointB);
        const float d = math::dot(p.normal(), a - b);
        bool keep = d <= kSeparation;
        if (keep) {
            p.distance = d;
            const Vec3 side = a - p.normal() * d - b;
            keep = math::dot(side, side) <= kSideways;
        }
        if (!keep) {
            points_[std::size_t(i)] = points_[std::size_t(count_ - 1)];
            --count_;
            --i;
            continue;
        }
        if (p.duration < 255) {
            ++p.duration;
        }
    }
    if (count_ == 0) {
        duration_ = 0;
    } else if (duration_ < 0xffff) {
        ++duration_;
    }
}

}   // namespace beacon::physics::rigid
