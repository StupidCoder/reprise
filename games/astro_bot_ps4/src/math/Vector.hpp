// Vector arithmetic for math::Vec3 and point/quaternion transforms (collision and gameplay code).
#pragma once

#include "math/Transform.hpp"

#include <algorithm>
#include <cmath>

namespace beacon::math {

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { return a = a + b; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { return a = a - b; }
inline Vec3& operator*=(Vec3& a, float s) { return a = a * s; }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a, const Vec3& fallback = {0, 1, 0}) {
    float l = length(a);
    return l > 1e-12f ? a * (1 / l) : fallback;
}
inline Vec3 min(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 max(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline float component(const Vec3& a, int axis) { return axis == 0 ? a.x : axis == 1 ? a.y : a.z; }

// Column-major matrix (m[col * 4 + row]) applied to a point / a direction.
inline Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
inline Vec3 transformDirection(const Mat4& m, const Vec3& d) {
    return {m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z,
            m[2] * d.x + m[6] * d.y + m[10] * d.z};
}

}   // namespace math
