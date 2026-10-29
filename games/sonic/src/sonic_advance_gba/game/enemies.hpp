#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// The enemies of the layouts' third list (creators 080B823C by kind).
namespace sonic_advance_gba {

// Creates the enemy whose creator routine is `creator`; false if that
// enemy is not reconstructed.
bool create_enemy(Game& g, u32 creator, const ObjectSpawn& o);

} // namespace sonic_advance_gba
