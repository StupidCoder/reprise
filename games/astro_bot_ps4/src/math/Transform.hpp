// Small transform math shared by data readers, animation and rendering: vectors, quaternions
// (x, y, z, w) and 4x4 matrices stored column-major for column vectors (m[col * 4 + row]).
#pragma once

#include <array>
#include <cmath>

namespace beacon::math {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

using Mat4 = std::array<float, 16>;

inline Mat4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) {
                s += a[k * 4 + row] * b[c * 4 + k];
            }
            r[c * 4 + row] = s;
        }
    }
    return r;
}

// Translation * rotation * scale.
inline Mat4 compose(const Quat& q, const Vec3& t, const Vec3& s) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 m{};
    m[0] = (1 - 2 * (y * y + z * z)) * s.x;
    m[1] = 2 * (x * y + z * w) * s.x;
    m[2] = 2 * (x * z - y * w) * s.x;
    m[4] = 2 * (x * y - z * w) * s.y;
    m[5] = (1 - 2 * (x * x + z * z)) * s.y;
    m[6] = 2 * (y * z + x * w) * s.y;
    m[8] = 2 * (x * z + y * w) * s.z;
    m[9] = 2 * (y * z - x * w) * s.z;
    m[10] = (1 - 2 * (x * x + y * y)) * s.z;
    m[12] = t.x;
    m[13] = t.y;
    m[14] = t.z;
    m[15] = 1;
    return m;
}

// Inverse of an affine matrix (rotation/scale/translation, last row 0 0 0 1).
inline Mat4 inverseAffine(const Mat4& m) {
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const float inv = det != 0 ? 1 / det : 0;
    Mat4 r{};
    r[0] = (e * i - f * h) * inv;
    r[4] = (c * h - b * i) * inv;
    r[8] = (b * f - c * e) * inv;
    r[1] = (f * g - d * i) * inv;
    r[5] = (a * i - c * g) * inv;
    r[9] = (c * d - a * f) * inv;
    r[2] = (d * h - e * g) * inv;
    r[6] = (b * g - a * h) * inv;
    r[10] = (a * e - b * d) * inv;
    for (int row = 0; row < 3; ++row) {
        r[12 + row] = -(r[row] * m[12] + r[4 + row] * m[13] + r[8 + row] * m[14]);
    }
    r[15] = 1;
    return r;
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Normalised linear interpolation along the shorter arc.
inline Quat nlerp(const Quat& a, Quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    Quat q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 0) {
        q = {q.x / len, q.y / len, q.z / len, q.w / len};
    }
    return q;
}

// Spherical interpolation along the shorter arc.
inline Quat slerp(const Quat& a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return nlerp(a, b, t);
    }
    const float th = std::acos(d), s = std::sin(th);
    const float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

}   // namespace math
