#pragma once
#include "fa_18_interceptor_amiga/game/flight_tables.hpp"
#include <array>
#include <cstdint>

// Attitude arithmetic of the flight model, in the original's integer
// units so that a flight follows the original tick for tick: angles from
// 0 to $707F (80 per degree; pitch: nose up is just below a full turn),
// matrices of words where $4000 is 1, sines from the program's table.
namespace interceptor::game {

constexpr int full_turn = 0x7080;

struct Angles {
    std::int16_t pitch{}, heading{}, roll{};
    template <class A> void persist(A& ar) { ar(pitch, heading, roll); }
};

using Matrix = std::array<std::int16_t, 9>; // rows of three; $4000 = 1

struct SineCosine {
    std::int16_t sine{}, cosine{};
};

// sin and cos of an angle in tenths of a degree (0 .. 3599).
SineCosine sine_cosine(const FlightTables& tables, int tenths);

// The aircraft's own matrix from its angles ($122C52).
Matrix body_matrix(const FlightTables& tables, Angles angles);
// The matrix the view and the velocity use: the body matrix of the
// negated angles in the original's order ($122CEC). Its third column is
// the direction of flight.
Matrix view_matrix(const FlightTables& tables, Angles angles);

// One tick of rotation ($1226B8): turns `body` by the rates (pitch, yaw,
// roll; 1/80 degree) and reads the new angles back from the product with
// the arcsine tables, as the original does every tick.
Angles rotate(const FlightTables& tables, const Matrix& body, std::int16_t pitch_rate, std::int16_t yaw_rate,
              std::int16_t roll_rate);

// A vector of the given length along (x, y, z), the sign of `length`
// taken over ($119CB8 with the length estimate $1108C4). $119D50 is the
// same with a first shift of 3 instead of 8: 32 times the length.
std::array<std::int16_t, 3> scaled_vector(const FlightTables& tables, std::int16_t length, std::int16_t x, std::int16_t y, std::int16_t z,
                                          int first_shift = 8);

// `$1108C4`: the length estimate of a vector from its three sizes (the
// first two the flat pair), by two ratio lookups; at most $7FFF.
std::int16_t length_estimate(const FlightTables& tables, std::uint16_t a, std::uint16_t b, std::uint16_t c);

// The pitch and heading of the direction (x, y, z) ($10454A, its first
// part: the arctangents of the larger and smaller of each pair by the
// table, after a common scaling). Used to point an object at its target.
struct Direction {
    std::int16_t pitch{}, heading{};
};
Direction direction_of(const FlightTables& tables, std::int32_t x, std::int32_t y, std::int32_t z);

} // namespace interceptor::game
