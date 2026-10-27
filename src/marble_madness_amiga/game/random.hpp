#pragma once
#include <cstdint>
#include <random>

// The game's random numbers. The original ($109136) runs a lagged additive
// generator whose low bits come from the display beam's position, so its
// sequence depends on timing and cannot be matched; the port draws from a
// standard generator instead and keeps how a number in a range is made: the
// raw bits are masked to the next power of two at or above the range and a
// result past the range is reduced once (so for ranges other than powers
// of two the low results come up more often, as in the original).
namespace marble_madness::game {

class Random {
public:
    // The tools' races use the fixed seed, so their runs repeat; the game
    // seeds once per start of the program.
    static constexpr std::uint32_t default_seed = 0x4D4D;
    explicit Random(std::uint32_t seed = default_seed) : engine_(seed) {}
    static Random seeded() { return Random(std::random_device{}()); }

    // A number in 0 .. range - 1 (range 1-$10000).
    std::uint32_t next(std::uint32_t range) {
        std::uint32_t mask = 1;
        while (mask < range) {
            mask <<= 1;
        }
        std::uint32_t result = std::uint32_t(engine_()) & (mask - 1);
        if (result >= range) {
            result -= range;
        }
        return result;
    }

private:
    std::mt19937 engine_;
};

} // namespace marble_madness::game
