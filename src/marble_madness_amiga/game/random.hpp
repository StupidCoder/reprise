#pragma once
#include <array>
#include <cstdint>
#include <deque>

// The game's random numbers ($109136): a lagged additive generator over 13
// longs ($109288, two running indices $1092BC and $1092BE) whose two lowest
// bits come from the video beam's position (graphics.library VBeamPos), so
// small ranges are decided by timing alone. The port keeps the generator
// and draws those two bits from its own source; replays of the original
// feed the bits it logged.
namespace marble_madness::game {

class Random {
public:
    // The generator as the program file leaves it ($109288, indices 0
    // and 0); the menus before a race advance it further.
    static Random from_program(const std::array<std::uint32_t, 13>& state, int first = 0, int second = 0) {
        Random r;
        r.state_ = state;
        r.first_ = first;
        r.second_ = second;
        return r;
    }

    // A number in 0 .. range - 1.
    std::uint32_t next(std::uint16_t range) {
        const int a = first_, b = second_;
        first_ = a + 1 >= 13 ? a + 1 - 13 : a + 1;
        second_ = b + 2 >= 13 ? b + 2 - 13 : b + 2;
        const std::uint32_t value = ((state_[std::size_t(a)] + state_[std::size_t(b)]) & ~3u) | beam_bits();
        const std::uint32_t difference = state_[std::size_t(b)] - state_[std::size_t(a)];
        state_[std::size_t(a)] = value;
        state_[std::size_t(b)] = difference;
        std::uint32_t mask = 1;
        while (mask < range) {
            mask <<= 1;
        }
        std::uint32_t result = value & (mask - 1);
        if (result >= range) {
            result -= range;
        }
        return result;
    }

    // Replays: the beam bits the original drew, in order.
    std::deque<int> forced_bits;
    // The port's own beam source (any seed will do).
    std::uint32_t beam_seed{0x1234567};

private:
    std::uint32_t beam_bits() {
        if (!forced_bits.empty()) {
            const int b = forced_bits.front();
            forced_bits.pop_front();
            return std::uint32_t(b & 3);
        }
        beam_seed ^= beam_seed << 13;
        beam_seed ^= beam_seed >> 17;
        beam_seed ^= beam_seed << 5;
        return beam_seed & 3;
    }

    std::array<std::uint32_t, 13> state_{};
    int first_{}, second_{};
};

} // namespace marble_madness::game
