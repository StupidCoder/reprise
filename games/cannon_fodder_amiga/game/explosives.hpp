#pragma once
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/world.hpp"

// Grenades, rockets and explosions (.analysis/cannon_fodder_amiga/game.md
// "Grenades, rockets and explosions").
//
// A grenade (kind 2) stays in the thrower's hand for 4 ticks, then flies
// towards the aim point with its shadow (kind 3), bounces, stops in water or
// grass, and goes off when its fuse runs out or it lands in a hole. A rocket
// (kind $21) rides on the launcher's shoulder for 6 ticks, then flies
// straight at the aim point and goes off near it or against a wall. Both
// become an explosion (kind $C): for 7 frames it hurts everything in a
// shrinking box, throws debris (kind $2F, trailing smoke $30) or a splash
// in water ($56/$57), and marks the map for damage. A second kind of
// explosion ($27) comes from map tiles that blow up.
namespace fodder::game {

// $97D88 / $9B4FA: false when the throw or launch cannot happen (no aim,
// two already in the air for the side, no free pair of slots, the squad out
// of grenades or rockets, an enemy's aim beyond 130).
bool throw_grenade(World& world, const ProgramTables& tables, int thrower);
bool launch_rocket(World& world, const ProgramTables& tables, int launcher);

// Updates by kind; false when something not rebuilt yet happens.
bool update_grenade(World& world, const Terrain& terrain, const ProgramTables& tables, int slot); // 2
void update_follower(World& world, int slot);                                                     // 3 (shadows)
bool update_rocket(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);  // $21
bool update_explosion(World& world, const ProgramTables& tables, int slot);                       // $C
bool update_tile_explosion(World& world, const ProgramTables& tables, int slot);                  // $27
void update_debris(World& world, const ProgramTables& tables, int slot);                          // $2F
void update_smoke(World& world, int slot);                                                        // $30
void update_splash(World& world, int slot);                                                       // $56, $57

// $AAFCE (from the tile drawer, once a tick): the next queued blast point
// whose tile has a damaged form (the tile sets' .swp) changes the map; on
// screen one change a tick, with an explosion of its own ($9B25A). Returns
// the changed cell (-1 none) for the view to redraw.
int apply_map_damage(World& world, Terrain& terrain, const BlastTables& tables);

} // namespace fodder::game
