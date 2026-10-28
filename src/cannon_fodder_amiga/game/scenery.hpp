#pragma once
#include <optional>
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/world.hpp"

// Objects of the landscape (.analysis/cannon_fodder_amiga/game.md
// "Scenery"): enemy huts that send soldiers out and blow away when hit,
// crates of grenades and rockets the leader picks up, bushes and trees that
// a blast throws into the air (they go off where they land), and animals
// that wander in tall grass.
namespace fodder::game {

void update_hut(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);      // $14, $19
void update_spawn_point(World& world, int slot);                                                  // $18
void update_trap(World& world, const ProgramTables& tables, int slot);                            // $38
void update_mine(World& world, const ProgramTables& tables, int slot);                            // $36
void update_trip_mine(World& world, const ProgramTables& tables, int slot);                       // $37
void update_enemy_hole(World& world, const Terrain& terrain, const ProgramTables& tables, int slot); // $58, $64
void update_civilian_building(World& world, const Terrain& terrain, const ProgramTables& tables, int slot); // $4A-$4C
void update_flicker(World& world, int slot);                                                      // $12
void update_drifter(World& world, int slot);                                                      // $13
void update_animated(World& world, int slot);                                                     // $1A
void update_vehicle_piece(World& world, int slot);                                                // $29
void update_puff(World& world, int slot);                                                         // $39
void update_rescue_tent(World& world, int slot);                                                  // $49
void update_civilian_house(World& world, const Terrain& terrain, const ProgramTables& tables, int slot); // $5A
void update_booby_trap(World& world, const ProgramTables& tables, int slot);                      // $5C
void update_power_up(World& world, const ProgramTables& tables, int slot);                        // $5D-$60, $62
void update_rescue_marker(World& world, const ProgramTables& tables, int slot);                   // $63
void update_sturdy_tree(World& world, int slot);                                                  // $6C-$6E
void count_rescue_markers(World& world);                                                          // $98488
void update_last_mission(World& world);                                                           // $984B6, $9837A
std::optional<int> leader_touch(const World& world, const GeometryTables& geometry, const GameObject& o); // $9B448
void update_house(World& world, int slot);                                                        // $3C
void update_crate(World& world, const ProgramTables& tables, int slot);                           // $25, $26
void update_bush(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);     // $D-$11
void update_bush_shadow(World& world, int slot);                                                  // $23
void update_animal(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);   // $1B
void update_bird(World& world, int slot);                                                         // $42, $43
// The rank marker over the selected squad's leader; a fallen leader is
// replaced ($9F9C6).
bool update_leader_marker(World& world, int slot);                                                // $15

} // namespace fodder::game
