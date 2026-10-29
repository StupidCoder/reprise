#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// The objects first placed in Casino Paradise (zone 3).
namespace sonic_advance_gba {

// Creates the object whose creator routine is `creator`; false if it is
// not one of these or not reconstructed.
bool create_casino_paradise_object(Game& g, u32 creator, const ObjectSpawn& o);
// The same for the enemies.
bool create_casino_paradise_enemy(Game& g, u32 creator, const ObjectSpawn& o);

} // namespace sonic_advance_gba
