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
// $9F0E8: a click on a recruit (map position): select his squad or join it.
bool click_recruit(World& world, int x, int y);

// $949F0: an enemy soldier comes out of `from` (a hut); his slot, or -1
// when ten are out already, the mission is won, or no slot is free.
int spawn_enemy(World& world, int from);

// $9E150: the next enemy's skill, bouncing between the phase's minimum and
// a maximum that grows with every 16 enemies sent out.
int next_enemy_skill(World& world);

// $8CC5C, once a tick: the end of a phase. On its first tick the end
// texts rise from below the view (won: "PHASE"/"MISSION" + "COMPLETE";
// lost with recruits left or a failed objective: "TRY" + "AGAIN"; no
// recruit left: "GAME OVER") in slots 40-42, and the end count starts
// (100 ticks, 50 after a failed objective). True once the count has run out.
enum class EndText { none, phase_complete, mission_complete, try_again, game_over };
bool run_mission_end(World& world, bool last_phase_of_mission, bool recruits_left, EndText* started = nullptr);

// $948BE … $949CA (kinds $1C-$1E, $22, $3A, $3B): an end text keeps its
// column relative to the view and rises 32 a tick to its line.
void update_end_text(World& world, int slot);

} // namespace fodder::game
