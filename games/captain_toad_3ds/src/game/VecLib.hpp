// Recovered engine vector library (original game, ARM11 main code 0x2fd230..0x2fd410).
//
// The game's own small vector helpers, rewritten with the exact operation order of the original
// VFP code. Why the order matters: the 3DS CPU rounds every float result to 32 bits after each
// multiply and each add. A multiply-accumulate (VMLA/VMLS) is executed as a separate multiply and a
// separate add, as the emulator's JIT implements it. Writing `a + b * c` in C++ and compiling with
// -ffp-contract=off (no fused multiply-add) reproduces this bit for bit. The port's simulation
// replays recorded runs of the original to within float rounding, so every helper here mirrors
// the original's sequence instead of using a "nicer" formula.
#pragma once

#include <cmath>

namespace captain_toad::RecoveredVec {

// A 3-component vector, as stored by the game (x right, y up, z towards the viewer).
struct V3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

// A row-major 3x4 matrix: a 3x3 rotation/scale part and a translation column (m[3], m[7], m[11]).
struct M34 {
    float m[12];
};

// 0x2fd230: component-wise sum.
inline V3 add(const V3& a, const V3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

// 0x2fd24c: component-wise difference.
inline V3 sub(const V3& a, const V3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// 0x2fd268: component-wise product.
inline V3 mulElem(const V3& a, const V3& b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

// 0x2fd330: component-wise quotient.
inline V3 divElem(const V3& a, const V3& b) {
    return {a.x / b.x, a.y / b.y, a.z / b.z};
}

// 0x2fd34c: scale by a number.
inline V3 scale(const V3& a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

// 0x2fd364: divide by a number. The original computes the reciprocal once and multiplies,
// which rounds differently from three divisions.
inline V3 divScalar(const V3& a, float s) {
    const float k = 1.0f / s;
    return {a.x * k, a.y * k, a.z * k};
}

// 0x2fd388: b + a * s ("move b along a by s").
inline V3 addScaled(const V3& a, const V3& b, float s) {
    V3 r = b;
    r.x = r.x + a.x * s;
    r.y = r.y + a.y * s;
    r.z = r.z + a.z * s;
    return r;
}

// Dot product in the form the original inlines everywhere: x*x', then + y*y', then + z*z'.
inline float dot(const V3& a, const V3& b) {
    float v = a.x * b.x;
    v = v + a.y * b.y;
    v = v + a.z * b.z;
    return v;
}

// 0x2fd3a4: normalise in place. A vector of length zero (or NaN) is left unchanged, which the
// game relies on for "no direction" cases such as an idle stick.
inline void normalize(V3& v) {
    float lengthSquared = v.x * v.x;
    lengthSquared = lengthSquared + v.y * v.y;
    lengthSquared = lengthSquared + v.z * v.z;
    const float length = std::sqrt(lengthSquared);
    if (length <= 0.0f) {
        return;
    }
    const float inverse = 1.0f / length;
    v = {v.x * inverse, v.y * inverse, v.z * inverse};
}

// 0x2fd3ec: cross product a x b (first products, then the subtractions).
inline V3 cross(const V3& a, const V3& b) {
    float x = a.y * b.z;
    float y = a.z * b.x;
    float z = a.x * b.y;
    x = x - a.z * b.y;
    y = y - a.x * b.z;
    z = z - a.y * b.x;
    return {x, y, z};
}

// 0x2fd284: matrix times point. Each row starts from its translation and then accumulates
// m0*x, m1*y and m2*z, in that order.
inline V3 mulPoint(const M34& M, const V3& p) {
    const float* m = M.m;
    float x = m[3];
    float y = m[7];
    float z = m[11];
    x = x + m[0] * p.x;
    y = y + m[4] * p.x;
    z = z + m[8] * p.x;
    x = x + m[1] * p.y;
    y = y + m[5] * p.y;
    z = z + m[9] * p.y;
    x = x + m[2] * p.z;
    y = y + m[6] * p.z;
    z = z + m[10] * p.z;
    return {x, y, z};
}

// 0x2fd2f4: matrix times direction (rotation/scale only, no translation).
inline V3 mulDir(const M34& M, const V3& d) {
    const float* m = M.m;
    float x = m[0] * d.x;
    float y = m[4] * d.x;
    float z = m[8] * d.x;
    x = x + m[1] * d.y;
    y = y + m[5] * d.y;
    z = z + m[9] * d.y;
    x = x + m[2] * d.z;
    y = y + m[6] * d.z;
    z = z + m[10] * d.z;
    return {x, y, z};
}

// Inverse of a row-major 3x4 affine matrix (cofactors in double precision).
inline M34 inverseAffine(const M34& w) {
    const float* m = w.m;
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double r[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                         (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    M34 out{};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            out.m[row * 4 + col] = float(r[row * 3 + col]);
        }
        out.m[row * 4 + 3] = float(-(r[row * 3] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]));
    }
    return out;
}

// Product of two row-major 3x4 affine matrices, a x b.
inline M34 mulAffine(const M34& a, const M34& b) {
    M34 o{};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            float v = a.m[r * 4 + 0] * b.m[c] + a.m[r * 4 + 1] * b.m[4 + c] + a.m[r * 4 + 2] * b.m[8 + c];
            if (c == 3) {
                v = v + a.m[r * 4 + 3];
            }
            o.m[r * 4 + c] = v;
        }
    }
    return o;
}

}   // namespace captain_toad::RecoveredVec
