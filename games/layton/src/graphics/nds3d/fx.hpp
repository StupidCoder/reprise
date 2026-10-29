#pragma once
#include "graphics/nds3d/gx.hpp"
#include <array>
#include <cstdint>

// Fixed-point helpers with the rounding of the ARM9 math library the G3D
// runtime calls (hardware divider 0x04000280 and square root 0x040002b0).
// ARM9 addresses are given per function.
namespace picarat::nds3d {
struct Vec {
    fx32 x = 0, y = 0, z = 0;
};

// Hardware divider / square root results.
std::int64_t hwDivide64(std::int64_t numerator, std::int64_t denominator); // DIV modes 1/2 quotient
std::uint32_t hwSqrt64(std::uint64_t value);                               // SQRT mode 1

inline fx32 fxMul(fx32 a, fx32 b) {
    return fx32((std::int64_t(a) * b) >> 12);
} // truncating

inline fx32 fxMulRound(fx32 a, fx32 b) {
    return fx32((std::int64_t(a) * b + 0x800) >> 12);
} // FX_Mul (0x800 bias)

fx32 fxDiv(fx32 numerator, fx32 denominator); // 0201a2cc: (n << 32) / d, rounded to 20.12
fx32 fxSqrt(fx32 value);                      // 0201a2dc
fx32 vecDot(const Vec& a, const Vec& b);      // 0201a4d0
Vec vecCross(const Vec& a, const Vec& b);     // 0201a50c
fx32 vecMag(const Vec& v);                    // 0201a590
Vec vecNormalize(const Vec& v);               // 0201a5f0

inline Vec vecAdd(const Vec& a, const Vec& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
} // 0201a468

inline Vec vecSub(const Vec& a, const Vec& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
} // 0201a49c

fx32 vecDistance(const Vec& a, const Vec& b); // 0201a708
std::int16_t atan2Index(fx32 y, fx32 x);      // 0201a784: radians in 20.12 (pi = 0x3244)
// FX_SinCosTable (0203e4f4): 4096 (sin, cos) pairs, round(sin(i*2pi/4096)*4096).
std::pair<std::int16_t, std::int16_t> sinCos(unsigned index);
// Matrices (row-vector convention as the hardware loads them).
Matrix lookAt(const Vec& eye, const Vec& up, const Vec& at); // 02019688 (4x3 in a 4x4)
Matrix perspective(fx32 fovySin, fx32 fovyCos, fx32 aspect, fx32 n, fx32 f,
                   fx32 scaleW);                                                  // 02019f78
Matrix orthographic(fx32 t, fx32 b, fx32 l, fx32 r, fx32 n, fx32 f, fx32 scaleW); // 0201a0c4
} // namespace picarat::nds3d
