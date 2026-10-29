#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The tables the flight model reads, taken from the program (game.md):
// the sine table and the three arcsine tables the attitude step uses, the
// control response, the climb and sink tables by airspeed, and the bank-turn
// fade by pitch. Angles in these tables are tenths of a degree; sines are
// scaled so that 1 = $4000.
namespace interceptor::game {

struct FlightTables {
    // sin(0.0°) .. sin(90.0°) in tenths of a degree ($1338E0). The original
    // also reads a little before the table for unusual pitches; `sine_at`
    // gives those words too.
    std::vector<std::int16_t> sine_window; // indices -900 .. 900
    std::int16_t sine(int tenths) const { return sine_window[std::size_t(tenths + 900)]; }

    // Arcsines in tenths of a degree: of a sine / 16 ($13308A), of the
    // sine's high word minus $DDB ($133290), and near 90° of the sine / 4096
    // minus $FF08 ($1336E0).
    std::vector<std::int16_t> arcsine_low, arcsine_mid, arcsine_high;

    // Rate per input step ($1327B8 for the player, $132834 for the others).
    std::vector<std::int16_t> player_response, other_response;

    std::array<std::int16_t, 31> climb_by_speed{}; // $11ABC8: speed change from the climb angle
    std::array<std::int16_t, 31> sink_by_speed{};  // $11AB88: sink at airspeed / 128
    std::array<std::int16_t, 15> bank_turn_by_pitch{}; // $12269A: fade of the bank turn, pitch / 256

    // sqrt(1 + (i / 256)^2) x $4000 for i = 0 .. 256 ($110928): the vector
    // length estimate.
    std::array<std::uint16_t, 257> length_factor{};
};

FlightTables read_flight_tables(const amiga::LoadedImage& program);

} // namespace interceptor::game
