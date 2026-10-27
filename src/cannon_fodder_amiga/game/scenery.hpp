#pragma once
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/world.hpp"

// Objects of the landscape (.analysis/cannon_fodder_amiga/game.md
// "Scenery"): enemy huts that send soldiers out and blow away when hit,
// crates of grenades and rockets the leader picks up, bushes and trees that
// a blast throws into the air (they go off where they land), and animals
// that wander in tall grass.
namespace fodder::game {

void update_hut(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);      // $14
void update_crate(World& world, const ProgramTables& tables, int slot);                           // $25, $26
void update_bush(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);     // $D-$11
void update_bush_shadow(World& world, int slot);                                                  // $23
void update_animal(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);   // $1B
void update_bird(World& world, int slot);                                                         // $42, $43
// The rank marker over the selected squad's leader; false when the leader
// has to be chosen anew ($9F9C6, not rebuilt yet).
bool update_leader_marker(World& world, int slot);                                                // $15

} // namespace fodder::game
