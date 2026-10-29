#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include <algorithm>
#include <cstdlib>

namespace interceptor::game {
namespace {

using i16 = std::int16_t;
using i32 = std::int32_t;

// The 68000's signed division of a long by a word: quotient and remainder,
// or the dividend's halves unchanged when the quotient does not fit.
struct Division {
    i16 quotient, remainder;
};

Division divide_signed(i32 dividend, i16 divisor) {
    if (divisor == 0) return {i16(dividend), i16(dividend >> 16)};
    const i32 q = dividend / divisor;
    if (q < -32768 || q > 32767) return {i16(dividend), i16(dividend >> 16)};
    return {i16(q), i16(dividend % divisor)};
}

std::uint16_t divide_unsigned(std::uint32_t dividend, std::uint16_t divisor) {
    if (divisor == 0) return std::uint16_t(dividend);
    const std::uint32_t q = dividend / divisor;
    return q > 0xFFFF ? std::uint16_t(dividend) : std::uint16_t(q);
}

i16 product14(i32 a, i32 b) { return i16((a * b) >> 14); }

// A ratio of a long by the cosine as an index into the arcsine table
// ($1228B4): small cosines divide the plain long by the cosine x 64 (and
// keep the cosine scaled for the following divisions, as the original
// does), larger ones divide the long / 64 and round half away from zero.
i16 ratio(i32 value, i16& cosine) {
    if (cosine <= 0x147) {
        cosine = i16(cosine << 6);
        return divide_signed(value, cosine).quotient;
    }
    const i16 half = i16(std::abs(int(cosine)) >> 1);
    const auto d = divide_signed(value >> 6, cosine);
    if (half > std::abs(int(d.remainder))) return d.quotient;
    return d.quotient >= 0 ? i16(d.quotient + 1) : i16(d.quotient - 1);
}

// An angle (tenths) from a sine-like long and a cosine-like long of the
// rotated matrix, divided by the cosine of the pitch ($1228B4 .. $1229DA).
i16 angle_from(const FlightTables& t, i32 sine_long, i32 cosine_long, i16& cosine) {
    const auto table = [&](int byte_index) { return t.arcsine_low[std::size_t(byte_index / 2)]; };
    const i16 q2 = i16(ratio(-sine_long, cosine) * 2);
    i16 angle;
    if (q2 >= 0) {
        if (q2 > 0x180) {
            angle = i16(0x384 - table(std::abs(int(i16(ratio(cosine_long, cosine) * 2)))));
        } else {
            angle = table(q2);
        }
    } else {
        i16 a;
        if (-q2 > 0x180) {
            a = i16(0x384 - table(std::abs(int(i16(ratio(cosine_long, cosine) * 2)))));
        } else {
            a = table(-q2);
        }
        angle = i16(0xE10 - a);
    }
    if (cosine_long < 0) {
        if (sine_long >= 0 && angle > 0x708) {
            angle = i16(0x1518 - angle);
        } else {
            angle = i16(0x708 - angle);
        }
    }
    return i16(std::abs(int(angle)));
}

} // namespace

SineCosine sine_cosine(const FlightTables& t, int a) {
    if (a < 900) return {t.sine(a), t.sine(900 - a)};
    if (a < 1800) return {t.sine(1800 - a), i16(-t.sine(a - 900))};
    if (a < 2700) return {i16(-t.sine(a - 1800)), i16(-t.sine(2700 - a))};
    return {i16(-t.sine(3600 - a)), t.sine(a - 2700)};
}

Matrix body_matrix(const FlightTables& t, Angles angles) {
    const auto p = sine_cosine(t, std::uint16_t(angles.pitch) >> 3);
    const auto h = sine_cosine(t, std::uint16_t(angles.heading) >> 3);
    const auto r = sine_cosine(t, std::uint16_t(angles.roll) >> 3);
    const i32 sp = p.sine, cp = p.cosine, sh = h.sine, ch = h.cosine, sr = r.sine, cr = r.cosine;
    return {i16((i32(product14(sh, sp)) * sr + ch * cr) >> 14),
            i16(-product14(cp, sr)),
            i16((sh * cr - i32(product14(ch, sp)) * sr) >> 14),
            i16((ch * sr - i32(product14(sh, sp)) * cr) >> 14),
            product14(cp, cr),
            i16((i32(product14(ch, sp)) * cr + sh * sr) >> 14),
            i16(-product14(sh, cp)),
            i16(-sp),
            product14(ch, cp)};
}

Matrix view_matrix(const FlightTables& t, Angles angles) {
    const auto negated = [](i16 a) { return a == 0 ? 0 : (full_turn - a) >> 3; };
    const auto p = sine_cosine(t, negated(angles.pitch));
    const auto h = sine_cosine(t, negated(angles.heading));
    const auto r = sine_cosine(t, negated(angles.roll));
    const i32 sp = p.sine, cp = p.cosine, sh = h.sine, ch = h.cosine, sr = r.sine, cr = r.cosine;
    return {i16((cr * ch - i32(product14(sr, sp)) * sh) >> 14),
            i16(-((i32(product14(cr, sp)) * sh + sr * ch) >> 14)),
            product14(cp, sh),
            product14(sr, cp),
            product14(cr, cp),
            i16(sp),
            i16(-((i32(product14(sr, sp)) * ch + cr * sh) >> 14)),
            i16((sr * sh - i32(product14(cr, sp)) * ch) >> 14),
            product14(cp, ch)};
}

Angles rotate(const FlightTables& t, const Matrix& m, i16 pitch_rate, i16 yaw_rate, i16 roll_rate) {
    const auto wrap = [](i16 rate) { return rate < 0 ? i16(rate + full_turn) : rate; };
    const Matrix d = body_matrix(t, {wrap(pitch_rate), wrap(yaw_rate), wrap(roll_rate)});
    std::array<i32, 9> r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r[std::size_t(3 * i + j)] = i32(d[std::size_t(3 * i)]) * m[std::size_t(j)] +
                                        i32(d[std::size_t(3 * i + 1)]) * m[std::size_t(3 + j)] +
                                        i32(d[std::size_t(3 * i + 2)]) * m[std::size_t(6 + j)];
        }
    }

    // Pitch from the sine in r[7], through the table for its range.
    const i32 sine = -r[7];
    const i32 size = sine < 0 ? -sine : sine;
    const std::vector<i16>* table = &t.arcsine_low;
    i16 index;
    if (size >= 0x0E210000) {
        i32 i;
        if (size >= 0x0FF60000) {
            table = &t.arcsine_high;
            i = (size >> 12) + ((size >> 11) & 1) - 0xFF08;
            if (i > 0xF8) i = 0xF8;
        } else {
            table = &t.arcsine_mid;
            i = i16(i16((size >> 16) + ((size >> 15) & 1)) - 0xDDB);
            if (i > 0x225) i = 0x225;
        }
        index = i16(sine < 0 ? -i : i);
    } else {
        const i16 high = i16(sine >> 16);
        index = i16((high >> 4) + ((high >> 3) & 1));
    }
    i16 pitch, cosine;
    if (index >= 0) {
        pitch = (*table)[std::size_t(index)];
        cosine = t.sine(0x384 - pitch);
    } else {
        pitch = i16(0xE10 - (*table)[std::size_t(-index)]);
        cosine = t.sine(pitch - 0xA8C);
    }
    if (cosine < 0x8F) { // nearly vertical: hold the pitch just short of 90°
        pitch = index < 0 ? 0xA82 : 0x38E;
        cosine = i16(0xFEE2);
    }
    const i16 heading = angle_from(t, r[6], r[8], cosine);
    const i16 roll = angle_from(t, r[1], r[4], cosine);
    return {i16(pitch * 8), i16(heading * 8), i16(roll * 8)};
}

std::array<i16, 3> scaled_vector(const FlightTables& t, i16 length, i16 x, i16 y, i16 z, int first_shift) {
    if (length == 0) return {0, 0, 0};
    // The length of (x, y, z) from two ratio lookups ($1108C4).
    std::uint16_t big = std::uint16_t(std::abs(int(x))), small = std::uint16_t(std::abs(int(y)));
    if (small > big) std::swap(big, small);
    std::uint16_t first = 0;
    if (small != 0 && big != 0) first = std::uint16_t(divide_unsigned(std::uint32_t(small) << 8, big) * 2);
    std::uint32_t flat = std::uint32_t(t.length_factor[first / 2]) * big;
    std::uint32_t upright = std::uint32_t(std::abs(int(z))) << 14;
    if (upright > flat) std::swap(flat, upright);
    const std::uint16_t longest = std::uint16_t(flat >> 14);
    std::uint16_t second = 0;
    if (longest != 0) second = std::uint16_t((i16(divide_unsigned(upright, longest)) >> 6) * 2);
    std::uint32_t estimate = (std::uint32_t(t.length_factor[second / 2]) * longest) >> 14;
    if (estimate > 0x7FFF) estimate = 0x7FFF;
    if (estimate == 0) return {0, 0, 0};

    // Scale by |length| / estimate with as many bits as fit ($119CB8).
    i32 scale = std::abs(int(length));
    int shift = first_shift;
    while (scale <= i32(estimate)) {
        scale <<= 2;
        shift += 2;
    }
    do {
        scale >>= 2;
        shift -= 2;
    } while (shift > 1 && scale > i32(estimate));
    scale <<= 2;
    shift += 2;
    const i16 factor = i16(divide_unsigned(std::uint32_t(scale) << 8, std::uint16_t(estimate)));
    std::array<i16, 3> out{i16((i32(factor) * x) >> shift), i16((i32(factor) * y) >> shift),
                           i16((i32(factor) * z) >> shift)};
    if (length < 0) {
        for (auto& c : out) c = i16(-c);
    }
    return out;
}

namespace {

// $119EE4: a signed word division rounded half away from zero (a quotient
// of 0 rounds up).
i16 divide_rounded(i32 dividend, i16 divisor) {
    const Division d = divide_signed(dividend, divisor);
    const int half = std::abs(int(divisor)) >> 1;
    if (std::abs(int(d.remainder)) < half) return d.quotient;
    return i16(d.quotient >= 0 ? d.quotient + 1 : d.quotient - 1);
}

// $119BB2: the square root by Newton's steps from n / 200 + 2, until two
// guesses differ by at most 1; larger numbers are scaled down first.
i16 square_root(std::uint32_t n) {
    int scale = 0;
    if (n > 0x63F0000) {
        n >>= 8;
        scale = 4;
    } else if (n > 0x63F000) {
        n >>= 4;
        scale = 2;
    }
    std::uint16_t guess = std::uint16_t(divide_unsigned(n, 200) + 2);
    for (int guard = 0; guard < 64; ++guard) {
        const std::uint16_t next = divide_unsigned(n, guess);
        const i16 step = i16(next - guess);
        if (step >= -1 && step <= 1) return i16(next << scale);
        guess = std::uint16_t(std::uint16_t(next + guess) >> 1);
    }
    return i16(guess << scale);
}

} // namespace

Direction direction_of(const FlightTables& t, i32 x, i32 y, i32 z) {
    // Signs apart, the three lengths scaled so that the largest fits a
    // word.
    const bool west = x < 0, down = y < 0, south = z < 0;
    x = std::abs(x);
    y = std::abs(y);
    z = std::abs(z);
    i32 largest = std::max(x, std::max(y, z));
    int down_shift = 8, up_shift = 6;
    if (largest > 0x10000000) {
        x >>= 3;
        y >>= 3;
        z >>= 3;
        down_shift = 14;
        up_shift = 0;
    } else if (largest > 0x2000000) {
        down_shift = 14;
        up_shift = 0;
    } else if (largest > 0x800000) {
        down_shift = 12;
        up_shift = 2;
    } else if (largest > 0x200000) {
        down_shift = 10;
        up_shift = 4;
    }
    const auto atan = [&](int index) { return int(t.arctangent[std::size_t(std::clamp(index, 0, 256))]) * 8; };
    // The heading: the smaller of x and z over the larger.
    const i16 sx = i16(x >> down_shift), sz = i16(z >> down_shift);
    int heading = 0;
    if (sz < sx) heading = 0x1C20 - atan(divide_rounded(z << up_shift, sx) >> 6);
    else if (sz == 0) heading = 0xE10;
    else heading = atan(divide_rounded(x << up_shift, sz) >> 6);
    if (west && south) heading = 0x3840 - heading;
    else if (south) heading = heading + 0x3840;
    else if (!west) heading = full_turn - heading;
    // The pitch: the height over the flat length.
    const i16 flat = square_root(std::uint32_t(i32(sz) * sz + i32(sx) * sx));
    const i16 sy = i16(y >> down_shift);
    int pitch = 0;
    if (flat < sy) pitch = 0x1C20 - atan(divide_rounded(i32(flat) << 8, sy));
    else if (flat == 0) pitch = 0xE10;
    else pitch = atan(divide_rounded(i32(sy) << 8, flat));
    if (!down) pitch = full_turn - pitch;
    if (pitch > 0x707F) pitch = 0;
    if (heading > 0x707F) heading = 0;
    return {i16(pitch), i16(heading)};
}

} // namespace interceptor::game
