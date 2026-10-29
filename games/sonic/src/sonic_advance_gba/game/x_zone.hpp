#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// Act 12 (its title card: X-Zone): the falling platforms and the three
// bosses met one after another (enemy kinds 28-30).
namespace sonic_advance_gba {

// Creates the object whose creator routine is `creator`; false if it is
// not one of these or not reconstructed.
bool create_x_zone_object(Game& g, u32 creator, const ObjectSpawn& o);
// The same for the enemies (the bosses).
bool create_x_zone_enemy(Game& g, u32 creator, const ObjectSpawn& o);

} // namespace sonic_advance_gba
