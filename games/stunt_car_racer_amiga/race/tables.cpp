#include "stunt_car_racer_amiga/race/tables.hpp"

namespace stuntcar::race {
namespace {

constexpr std::uint32_t quarter_wave_table = 0x1CA42;
constexpr std::uint32_t atan_table = 0x1CC46;
constexpr std::uint32_t hypot_table = 0x1DC46;
constexpr std::uint32_t slope_table = 0x1EECA;
constexpr std::uint32_t transform_table = 0x1EC46;
constexpr std::uint32_t angle_limit_table = 0x61AD4;
constexpr std::uint32_t heading_bias_table = 0x6125A;
constexpr std::uint32_t radius_gain_table = 0x5C6B8;

std::uint16_t rotate_right(std::uint16_t v, int n) { return std::uint16_t((v >> n) | (v << (16 - n))); }

// Shared by sine (phase 0) and cosine (phase $4000): mirror into the first
// quarter, interpolate between the two table entries, halve, and set the sign.
std::int16_t wave(const Tables& t, std::int16_t angle, std::uint16_t phase) {
    const auto a = std::uint16_t(angle);
    std::uint16_t q = a & 0x3FFF;
    if (((a & 0x4000) ^ phase) == 0) q = std::uint16_t((q ^ 0x3FFF) + 1);
    const auto r = rotate_right(q, 5);
    const std::size_t i = (r & 0x3FE) >> 1;
    const auto here = t.quarter_wave[i], next = t.quarter_wave[i + 1];
    const auto step = std::uint16_t(here - next);
    const auto fraction = std::uint16_t(rotate_right(r, 1) & 0xFC00);
    const auto drop = std::uint16_t((std::uint32_t(fraction) * step) >> 16);
    auto value = std::int16_t(std::uint16_t(here - drop) >> 1);
    const auto sign = std::uint16_t((a & phase) << 1);
    if (std::int16_t(a ^ sign) < 0) value = std::int16_t(-value);
    return value;
}

} // namespace

Tables load_tables(const data::Program& program) {
    Tables t;
    for (std::size_t i = 0; i < t.quarter_wave.size(); ++i) t.quarter_wave[i] = program.u16(quarter_wave_table + std::uint32_t(i) * 2);
    for (std::size_t i = 0; i < t.atan.size(); ++i) {
        t.atan[i] = std::int16_t(program.u16(atan_table + std::uint32_t(i) * 2));
        t.hypot_gain[i] = program.u16(hypot_table + std::uint32_t(i) * 2);
    }
    for (std::size_t i = 0; i < t.slope_half_angle.size(); ++i) t.slope_half_angle[i] = program.u8(slope_table + std::uint32_t(i));
    for (std::size_t i = 0; i < t.transform_terms.size(); ++i) t.transform_terms[i] = program.u8(transform_table + std::uint32_t(i));
    for (std::size_t i = 0; i < t.angle_limits.size(); ++i)
        t.angle_limits[i] = std::int16_t(program.u16(angle_limit_table + std::uint32_t(i) * 2));
    for (std::size_t i = 0; i < t.heading_bias.size(); ++i)
        t.heading_bias[i] = std::int16_t(program.u16(heading_bias_table + std::uint32_t(i) * 2));
    for (std::size_t i = 0; i < t.radius_gain.size(); ++i) t.radius_gain[i] = program.u8(radius_gain_table + std::uint32_t(i));
    return t;
}

std::int16_t sine(const Tables& t, std::int16_t angle) { return wave(t, angle, 0x0000); }
std::int16_t cosine(const Tables& t, std::int16_t angle) { return wave(t, angle, 0x4000); }

Polar polar(const Tables& t, std::int16_t x, std::int16_t y) {
    const auto ax = std::uint16_t(x < 0 ? -x : x), ay = std::uint16_t(y < 0 ? -y : y);
    std::uint16_t ratio;
    std::int16_t angle;
    // A quotient too large for the 68000's DIVU leaves 0 behind.
    const auto divide = [](std::uint16_t small, std::uint16_t big) {
        const auto q = (std::uint32_t(small) << 16) / big;
        return std::uint16_t(q > 0xFFFF ? 0 : q);
    };
    if (ay == ax) {
        ratio = 0xFFFF;
        angle = 0x2000;
    } else if (ay > ax) {
        ratio = divide(ax, ay);
        angle = t.atan[(ratio >> 4) >> 1];
    } else {
        ratio = divide(ay, ax);
        angle = t.atan[(ratio >> 4) >> 1];
        if ((x ^ y) >= 0) angle = std::int16_t(-angle);
        angle = std::int16_t(std::uint16_t((x < 0 ? 0xC000 : 0x4000) + std::uint16_t(angle)));
        goto length;
    }
    if ((x ^ y) < 0) angle = std::int16_t(-angle);
    if (y < 0) angle = std::int16_t(std::uint16_t(angle) + 0x8000);
length:
    const auto small = ax < ay ? ax : ay, big = ax < ay ? ay : ax;
    const auto gain = t.hypot_gain[(ratio >> 4) >> 1];
    const auto extra = std::uint16_t((std::uint32_t(gain) * small) >> 16);
    return {angle, std::int16_t(std::uint16_t(extra + big))};
}

} // namespace stuntcar::race
