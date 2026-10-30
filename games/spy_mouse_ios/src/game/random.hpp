// The C library random generator of iPhone OS (Darwin's rand(): Park-Miller "minimal standard",
// Schrage's method, 32-bit state, unseeded state 1). The original seeds it once with time()
// (Engine::initEngine) and every random choice in the game comes from it, so the port draws
// the same numbers in the same order.
#pragma once

#include <cstdint>

namespace doc::game {

class GameRandom {
public:
    void seed(std::uint32_t s) { state_ = s; }
    int next();
    std::uint32_t state() const { return state_; }
    std::uint64_t draws() const { return draws_; }

    // randWithinRange(float, float) @0x19d6e4: a + (b - a) * rand() / 2^31 (one draw).
    float within(float a, float b) { return ((b - a) * float(next())) * 4.65661287e-10f + a; }

private:
    std::uint32_t state_ = 1;
    std::uint64_t draws_ = 0;
};

// randWithinRange(float, float, int) @0x19c7d4: the same mapping for a value drawn earlier.
inline float randomWithinFrom(float a, float b, int value) {
    return ((b - a) * float(value)) * 4.65661287e-10f + a;
}

}  // namespace doc::game
