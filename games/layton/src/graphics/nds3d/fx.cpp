// Fixed-point maths of the 3D library. See fx.hpp.
#include "graphics/nds3d/fx.hpp"
#include <cmath>

namespace picarat::nds3d {
std::int64_t hwDivide64(std::int64_t n, std::int64_t d) {
    if (d == 0) {
        return n < 0 ? 1
                     : -1; // hardware result on division by zero (sign of the numerator inverted)
    }
    if (n == INT64_MIN && d == -1) {
        return n;
    }
    return n / d;
}

std::uint32_t hwSqrt64(std::uint64_t v) {
    std::uint64_t r = std::uint64_t(std::sqrt(double(v)));
    while (r * r > v) {
        --r;
    }
    while ((r + 1) * (r + 1) <= v) {
        ++r;
    }
    return std::uint32_t(r);
}

fx32 fxDiv(fx32 n, fx32 d) {
    const std::int64_t q =
        hwDivide64(std::int64_t(n) * (std::int64_t(1) << 32), d); // mode 1: 64/32
    return fx32((q + 0x80000) >> 20);
}

fx32 fxSqrt(fx32 v) {
    if (v <= 0) {
        return v;
    }
    return fx32((std::uint64_t(hwSqrt64(std::uint64_t(std::uint32_t(v)) << 32)) + 0x200) >> 10);
}

fx32 vecDot(const Vec& a, const Vec& b) {
    return fx32(
        (std::int64_t(a.x) * b.x + std::int64_t(a.y) * b.y + std::int64_t(a.z) * b.z + 0x800) >>
        12);
}

Vec vecCross(const Vec& a, const Vec& b) {
    return {fx32((std::int64_t(a.y) * b.z - std::int64_t(a.z) * b.y + 0x800) >> 12),
            fx32((std::int64_t(a.z) * b.x - std::int64_t(a.x) * b.z + 0x800) >> 12),
            fx32((std::int64_t(a.x) * b.y - std::int64_t(a.y) * b.x + 0x800) >> 12)};
}

fx32 vecMag(const Vec& v) {
    const std::int64_t s =
        std::int64_t(v.x) * v.x + std::int64_t(v.y) * v.y + std::int64_t(v.z) * v.z;
    return fx32((std::uint64_t(hwSqrt64(std::uint64_t(s) * 4)) + 1) >> 1);
}

fx32 vecDistance(const Vec& a, const Vec& b) {
    return vecMag(vecSub(a, b));
}

Vec vecNormalize(const Vec& v) {
    // 2^56 / |v|^2 (64/64 division) times sqrt(4|v|^2), then (v * that) >> 32, rounded to .12.
    const std::int64_t s =
        std::int64_t(v.x) * v.x + std::int64_t(v.y) * v.y + std::int64_t(v.z) * v.z;
    const std::int64_t q = hwDivide64(std::int64_t(1) << 56, s);
    const std::uint32_t r = hwSqrt64(std::uint64_t(s) * 4);
    const std::uint64_t sq = std::uint64_t(r) * std::uint64_t(q);
    auto one = [&](fx32 c) {
        const std::int32_t hi =
            std::int32_t(std::uint64_t(std::int64_t(c) * std::int64_t(sq)) >> 32);
        return fx32((hi + 0x1000) >> 13);
    };
    return {one(v.x), one(v.y), one(v.z)};
}

std::pair<std::int16_t, std::int16_t> sinCos(unsigned i) {
    i &= 4095;
    const double a = double(i) * 2.0 * 3.14159265358979323846 / 4096.0;
    return {std::int16_t(std::lround(std::sin(a) * 4096.0)),
            std::int16_t(std::lround(std::cos(a) * 4096.0))};
}

std::int16_t atan2Index(fx32 p1, fx32 p2) {
    // FX_AtanTable (020424f4): round(atan(i/128) * 4096), i = 0..128.
    auto table = [](int i) {
        return int(std::lround(std::atan(double(i) / 128.0) * 4096.0));
    };
    int smaller, larger, base;
    bool add;
    if (p1 < 1) {
        if (p1 >= 0) {
            return std::int16_t(p2 >= 0 ? 0 : 0x3244);
        }
        smaller = -p1;
        larger = smaller;
        base = -0x1922;
        if (p2 < 0) {
            const int m = -p2;
            if (p1 == p2 || m < smaller) {
                if (smaller <= m) {
                    return std::int16_t(-0x25b3);
                }
                add = false;
                smaller = m;
            } else {
                add = true;
                larger = m;
                base = -0x3244;
            }
        } else {
            if (p2 < 1) {
                return std::int16_t(-0x1922);
            }
            if (p2 < smaller) {
                add = true;
                smaller = p2;
            } else {
                if (p2 <= smaller) {
                    return std::int16_t(-0xc91);
                }
                base = 0;
                add = false;
                larger = p2;
            }
        }
    } else {
        larger = p1;
        base = 0x1922;
        if (p2 < 1) {
            if (p2 >= 0) {
                return 0x1922;
            }
            smaller = -p2;
            if (smaller < p1) {
                add = true;
            } else {
                if (-p1 == p2 || smaller < p1) {
                    return 0x25b3;
                }
                add = false;
                larger = smaller;
                smaller = p1;
                base = 0x3244;
            }
        } else if (p1 < p2) {
            add = true;
            larger = p2;
            smaller = p1;
            base = 0;
        } else {
            if (p1 <= p2) {
                return 0xc91;
            }
            add = false;
            smaller = p2;
        }
    }
    if (larger == 0) {
        return 0;
    }
    const int t = table(fxDiv(smaller, larger) >> 5);
    return std::int16_t(add ? base + t : base - t);
}

Matrix lookAt(const Vec& eye, const Vec& up, const Vec& at) {
    const Vec z = vecNormalize(vecSub(eye, at));
    const Vec x = vecNormalize(vecCross(up, z));
    const Vec y = vecCross(z, x);
    Matrix m = Matrix::identity();
    m.m[0] = x.x;
    m.m[1] = y.x;
    m.m[2] = z.x;
    m.m[4] = x.y;
    m.m[5] = y.y;
    m.m[6] = z.y;
    m.m[8] = x.z;
    m.m[9] = y.z;
    m.m[10] = z.z;
    m.m[12] = -vecDot(eye, x);
    m.m[13] = -vecDot(eye, y);
    m.m[14] = -vecDot(eye, z);
    return m;
}

namespace {
// Rounded high word of a 64-bit product (value * reciprocal) as the library takes it.
fx32 highRounded(std::int64_t product) {
    return fx32((product >> 32) + ((std::uint32_t(product) > 0x7fffffffu) ? 1 : 0));
}

std::int64_t scaleReciprocal(std::int64_t r, fx32 w) {
    return w == 0x1000 ? r : hwDivide64(r * w, 0x1000);
}
} // namespace

Matrix perspective(fx32 s, fx32 c, fx32 aspect, fx32 n, fx32 f, fx32 w) {
    fx32 cot = fxDiv(c, s);
    std::int64_t inv = hwDivide64(std::int64_t(0x1000) << 32, n - f); // 1/(n-f), 32.32
    if (w != 0x1000) {
        cot = fx32(std::int32_t(std::int64_t(cot) * w / 0x1000)); // truncating toward zero
    }
    Matrix m;
    m.m[5] = cot;
    m.m[11] = -w;
    const fx32 m0 = fx32((hwDivide64(std::int64_t(cot) << 32, aspect) + 0x80000) >> 20);
    inv = scaleReciprocal(inv, w);
    m.m[10] = highRounded(std::int64_t(f + n) * inv);
    const fx32 twoNF = fx32((std::int64_t(n * 2) * f + 0x800) >> 12);
    m.m[14] = highRounded(std::int64_t(twoNF) * inv);
    m.m[0] = m0;
    return m;
}

Matrix orthographic(fx32 t, fx32 b, fx32 l, fx32 r, fx32 n, fx32 f, fx32 w) {
    Matrix m;
    m.m[15] = w;
    auto reciprocal = [&](fx32 d) {
        return scaleReciprocal(hwDivide64(std::int64_t(0x1000) << 32, d), w);
    };
    const std::int64_t rx = reciprocal(r - l), ry = reciprocal(t - b), rz = reciprocal(n - f);
    m.m[0] = highRounded(rx << 13);
    m.m[5] = highRounded(ry << 13);
    m.m[10] = highRounded(rz << 13);
    m.m[12] = highRounded(std::int64_t(-(r + l)) * rx);
    m.m[13] = highRounded(std::int64_t(-(t + b)) * ry);
    m.m[14] = highRounded(std::int64_t(f + n) * rz);
    return m;
}
} // namespace picarat::nds3d
