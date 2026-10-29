#pragma once
#include "sonic_advance_gba/game/types.hpp"

// The act's rings (08007D30-08008784): a layout of ring positions in cells
// of 256x256 pixels, decompressed from the cartridge at the start of the
// act; touching a ring collects it (marked in the layout copy), the
// visible ones are drawn with one shared animated sprite.
namespace sonic_advance_gba {

class Game;

// 08007D30
void start_rings(Game& g);
// 0801E43C: the sparkle of a collected ring at act position (x, y), with
// its sound (at most 8 at a time).
void create_ring_sparkle(Game& g, s16 x, s16 y);

} // namespace sonic_advance_gba
