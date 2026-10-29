#pragma once
#include "sonic_advance_gba/game/types.hpp"

// The game over (08055D9C).
namespace sonic_advance_gba {

class Game;

// 08055D9C: over the stopped act; `mode` 2 the game over (the save is
// written, back to the SEGA logo), 1 the same before the continue screen,
// 4 the time over's words.
void start_game_over(Game& g, u8 mode);

} // namespace sonic_advance_gba
