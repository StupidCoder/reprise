#include "game/random.hpp"

namespace doc::game {

int GameRandom::next() {
    ++draws_;
    if (state_ == 0) state_ = 123459876;
    std::int32_t hi = std::int32_t(state_ / 127773);
    std::int32_t lo = std::int32_t(state_ % 127773);
    std::int32_t x = 16807 * lo - 2836 * hi;
    if (x < 0) x += 0x7fffffff;
    state_ = std::uint32_t(x);
    return int(state_ % 0x80000000u);
}

}  // namespace doc::game
