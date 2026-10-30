#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include <array>
#include <cstdint>

// The fixed tables the race's arithmetic reads from the game program (game.md,
// "Car physics"): trigonometry, the body-frame transform recipe, angle limits
// and a few per-shape gains. All angles count 65536 to the turn.
namespace stuntcar::race {

struct Tables {
    // A quarter wave of the cosine, 1.0 = $FFFF, sampled every 32 units.
    std::array<std::uint16_t, 258> quarter_wave{};
    // atan of ratio/65536 in the first eighth, and the hypot correction
    // (max + min * hypot_gain >> 16), both indexed by the ratio >> 5.
    std::array<std::int16_t, 2048> atan{};
    std::array<std::uint16_t, 2048> hypot_gain{};
    // Half the angle of a road slope 0-255 (two entries per slope step).
    std::array<std::uint8_t, 128> slope_half_angle{};
    // Which orientation-matrix element each term of the body/world transforms
    // uses (velocity to body, force to world, spin to world).
    std::array<std::uint8_t, 24> transform_terms{};
    // Pitch and roll limits: positive and negative, for normal driving and
    // for the alternative set the game selects in one case.
    std::array<std::int16_t, 4> angle_limits{};
    // The auto-steer's heading bias for straight, curved and mirrored-curved pieces.
    std::array<std::int16_t, 3> heading_bias{};
    // Curve pieces: how fast the placement's radius estimate converges, by shape.
    std::array<std::uint8_t, 16> radius_gain{};
};

Tables load_tables(const data::Program&);

// The game's sine and cosine (quarter-wave table, linear interpolation;
// 1.0 = $7FFF).
std::int16_t sine(const Tables&, std::int16_t angle);
std::int16_t cosine(const Tables&, std::int16_t angle);

// The game's atan2 of a plan vector (0 along +y, a quarter turn along +x) and
// its hypot, which reuses the ratio the atan2 computed.
struct Polar {
    std::int16_t angle{};
    std::int16_t length{};
};
Polar polar(const Tables&, std::int16_t x, std::int16_t y);

} // namespace stuntcar::race
