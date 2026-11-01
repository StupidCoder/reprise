// =============================================================================================
// Matrix.hpp - 4x4 transforms for the renderers (column-major, OpenGL conventions)
// =============================================================================================
//
// The renderers used to build their view and projection on OpenGL's fixed-function matrix
// stack. These are the same operations on explicit matrices: frustum, ortho, translate, rotate
// and products in double precision, column-major (m[column * 4 + row]) with the standard OpenGL
// formulas, so a draw's matrices can be handed to any backend as shader uniforms.
//
// What the shaders receive was measured against the fixed-function path (render regression on
// the opening and the courses): the clip transform must be one matrix, model-view-projection,
// multiplied in single precision from the float projection and model-view (column by column,
// summing over k in ascending order); computing projection * (model-view * position) in the
// shader instead moves depth by enough to change which of two coplanar surfaces wins. With the
// driver's own matrices that product reproduced its clip positions exactly.
// The macOS driver builds its matrices in single precision by formulas that none of the obvious
// float or double variants reproduce bit for bit (a probe of glFrustum / glRotate / products
// against these found last-bit differences in most elements), and its normal matrix likewise;
// the double-precision versions here leave a few pixels per image 1/255 off the old output.
// =============================================================================================
#pragma once

#include <array>
#include <cmath>

namespace captain_toad::gpu {

struct Mat4 {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return {}; }
    double& operator()(int row, int column) { return m[size_t(column * 4 + row)]; }
    double operator()(int row, int column) const { return m[size_t(column * 4 + row)]; }

    friend Mat4 operator*(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int c = 0; c < 4; c++) {
            for (int row = 0; row < 4; row++) {
                double v = 0;
                for (int k = 0; k < 4; k++) {
                    v += a(row, k) * b(k, c);
                }
                r(row, c) = v;
            }
        }
        return r;
    }

    // From 16 column-major values.
    template <typename T>
    static Mat4 fromColumnMajor(const T* v) {
        Mat4 r;
        for (int i = 0; i < 16; i++) {
            r.m[size_t(i)] = double(v[i]);
        }
        return r;
    }
    // From a 3x4 row-major affine matrix (rotation and translation; the game's matrices).
    template <typename T>
    static Mat4 fromRows34(const T* v) {
        Mat4 r;
        for (int row = 0; row < 3; row++) {
            for (int c = 0; c < 4; c++) {
                r(row, c) = double(v[row * 4 + c]);
            }
        }
        return r;
    }

    // glFrustum, glOrtho, glTranslate, glScale and glRotate (angle in degrees) as matrices.
    static Mat4 frustum(double l, double r, double b, double t, double n, double f) {
        Mat4 x;
        x(0, 0) = 2 * n / (r - l);
        x(1, 1) = 2 * n / (t - b);
        x(0, 2) = (r + l) / (r - l);
        x(1, 2) = (t + b) / (t - b);
        x(2, 2) = -(f + n) / (f - n);
        x(3, 2) = -1;
        x(2, 3) = -2 * f * n / (f - n);
        x(3, 3) = 0;
        return x;
    }
    static Mat4 ortho(double l, double r, double b, double t, double n, double f) {
        Mat4 x;
        x(0, 0) = 2 / (r - l);
        x(1, 1) = 2 / (t - b);
        x(2, 2) = -2 / (f - n);
        x(0, 3) = -(r + l) / (r - l);
        x(1, 3) = -(t + b) / (t - b);
        x(2, 3) = -(f + n) / (f - n);
        return x;
    }
    static Mat4 translation(double x, double y, double z) {
        Mat4 t;
        t(0, 3) = x;
        t(1, 3) = y;
        t(2, 3) = z;
        return t;
    }
    static Mat4 scaling(double x, double y, double z) {
        Mat4 s;
        s(0, 0) = x;
        s(1, 1) = y;
        s(2, 2) = z;
        return s;
    }
    static Mat4 rotation(double degrees, double x, double y, double z) {
        const double len = std::sqrt(x * x + y * y + z * z);
        x /= len;
        y /= len;
        z /= len;
        const double a = degrees * M_PI / 180, c = std::cos(a), s = std::sin(a), d = 1 - c;
        Mat4 r;
        r(0, 0) = x * x * d + c;
        r(0, 1) = x * y * d - z * s;
        r(0, 2) = x * z * d + y * s;
        r(1, 0) = y * x * d + z * s;
        r(1, 1) = y * y * d + c;
        r(1, 2) = y * z * d - x * s;
        r(2, 0) = x * z * d - y * s;
        r(2, 1) = y * z * d + x * s;
        r(2, 2) = z * z * d + c;
        return r;
    }

    std::array<float, 16> toFloat() const {
        std::array<float, 16> f;
        for (int i = 0; i < 16; i++) {
            f[size_t(i)] = float(m[size_t(i)]);
        }
        return f;
    }
};

// The clip transform as the shaders receive it: the float product projection x model-view.
inline std::array<float, 16> modelViewProjection(const Mat4& projection, const Mat4& modelView) {
    const std::array<float, 16> p = projection.toFloat(), mv = modelView.toFloat();
    std::array<float, 16> r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float v = 0;
            for (int k = 0; k < 4; k++) {
                v += p[size_t(k * 4 + row)] * mv[size_t(c * 4 + k)];
            }
            r[size_t(c * 4 + row)] = v;
        }
    }
    return r;
}

// The normal matrix: inverse transpose of the model-view's upper 3x3 (column-major 3x3).
inline std::array<float, 9> normalMatrix(const Mat4& mv) {
    auto a = [&](int r, int c) {
        return mv(r, c);
    };
    const double det = a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) -
                       a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0)) +
                       a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0));
    std::array<float, 9> n;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            const int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (c + 1) % 3, c2 = (c + 2) % 3;
            // The cofactor of (r, c) / det is the inverse transpose's element (r, c).
            n[size_t(c * 3 + r)] = float((a(r1, c1) * a(r2, c2) - a(r1, c2) * a(r2, c1)) / det);
        }
    }
    return n;
}

}   // namespace captain_toad::gpu
