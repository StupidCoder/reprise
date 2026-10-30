#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/race/car.hpp"
#include "stunt_car_racer_amiga/race/contact.hpp"
#include "stunt_car_racer_amiga/race/tables.hpp"

// A race in progress (game.md, "The race"): the player's car lowered onto the
// circuit by the crane, then driven frame by frame as the game's race loop
// does it: controls, physics, placement, clock.
namespace stuntcar::race {

// The figures of the player's car (a table of two cars at $1FE6C: the
// standard one and the super league's).
struct CarSpec {
    std::uint8_t accelerate_low{}, accelerate_high{};
    std::uint8_t boost_reload{};
    std::uint8_t damage_tolerance{};
};
CarSpec car_spec(const data::Program&, bool super_league);

class Race {
  public:
    Race(const data::Program&, const data::Circuits&, int track, bool super_league = false);

    // One frame of the race loop.
    void step(const Controls&);

    const Car& player() const { return car_; }
    const data::Track& track() const { return track_; }
    const Tables& tables() const { return tables_; }

  private:
    void spawn(int segment);

    const data::Track& track_;
    Tables tables_;
    TrackContact contact_;
    Car car_;
    std::array<std::uint8_t, 16> start_allowed_{}; // by shape kind
};

} // namespace stuntcar::race
