#pragma once
#include "sonic_advance_gba/game/types.hpp"

// The end of an act: the results and the next act.
namespace sonic_advance_gba {

class Game;

// 080573A0: the results for `rings` and the act's clock: the act's
// progress goes into the save, the bonuses are counted into the score,
// then the next act starts. Returns the frames the tally takes (plus 168h).
u32 start_results(Game& g, u16 rings, u32 clock);
// 0803D6F0: the next act (the save is written in between).
void next_act(Game& g);

} // namespace sonic_advance_gba
