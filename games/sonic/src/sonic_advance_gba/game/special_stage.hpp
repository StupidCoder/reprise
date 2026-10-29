#pragma once
#include "sonic_advance_gba/game/types.hpp"

// The special stages: entered by the special spring (08092EC8) through the
// "welcome" scene (08068654), a run down a tube (080298D8) collecting
// rings for an emerald, then the special stage's results (0805D014; after
// an emerald its scene, 08068CD8) and back to the act.
namespace sonic_advance_gba {

class Game;

// 08068654: the welcome scene (everything else has ended).
void start_special_stage_entry(Game& g);
// 080298D8: the special stage of the current act.
void start_special_stage(Game& g);

} // namespace sonic_advance_gba
