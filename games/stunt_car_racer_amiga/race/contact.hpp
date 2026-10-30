#pragma once
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/race/car.hpp"
#include "stunt_car_racer_amiga/race/tables.hpp"
#include <array>

// Where a car meets the circuit (game.md, "Car physics: road contact"): the
// road's height under each suspension point, sampled from the track's
// height profiles, and the track following that turns the car with the road.
namespace stuntcar::race {

// A segment as the race reads it: its shape's placement figures and its edges'
// height profiles.
struct Piece {
    int segment{};
    const data::Segment* layout{};
    const data::Shape* shape{};
    const data::Profile* left{};
    const data::Profile* right{};
    std::uint16_t heading{};   // quarter turns << 14
    bool reversed{};
    int points{};              // the shape's point count (two per cross-section)
    bool mirrored{};
    std::uint8_t placement{};  // bit 7 arc, bit 6 diagonal

    // A road edge's height at a point (even: left edge, odd: right) in units
    // of 32, wrapping as the game's 16-bit sum does.
    std::int16_t point_height(int point) const;
};

class TrackContact {
  public:
    TrackContact(const data::Circuits&, const data::Track&);

    Piece piece(int segment) const;
    int segment_count() const { return int(track_.segments.size()); }
    // The segment in a grid cell (16 x 16), or -1.
    int segment_at(int cell_x, int cell_z) const;

    // The road's height under each suspension point.
    void sample_surface(Car&);
    // Track following: turn the car with the road (yaw) and the steering
    // torque it gets from the joystick.
    void follow(Car&, const Tables&);

  private:
    const data::Circuits& circuits_;
    const data::Track& track_;
    std::array<std::int16_t, 256> grid_{};
};

} // namespace stuntcar::race
