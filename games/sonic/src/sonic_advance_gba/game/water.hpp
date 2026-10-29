#pragma once
#include "sonic_advance_gba/game/types.hpp"
#include <array>

// Water (Ice Paradise): the water's level, its surface drawn as a row of
// waves, and the picture split at the water's line: below it the colours
// are the underwater ones, layer 0 is off and the far layer waves
// (0804C5A4-0804C9F8).
namespace sonic_advance_gba {

class Game;
class Process;

struct WaterState {
    s16 target = -1;      // 03006084 the level it moves to (a pixel every other frame)
    u8 line = 0xFF;       // 03006081 the water's line on screen (0: all under, FF: below)
    u8 shown_line = 0xFF; // 03006080 the line of the last picture
    u32 tint{};           // 0300608C two colours mixed into the underwater ones
    u16 options{};        // 03006088 bit 8 from the act start
    // 03005E80, 03005C80: the background and sprite colours under water.
    std::array<u16, 256> under_bg{}, under_sprites{};
    Process* process{}; // 03006090
};

// 0804C5A4: the act's water level (negative: none), the underwater tint and
// options; with water, its process starts.
void set_water(Game& g, s16 level, u32 tint, u32 options);
// 0804C3D8: the underwater colours from the current ones: each halfway to
// the tint, then halfway back.
void build_underwater_colors(Game& g);
// 0804C564: the water's colours (animation 354h's colour command).
void load_water_colors(Game& g);

} // namespace sonic_advance_gba
