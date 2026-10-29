#include "fa_18_interceptor_amiga/game/flight_tables.hpp"

namespace interceptor::game {
namespace {

std::vector<std::int16_t> words(const amiga::LoadedImage& m, std::uint32_t at, int count) {
    std::vector<std::int16_t> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) out[std::size_t(i)] = m.s16(at + std::uint32_t(2 * i));
    return out;
}

template <std::size_t N> std::array<std::int16_t, N> word_array(const amiga::LoadedImage& m, std::uint32_t at) {
    std::array<std::int16_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = m.s16(at + std::uint32_t(2 * i));
    return out;
}

} // namespace

FlightTables read_flight_tables(const amiga::LoadedImage& m) {
    FlightTables t;
    t.sine_window = words(m, 0x1338E0 - 2 * 900, 1801);
    t.arcsine_low = words(m, 0x13308A, 0x200); // index up to $E21 / 16; rounding can read further
    t.arcsine_mid = words(m, 0x133290, 0x226); // clamped to $225
    t.arcsine_high = words(m, 0x1336E0, 0xF9); // clamped to $F8
    t.player_response = words(m, 0x1327B8, 62);
    t.other_response = words(m, 0x132834, 62);
    t.climb_by_speed = word_array<31>(m, 0x11ABC8);
    t.sink_by_speed = word_array<31>(m, 0x11AB88);
    t.bank_turn_by_pitch = word_array<15>(m, 0x12269A);
    for (std::size_t i = 0; i < t.length_factor.size(); ++i) t.length_factor[i] = m.u16(0x110928 + std::uint32_t(2 * i));
    return t;
}

} // namespace interceptor::game
