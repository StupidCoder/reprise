#include "game/PadStick.hpp"

#include <cmath>

namespace beacon::game {

namespace {
constexpr float kDeadZone = 0.1f;
constexpr float kFullRange = 1.0f;
}   // namespace

PadStick padStick(std::uint8_t x, std::uint8_t y) {
    const float sx = 2 * (x / 255.0f - 0.5f), sy = 2 * (y / 255.0f - 0.5f);
    const float len = std::sqrt(sx * sx + sy * sy);
    if (len < kDeadZone) {
        return {};
    }
    const float k = (len - kDeadZone) / (kFullRange - kDeadZone);
    return {sx * k, sy * k};
}

}   // namespace game
