#pragma once
#include "sonic_advance_gba/game/types.hpp"

// The act's view: its background layers and the camera that follows the
// player (0803E0B0-0803F6DC).
namespace sonic_advance_gba {

class Game;
struct StageState;

// 0803E0B0: the layers of act `zone`, its collision map, the camera at
// the player and the camera's process.
void set_up_act_view(Game& g, s8 zone);

// Act 13: the ground runs on under a still view, eight pixels a frame; the
// ground's map position at view position x (0803E690, 0804A084): the
// ground repeats every A80h pixels after the first 48h.
s32 moon_ground_x(const StageState& s, s32 x);

} // namespace sonic_advance_gba
