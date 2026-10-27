#pragma once
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/world.hpp"

// Shooting (.analysis/cannon_fodder_amiga/game.md "Shooting").
//
// While the right button is held the selected squad fires at the pointer:
// every few frames the next member in the squad's firing order gets his
// turn, and on his update he fires a bullet (an object of kind 6). For its
// first ticks the bullet is the muzzle flash at the shooter; then it flies
// towards the aim point with a little spread until its lifetime runs out, a
// blocking tile stops it, or it hits: a player's bullet tests the objects
// whose kind is hittable, an enemy's bullet tests the player's soldiers. A
// spent or hitting bullet shows its impact frames and frees its slot.
namespace fodder::game {

// Once per tick ($9E92A, $86338): the squad's weapon by the first recruit's rank;
// the hold timer runs down.
void update_fire_control(World& world, const FireTables& tables);

// Once per frame while the right button is held ($9F454 / $9E65C): the
// squad is told to fire at the pointer, and the member whose turn it is gets
// ready. first_press: the button went down this frame.
void hold_fire(World& world, const FireTables& tables, bool first_press);

// $9766C: `shooter` fires a bullet at his aim point. False when he cannot
// (no aim, no free slot, 12 bullets in the air, the leader throwing).
bool fire_bullet(World& world, const FireTables& tables, int shooter);

// $98104: the highest free slot below 43, cleared; -1 when there is none.
int allocate_slot(World& world);
// $96D2C: the object leaves at the next draw list.
void free_object(World& world, int slot);

// Effects of a hit soldier: blood ($94AB2, kind $17) and the falling body's
// shadow ($97AFC, kind $16, only for bodies on screen), and their updates
// ($94AFE, $95444).
void spawn_blood(World& world, int body);
void cast_shadow(World& world, int body);
void update_blood(World& world, int slot);
void update_shadow(World& world, int slot);

// One bullet's update ($97878); false when something not rebuilt yet happens.
bool update_bullet(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);

} // namespace fodder::game
