#pragma once
#include "wizball_c64/data/assets.hpp"
#include <array>
#include <cstdint>

// Wizball's random numbers ($7533): the next byte of a 256-byte table (the
// game uses its own code at $7300 as the table; a counter at $7532 steps
// through it) XORed with the low byte of CIA 2's timer B, which counts the
// machine's cycles since the start. The timer makes the value depend on the
// exact cycle of each call, which a re-implementation cannot know; the game
// keeps the table and models the timer as a free-running counter advanced
// by an amount that varies from call to call. Checks against the original
// replay its recorded values instead (RandomSource is an interface).
namespace wizball::game {

class RandomSource {
public:
    virtual ~RandomSource() = default;
    virtual std::uint8_t next() = 0;
};

class GameRandom : public RandomSource {
public:
    explicit GameRandom(const data::GameImage& image);
    std::uint8_t next() override;

private:
    std::array<std::uint8_t, 256> table_{};
    std::uint8_t counter_{};
    std::uint32_t timer_{0xFFFF};
    std::uint32_t drift_{0x1234567};
};

} // namespace wizball::game
