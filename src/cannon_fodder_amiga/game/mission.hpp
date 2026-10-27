#pragma once
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/world.hpp"

// The mission around the objects (.analysis/cannon_fodder_amiga/game.md
// "Mission"): the squads' lists rebuilt each tick, losing (no recruit left)
// and winning (the phase's objectives met), and the enemy soldiers that huts
// send out.
namespace fodder::game {

// $9E0EC at the mission's start: the phase's objectives and the enemies'
// skill range; every enemy placed on the map gets its skill.
void start_mission(World& world, const MissionTables& tables, int phase);

// $9DC18, once a tick: each squad's members (the recruits on the map that
// are alive or in a vehicle) in roster order, and the enemies' list of all
// of them; no recruit left loses the mission.
void rebuild_squads(World& world);

// $9DD64, once a tick: buildings left; the mission is won when every
// objective of the phase that can be counted is met (all enemies dead, all
// buildings destroyed, no hostage or leader left in place, the civilian
// home).
void check_objectives(World& world);

// $9EB36 + $A1136: a left click on the map orders the selected squad there.
// The route ends at the target; each soldier heads for the place of the one
// ahead of him (a snake behind the leader). Clicks within 8 ticks of the last
// add waypoints to the route instead.
void order_walk(World& world, int x, int y);
// $A1248, once a tick: the clicks' windows run down.
void tick_route_windows(World& world);

// $9F9C6: the selected squad's new leader, the first member on his feet
// (one in a vehicle only when no other follows); none: no squad selected.
void choose_leader(World& world);

// $949F0: an enemy soldier comes out of `from` (a hut); false when ten
// are out already, the mission is won, or no slot is free.
bool spawn_enemy(World& world, int from);

// $9E150: the next enemy's skill, bouncing between the phase's minimum and
// a maximum that grows with every 16 enemies sent out.
int next_enemy_skill(World& world);

} // namespace fodder::game
