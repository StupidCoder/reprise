#pragma once
#include "sonic_advance_gba/game/types.hpp"

// What appears where an enemy (or a breakable object) is destroyed: the
// explosion, the freed animal and the score with its rising number.
namespace sonic_advance_gba {

class Game;

// 0801FEA0: an explosion (animation 197) at act position (x, y).
void create_explosion(Game& g, s16 x, s16 y);
// 08054920: one of the zone's three animals (in turn) jumps out at (x, y).
void free_animal(Game& g, s16 x, s16 y);
// 080095CC: points for an enemy, more for each one in a row (the player's
// chain, up to the fifth); every 50000 points a life. The points rise from
// (x, y).
void award_enemy_points(Game& g, s16 x, s16 y);

} // namespace sonic_advance_gba
