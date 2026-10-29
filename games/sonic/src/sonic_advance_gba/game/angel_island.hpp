#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// The objects first placed in Angel Island (zone 5).
namespace sonic_advance_gba {

// Creates the object whose creator routine is `creator`; false if it is
// not one of these or not reconstructed.
bool create_angel_island_object(Game& g, u32 creator, const ObjectSpawn& o);
// The same for the enemies.
bool create_angel_island_enemy(Game& g, u32 creator, const ObjectSpawn& o);
// The waterfall creator 080757F8's Angel Island variant (08075C04).
void create_holding_waterfall(Game& g, const ObjectSpawn& o);

} // namespace sonic_advance_gba
