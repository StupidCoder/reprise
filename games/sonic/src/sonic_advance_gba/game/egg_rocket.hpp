#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// The objects first placed in zone 6: act 10 (its title card: Egg Rocket)
// and act 11 (Cosmic Angel), and act 10's rocket (its stages falling away
// at the checkpoints).
namespace sonic_advance_gba {

// Creates the object whose creator routine is `creator`; false if it is
// not one of these or not reconstructed.
bool create_egg_rocket_object(Game& g, u32 creator, const ObjectSpawn& o);
// The same for the enemies.
bool create_egg_rocket_enemy(Game& g, u32 creator, const ObjectSpawn& o);

// 080215D8's act-10 part: the player passed checkpoint `checkpoint` (1:
// the rocket lifts off, 08029830; 2 and 3: a stage falls away below the
// act's height `bottom`, 08028C54).
void rocket_checkpoint(Game& g, u8 checkpoint);

} // namespace sonic_advance_gba
