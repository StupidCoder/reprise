#pragma once
#include <array>
#include <cstdint>

// The game's random numbers ($62574): five bytes shifted along one each
// draw, the new byte made from two of the older ones. The race draws from it
// every frame (the joystick routine and the engine sound stir it), so the
// smoke, the crack and the crane's waits depend on the whole race so far.
namespace stuntcar::race {

struct Random {
    // The race set-up stores $3B in the fourth byte ($5D7DE); the rest are
    // whatever earlier draws left.
    std::array<std::uint8_t, 5> bytes{0, 0, 0, 0x3B, 0};

    std::uint8_t next() {
        const auto older = std::uint16_t(bytes[2] << 8 | bytes[3]) >> 4;
        const auto oldest = std::uint16_t(bytes[0] << 8 | bytes[1]) >> 1;
        const auto drawn = std::uint8_t(older ^ oldest);
        bytes = {bytes[1], bytes[2], bytes[3], bytes[4], drawn};
        return drawn;
    }
};

} // namespace stuntcar::race
