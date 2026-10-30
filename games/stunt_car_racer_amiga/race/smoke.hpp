#pragma once
#include "stunt_car_racer_amiga/race/car.hpp"
#include "stunt_car_racer_amiga/race/random.hpp"
#include <array>
#include <vector>

// Dust and smoke in front of the car (game.md, "Smoke"): 32 particles
// thrown up from the bottom of the view while the car scrapes the road's
// edge or is wrecked, falling back under gravity; after going over the
// edge, 16 puffs of smoke rise from the wreck instead. Positions are in
// view pixels; the race screen draws them after the view, before the car.
namespace stuntcar::race {

struct Particle {
    std::int16_t x{}, y{}, vx{}, vy{};
};

struct Smoke {
    std::array<Particle, 32> particles{};

    struct Spark {
        int x{}, y{}; // two pixels and two above, the upper right white
    };
    struct Puff {
        int x{}, y{}, size{}; // the puff's top left; size 0-7
    };
    // What this frame drew.
    std::vector<Spark> sparks;
    std::vector<Puff> puffs;

    Smoke() { clear(); }
    void clear();

    // One frame, as the drawing pass runs it after the view; `frame_clock`
    // is the race loop's down-counter, which varies the puffs.
    void step(const Car&, Random&, std::uint8_t frame_clock);

  private:
    void run(const Car&, Random&, int count, std::uint8_t strength, bool crashed, std::uint8_t frame_clock);
    bool draw(Particle&, int index, bool crashed, std::uint8_t frame_clock);
};

} // namespace stuntcar::race
