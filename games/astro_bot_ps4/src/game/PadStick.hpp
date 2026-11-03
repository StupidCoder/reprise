// Analog stick values as the game's pad layer hands them on (0xc89950, called by the pad reader
// 0xc87230 for both sticks with dead zone 0.1 and full range 1.0 from 0x1301ba0).
#pragma once

#include <cstdint>

namespace beacon::game {

struct PadStick {
    float x = 0, y = 0;   // right +x, down +y (raw pad orientation); length up to sqrt(2)
};

// Raw byte axes (0..255, centre 127.5) to the shaped stick: s = (v - 127.5) / 127.5 per axis;
// |s| below the dead zone gives 0, otherwise s keeps its direction and is scaled by
// (|s| - dead) / (full - dead), so the length grows like |s| (|s| - 0.1) / 0.9.
PadStick padStick(std::uint8_t x, std::uint8_t y);

}   // namespace game
