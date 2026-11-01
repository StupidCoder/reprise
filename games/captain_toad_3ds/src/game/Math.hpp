// Recovered single-precision math routines of the original code (ARM11 main code).
//
// The game ships its own sine, cosine, arc cosine and arc tangent. Their results differ from the
// Mac's math library in the last bits, and these small differences add up over hundreds of ticks
// of walking and turning. To replay the original exactly, the port uses transcriptions of the
// original routines. They are written close to the machine code: variables named s0..s8 are the
// VFP registers of the original, constants are given as their bit patterns (fbits(0x...)), and
// every operation stays in the original order. Compile with -ffp-contract=off.
//
//   0x2f7264 cosf, 0x2edab8 sinf: |x| < pi/4 is evaluated directly; |x| < 12867.95 is first reduced
//            by the Cody-Waite method (rounding x * 2/pi through +-2^23, pi/2 split into four parts
//            0x3fc90000 + 0x39fda000 + 0x33a22000 + 0x2c34611a); larger arguments go to a library
//            routine (0x259b8c) that is not recovered (never reached in the opening).
//   0x2db188 acosf, 0x2eee88 atan2f, 0x2db2b4 quaternion rotation, 0x2ed5bc rotation between two
//            vectors, 0x2f09ac quaternion from axis and angle, 0x2f06fc matrix from quaternion.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "game/VecLib.hpp"

namespace captain_toad::RecoveredMath {

// Reinterpret bit patterns as floats and back (the original's constants are bit patterns).
inline float fbits(uint32_t b) {
    float f;
    memcpy(&f, &b, 4);
    return f;
}
inline uint32_t bitsf(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

// ---- Sine and cosine ----------------------------------------------------------------------------

// Shared range reduction of sinf/cosf: returns the argument reduced to [-pi/4, pi/4] and the
// quadrant it came from (0..3), or quadrant -1 for tiny arguments that need no polynomial.
inline float reduce(float x, int& quadrant) {
    const uint32_t b = bitsf(x);
    if (0x7e921fb6u > (b << 1)) {   // |x| < pi/4: no reduction
        quadrant = (0x73000000u <= (b << 1)) ? 0 : -1;
        return x;
    }
    if (!(0x46490e49u > (b & 0x7fffffffu))) {
        throw std::runtime_error("recovered trig: argument beyond Cody-Waite range");
    }
    // k = round(x * 2/pi), using the 2^23 trick: adding and subtracting 2^23 rounds to an integer.
    const float twoOverPi = fbits(0x3f22f983);
    const float magic = fbits(0x4b000000);
    float s2 = x * twoOverPi;
    float k;
    if (b & 0x80000000u) {
        s2 = s2 - magic;
        k = s2 + magic;
    } else {
        s2 = s2 + magic;
        k = s2 - magic;
    }
    quadrant = int32_t(k) & 3;
    // r = x - k * pi/2, with pi/2 in four parts so the subtraction stays exact.
    float r = x;
    r = r - k * fbits(0x3fc90000);
    r = r - k * fbits(0x39fda000);
    r = r - k * fbits(0x33a22000);
    r = r - k * fbits(0x2c34611a);
    return r;
}

// Sine polynomial on the reduced range: r + r*(r^2*(c1 + r^2*(c2 - r^2*c3))).
inline float sinPoly(float r) {
    float x2 = r * r;
    float a = fbits(0x3c0882da);
    a = a - x2 * fbits(0x394c6d33);
    float c = fbits(0xbe2aaaa0);
    c = c + x2 * a;
    float t = c * x2;
    return r + r * t;
}

// Cosine polynomial on the reduced range: 1 + r^2*(c1 + r^2*(c2 + r^2*c3)).
inline float cosPoly(float r) {
    float x2 = r * r;
    float a = fbits(0x3d2a9fca);
    a = a + x2 * fbits(0xbab23ab9);
    float c = fbits(0xbeffffdd);
    c = c + x2 * a;
    float one = 1.0f;
    return one + x2 * c;
}

// 0x2f7264
inline float cosf(float x) {
    int quadrant;
    float r = reduce(x, quadrant);
    if (quadrant < 0) {
        if ((bitsf(x) << 1) > 0xff000000u) {
            return x + x;   // NaN in, NaN out
        }
        return 1.0f;
    }
    if (quadrant & 1) {
        float v = sinPoly(r);
        return (quadrant & 2) ? v : -v;
    }
    float v = cosPoly(r);
    return (quadrant & 2) ? -v : v;
}

// 0x2edab8
inline float sinf(float x) {
    int quadrant;
    float r = reduce(x, quadrant);
    if (quadrant < 0) {
        if ((bitsf(x) << 1) > 0xff000000u) {
            return x + x;   // NaN in, NaN out
        }
        return x;   // sin(x) = x for tiny x
    }
    if (quadrant & 1) {
        float v = cosPoly(r);
        return (quadrant & 2) ? -v : v;
    }
    float v = sinPoly(r);
    return (quadrant & 2) ? -v : v;
}

// ---- Square root, arc cosine, arc tangent -----------------------------------------------------

// 0x451460 sqrtf: the VSQRT instruction (the errno path for negative input is not modelled).
inline float sqrtf_(float x) {
    return std::sqrt(x);
}

// 0x2db188 acosf. Main range 2^-25 <= |x| <= 1:
//   |x| <= 0.5 uses pi/2 - (x + x^3 P(x^2));
//   |x| > 0.5 uses 2 asin(sqrt((1-|x|)/2)), or pi minus that for negative x.
// pi and pi/2 are split into a short head (s18) and a tail (s16) for precision.
inline float acosf(float x) {
    const uint32_t b = bitsf(x);
    float s16 = 0.0f;
    float s17;
    float s18;
    float s0 = x;
    if (uint32_t(0x9a000000u + (b << 1)) > 0x19000000u) {
        if (0x66000000u > (b << 1)) {
            return fbits(0x3fc90fdb);   // tiny |x|: pi/2
        }
        if (0xff000000u < (b << 1)) {
            return x + x;   // NaN
        }
        return std::nanf("");   // |x| > 1: errno path (0x2fdcfa), result 0/0
    }
    if (!(0x7e000000u >= (b << 1))) {
        // |x| > 0.5
        float a = std::fabs(x);
        s17 = (1.0f - a) * 0.5f;
        s0 = sqrtf_(s17);
        if (b & 0x80000000u) {
            s0 = -s0;
            s18 = fbits(0x40490000);   // pi (head)
            s16 = fbits(0x3a7daa22);   // pi (tail)
        } else {
            s18 = s16;
        }
        s0 = s0 * -2.0f;
    } else {
        // |x| <= 0.5
        s17 = x * x;
        s18 = fbits(0x3fc90000);   // pi/2 (head)
        s16 = fbits(0x39fdaa22);   // pi/2 (tail)
    }
    // Polynomial in s17 (Horner form, coefficients as in the original's literal pool).
    float s1 = fbits(0x3d1cfe24);
    float s4 = fbits(0x3cd88ac7);
    float s5 = fbits(0x3d38671e);
    float s3 = fbits(0x3d99931b);
    float s2 = s16 - s0;
    s0 = s0 * s17;
    s4 = s4 + s17 * s1;
    s1 = fbits(0x3e2aaaaf);
    s5 = s5 + s17 * s4;
    s3 = s3 + s17 * s5;
    s1 = s1 + s17 * s3;
    s2 = s2 - s0 * s1;
    return s2 + s18;
}

// 0x2eee88 atan2f(y, x). Structure of the original:
//   - special cases (zeros, infinities, NaN, extreme exponents) are normalised first;
//   - if the exponents differ by a lot, the result is +-pi/2, 0 or +-pi at once;
//   - otherwise a quadrant offset (+-pi/2 or pi, head s3 + tail s2) is chosen, the ratio is
//     taken, and for |num| ~ |den| the ratio is first rotated by atan(0.5) for accuracy;
//   - an odd polynomial in the ratio gives the final angle.
inline float atan2f(float y, float x) {
    uint32_t r0 = bitsf(y);
    uint32_t r1 = bitsf(x);
    float s0 = y;
    float s1 = x;
    float s2;
    float s3;
    float s4;
    {
        uint32_t r3 = 0xe8000000u + (r0 << 1);
        uint32_t r2 = 0xe8000000u + (r1 << 1);
        if (!(r3 < 0xe5000000u && r2 < 0xe5000000u)) {   // 0x2ef00c: zeros, infinities, NaN, extremes
            const uint32_t inf2 = 0xff000000u;
            if (!(inf2 >= (r0 << 1)) || !(inf2 >= (r1 << 1))) {
                return s0 + s1;   // NaN
            }
            if (((r0 | r1) << 1) == 0) {
                r1 |= 0x7f800000u;
                s1 = fbits(r1);
            } else if (inf2 == (r0 << 1) && inf2 == (r1 << 1)) {
                r0 &= ~0x40000000u;
                s0 = fbits(r0);
                r1 &= ~0x40000000u;
                s1 = fbits(r1);
            } else if (inf2 == (r0 << 1) || (r1 << 1) == 0) {
                r0 |= 0x7f800000u;
                r1 &= 0x80000000u;
            } else if (!(int32_t((r1 << 1) ^ (r0 << 1)) < 0)) {
                // Both tiny or both huge: rescale by 2^32 or 2^-32.
                if (int32_t(r1 << 1) >= 0) {
                    s0 = s0 * fbits(0x4f800000);
                    s1 = s1 * fbits(0x4f800000);
                } else {
                    s0 = s0 * fbits(0x2f800000);
                    s1 = s1 * fbits(0x2f800000);
                }
                r0 = bitsf(s0);
                r1 = bitsf(s1);
            }
        }
    }
    // 0x2eeeb0: exponent difference shortcuts.
    uint32_t r3 = r0 << 1;
    int exponentDifference = int((r3 >> 24)) - int(((r1 << 1) >> 24));
    if (exponentDifference > 27) {
        return (r0 & 0x80000000u) ? fbits(0xbfc90fdb) : fbits(0x3fc90fdb);   // +-pi/2
    }
    if (exponentDifference < -26) {
        if (!(r1 & 0x80000000u)) {
            return s0 / s1;
        }
        return (r0 & 0x80000000u) ? fbits(0xc0490fdb) : fbits(0x40490fdb);   // +-pi
    }
    if (r3 > (r1 << 1)) {
        // |y| > |x|: atan2 = +-pi/2 + atan(x / -y)
        float s4x = s1;
        uint32_t xr = r1;
        s1 = -s0;
        if (r0 & 0x80000000u) {
            s3 = fbits(0xbfc90000);
            s2 = fbits(0xb9fdaa22);
        } else {
            s3 = fbits(0x3fc90000);
            s2 = fbits(0x39fdaa22);
        }
        r1 = r0 ^ 0x80000000u;
        r0 = xr;
        s0 = s4x;
    } else if (!(r1 & 0x80000000u)) {
        s2 = 0.0f;
        s3 = s2;
    } else if (r0 & 0x80000000u) {
        s3 = fbits(0xc0490000);
        s2 = fbits(0xba7daa22);
    } else {
        s3 = fbits(0x40490000);
        s2 = fbits(0x3a7daa22);
    }
    // 0x2eef54: the ratio.
    if (uint32_t((r1 - r0) << 1) >= 0x1000000u) {
        s4 = s0 / s1;
    } else {
        // |num| ~ |den|: rotate by atan(0.5) first.
        const bool differ = (r0 ^ r1) & 0x80000000u;
        const float s5c = fbits(0x3eed6000);
        const float s6 = fbits(0x37ce0ac3);
        s4 = differ ? -0.5f : 0.5f;
        if (differ) {
            s3 = s3 - s5c;
            s2 = s2 - s6;
        } else {
            s3 = s3 + s5c;
            s2 = s2 + s6;
        }
        float s5 = s0;
        s5 = s5 - s4 * s1;
        s1 = s1 + s0 * s4;
        s4 = s5 / s1;
    }
    // 0x2eefcc: odd polynomial in the ratio.
    float c1 = fbits(0xbd65ad2d);
    float s5 = fbits(0x3dd5b88f);
    float s7 = fbits(0xbe11b50f);
    float s6 = fbits(0x3e4cc861);
    float z = s4 * s4;
    s5 = s5 + z * c1;
    c1 = fbits(0xbeaaaaa8);
    float s8 = s4 * z;
    s7 = s7 + z * s5;
    s6 = s6 + z * s7;
    c1 = c1 + z * s6;
    s2 = s2 + s8 * c1;
    float r = s2 + s4;
    return r + s3;
}

// ---- Rotations ----------------------------------------------------------------------------------

struct Quat {
    float x, y, z, w;
};

// 0x2db2b4: rotate v in place by quaternion q (q * v * q^-1, written out term by term).
inline void rotate(captain_toad::RecoveredVec::V3& v, const Quat& q) {
    float s0 = q.y * v.z;
    float s1 = q.z * v.x;
    float s2 = q.x * v.y;
    float s3 = -(q.x * v.x);
    s0 = s0 - q.z * v.y;
    s1 = s1 - q.x * v.z;
    s2 = s2 - q.y * v.x;
    s3 = s3 - q.y * v.y;
    s0 = s0 + q.w * v.x;
    s1 = s1 + q.w * v.y;
    s2 = s2 + q.w * v.z;
    s3 = s3 - q.z * v.z;

    float x = s0 * q.w;
    x = x - s1 * q.z;
    x = x + s2 * q.y;
    x = x - s3 * q.x;
    float y = s1 * q.w;
    y = y + s0 * q.z;
    y = y - s2 * q.x;
    y = y - s3 * q.y;
    float z = s1 * q.x;
    z = z - s0 * q.y;
    z = z + s2 * q.w;
    z = z - s3 * q.z;
    v = {x, y, z};
}

// 0x2ed5bc: the rotation from direction `from` to direction `to`, as an axis (the normalised
// cross product) and an angle (acos of the clamped dot product). Returns false (angle 0; the axis
// would come from a data constant at 0x55017c that the recovered callers never read) when the
// directions are almost parallel: |from x to|^2 < 1e-6 or |angle| < 0.001.
inline bool rotationBetween(captain_toad::RecoveredVec::V3& axis, float& angle,
                            const captain_toad::RecoveredVec::V3& from, const captain_toad::RecoveredVec::V3& to) {
    using namespace captain_toad::RecoveredVec;
    V3 c = cross(from, to);
    float lengthSquared = c.x * c.x;
    lengthSquared = lengthSquared + c.y * c.y;
    lengthSquared = lengthSquared + c.z * c.z;
    if (lengthSquared < fbits(0x358637be)) {
        angle = 0.0f;
        return false;
    }
    normalize(c);
    float d = from.x * to.x;
    d = d + from.y * to.y;
    d = d + from.z * to.z;
    if (d < -1.0f) {
        d = -1.0f;
    } else if (d > 1.0f) {
        d = 1.0f;
    }
    float a = acosf(d);
    float magnitude = a;
    if (a <= 0.0f) {
        magnitude = -magnitude;
    }
    if (magnitude < fbits(0x3a83126f)) {
        angle = 0.0f;
        return false;
    }
    angle = a;
    axis = c;
    return true;
}

// ---- Small scalar helpers of the walk code -------------------------------------------------------

// 0x2a2204: clamp to [0, 1].
inline float clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

// 0x2a206c: move v toward zero by max / frames (frames as unsigned), without crossing zero.
// The walk uses this to let speeds decay over a fixed number of frames.
inline float decayToZero(uint32_t frames, float v, float max) {
    if (v == 0.0f) {
        return v;
    }
    float step = max / float(frames);
    if (0.0f > v) {
        float r = step + v;
        return r > 0.0f ? 0.0f : r;
    }
    float r = v - step;
    return r < 0.0f ? 0.0f : r;
}

// 0x1a83fc: add a step and clamp to a maximum.
inline float addClampMax(float v, float max, float step) {
    v = v + step;
    return v > max ? max : v;
}

// 0x1a8410: values below a limit count as zero.
inline float zeroBelow(float v, float limit) {
    return v < limit ? 0.0f : v;
}

// 0x2f09ac: quaternion for a rotation of `angle` radians about a unit axis (half angle via 0.5).
inline Quat fromAxisAngle(const captain_toad::RecoveredVec::V3& axis, float angle) {
    float half = angle * 0.5f;
    float c = cosf(half);
    float s = sinf(half);
    return {axis.x * s, axis.y * s, axis.z * s, c};
}

// 0x2f06fc: rotation matrix (3x4 with zero translation) of a unit quaternion. The products are
// formed and combined in the original's order (comments name the original's registers).
inline captain_toad::RecoveredVec::M34 toMatrix(const Quat& q) {
    const float one = 1.0f;
    const float two = 2.0f;
    float xx = q.x * q.x;
    float xy = q.x * q.y;
    float yy = q.y * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wy = q.w * q.y;
    float wz = q.w * q.z;
    float wx = q.w * q.x;
    float zz = q.z * q.z;
    float m00 = one;
    float m11 = one;
    m11 = m11 - two * xx;   // s0 = 1 - 2xx
    m00 = m00 - two * yy;   // s7 = 1 - 2yy
    float xy2 = xy * two;
    float xz2 = xz * two;
    float yz2 = yz * two;
    float m22 = m11;   // s13 = 1 - 2xx
    float m01 = xy2;
    float m10 = xy2;
    m10 = m10 + two * wz;
    m11 = m11 - two * zz;
    m22 = m22 - two * yy;
    m01 = m01 - two * wz;
    m00 = m00 - two * zz;
    float m21 = yz2;
    float m12 = yz2;
    m21 = m21 + two * wx;
    m12 = m12 - two * wx;
    float m02 = xz2;
    float m20 = xz2;
    m20 = m20 - two * wy;
    m02 = m02 + two * wy;
    return {{m00, m01, m02, 0.0f, m10, m11, m12, 0.0f, m20, m21, m22, 0.0f}};
}

// 0x2fd018: quaternion from a rotation matrix (the classic branch on the largest diagonal term).
inline Quat fromMatrix(const captain_toad::RecoveredVec::M34& M) {
    const float* m = M.m;
    const float m00 = m[0], m01 = m[1], m02 = m[2];
    const float m10 = m[4], m11 = m[5], m12 = m[6];
    const float m20 = m[8], m21 = m[9], m22 = m[10];
    float s13 = m00 + m11;
    float trace = s13 + m22;
    float x, y, z, w;
    if (trace > 0.0f) {
        float s1 = trace + 1.0f;
        float r = std::sqrt(s1);
        w = r * 0.5f;
        float k = 0.5f / r;
        x = (m21 - m12) * k;
        y = (m02 - m20) * k;
        z = (m10 - m01) * k;
    } else if (m11 > m00) {
        if (m22 > m11) {
            goto zLargest;
        }
        float s1 = m22 + m00;
        s1 = m11 - s1;
        float r = std::sqrt(s1 + 1.0f);
        y = r * 0.5f;
        float k = r;
        if (r != 0.0f) {
            k = 0.5f / r;
        }
        z = (m12 + m21) * k;
        w = (m02 - m20) * k;
        x = (m10 + m01) * k;
    } else {
        if (m22 > m00) {
            goto zLargest;
        }
        float s1 = m11 + m22;
        s1 = m00 - s1;
        float r = std::sqrt(s1 + 1.0f);
        x = r * 0.5f;
        float k = r;
        if (r != 0.0f) {
            k = 0.5f / r;
        }
        w = (m21 - m12) * k;
        y = (m01 + m10) * k;
        z = (m02 + m20) * k;
    }
    return {x, y, z, w};

zLargest:
    {
        float s1 = m22 - s13;
        float r = std::sqrt(s1 + 1.0f);
        z = r * 0.5f;
        float k = r;
        if (r != 0.0f) {
            k = 0.5f / r;
        }
        w = (m10 - m01) * k;
        x = (m20 + m02) * k;
        y = (m21 + m12) * k;
    }
    return {x, y, z, w};
}

// 0x23cde4: the rotation between two rotation matrices at t, as a matrix: both become quaternions
// (0x2fd018), the dot product is clamped to [-1, 1] and the shorter arc taken; below a sine of
// 2^-23 the weights are linear (1 - t, t), else sin((1 - t)a) / sin a and sin(ta) / sin a.
inline captain_toad::RecoveredVec::M34 slerp(const captain_toad::RecoveredVec::M34& from,
                                             const captain_toad::RecoveredVec::M34& to, float t) {
    const Quat a = fromMatrix(from);
    const Quat b = fromMatrix(to);
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
    const bool flip = d < 0.0f;
    if (flip) {
        d = -d;
    }
    const float angle = acosf(d);
    const float s = sinf(angle);
    float wa, wb;
    if (std::fabs(s) < fbits(0x34000000)) {
        wa = 1.0f - t;
        wb = t;
    } else {
        const float inv = 1.0f / s;
        wa = sinf(angle - angle * t) * inv;
        wb = sinf(angle * t) * inv;
    }
    if (flip) {
        wb = -wb;
    }
    return toMatrix({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

}   // namespace captain_toad::RecoveredMath
