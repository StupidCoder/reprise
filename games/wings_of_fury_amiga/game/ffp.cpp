#include "wings_of_fury_amiga/game/ffp.hpp"
#include <bit>
#include <utility>

namespace wings::game::ffp {
namespace {

constexpr int bias = 64;
constexpr Ffp largest = 0xFFFFFF7F;

struct Parts {
    std::uint32_t mantissa; // 24 bits
    bool negative;
    int exponent;           // excess 64
};

Parts split(Ffp f) {
    return {f >> 8, (f & 0x80) != 0, int(f & 0x7F)};
}

Ffp pack(std::uint32_t mantissa, bool negative, int exponent) {
    if (mantissa == 0 || exponent < 0) return 0;
    if (exponent > 127) return largest | (negative ? 0x80 : 0);
    return mantissa << 8 | (negative ? 0x80 : 0) | std::uint32_t(exponent);
}

} // namespace

Ffp from_int(std::int32_t value) {
    if (value == 0) return 0;
    const bool negative = value < 0;
    std::uint64_t m = negative ? std::uint64_t(-std::int64_t(value)) : std::uint64_t(value);
    int bits = std::bit_width(m);
    int exponent = bias + bits;
    if (bits > 24) {
        const int shift = bits - 24;
        const bool half = (m >> (shift - 1)) & 1;
        m >>= shift;
        if (half && ++m >> 24) {
            m >>= 1;
            ++exponent;
        }
    } else {
        m <<= 24 - bits;
    }
    return pack(std::uint32_t(m), negative, exponent);
}

std::int32_t to_int(Ffp f) {
    if (f == 0) return 0;
    const auto p = split(f);
    const int shift = p.exponent - bias - 24;
    if (p.exponent - bias > 31) return p.negative ? INT32_MIN : INT32_MAX;
    const std::int64_t magnitude = shift >= 0 ? std::int64_t(p.mantissa) << shift
                                   : shift > -32 ? std::int64_t(p.mantissa >> -shift)
                                                 : 0;
    return std::int32_t(p.negative ? -magnitude : magnitude);
}

Ffp add(Ffp a, Ffp b) {
    if (a == 0) return b;
    if (b == 0) return a;
    auto x = split(a), y = split(b);
    if (x.exponent < y.exponent || (x.exponent == y.exponent && x.mantissa < y.mantissa)) std::swap(x, y);
    const int distance = x.exponent - y.exponent;
    if (distance >= 24) return pack(x.mantissa, x.negative, x.exponent);
    // Eight guard bits; the sum is rounded at the larger operand's scale
    // before it is normalized.
    const std::uint64_t larger = std::uint64_t(x.mantissa) << 8;
    const std::uint64_t smaller = (std::uint64_t(y.mantissa) << 8) >> distance;
    std::uint64_t sum = x.negative == y.negative ? larger + smaller : larger - smaller;
    sum = (sum + 0x80) & ~std::uint64_t(0xFF);
    if (sum == 0) return 0;
    int exponent = x.exponent;
    if (sum >> 32) {
        sum >>= 1;
        ++exponent;
    }
    while (!(sum & 0x80000000u)) {
        sum <<= 1;
        --exponent;
    }
    return pack(std::uint32_t(sum >> 8), x.negative, exponent);
}

Ffp multiply(Ffp a, Ffp b) {
    if (a == 0 || b == 0) return 0;
    const auto x = split(a), y = split(b);
    const std::uint64_t product = std::uint64_t(x.mantissa) * y.mantissa; // 46-48 bits
    int exponent = x.exponent + y.exponent - bias;
    std::uint64_t m, rest;
    if (product >> 47) {
        m = product >> 24;
        rest = product & 0xFFFFFF;
    } else {
        m = product >> 23;
        rest = (product << 1) & 0xFFFFFF;
        --exponent;
    }
    if (rest & 0x800000) {
        if (++m >> 24) {
            m >>= 1;
            ++exponent;
        }
    }
    return pack(std::uint32_t(m), x.negative != y.negative, exponent);
}

Ffp divide(Ffp a, Ffp b) {
    if (a == 0 || b == 0) return 0; // (the game never divides by zero)
    const auto x = split(a), y = split(b);
    int exponent = x.exponent - y.exponent + bias;
    std::uint64_t q = (std::uint64_t(x.mantissa) << 24) / y.mantissa;
    const std::uint64_t r = (std::uint64_t(x.mantissa) << 24) % y.mantissa;
    bool round_up;
    if (q >> 24) {
        round_up = q & 1;
        q >>= 1;
        ++exponent;
    } else {
        round_up = 2 * r >= y.mantissa;
    }
    if (round_up && ++q >> 24) {
        q >>= 1;
        ++exponent;
    }
    return pack(std::uint32_t(q), x.negative != y.negative, exponent);
}

Ffp negate(Ffp f) {
    return f == 0 ? 0 : f ^ 0x80;
}

} // namespace wings::game::ffp
