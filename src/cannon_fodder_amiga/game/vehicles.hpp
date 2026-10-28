#pragma once
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/world.hpp"

// Vehicles (.analysis/cannon_fodder_amiga/kinds.md "Vehicles"): jeeps and
// armed jeeps, tanks, gun turrets and helicopters, for the recruits to
// drive or for the enemy, and the missiles they fire.
//
// A vehicle is placed with helper objects in the next slots (its shadow,
// a helicopter's rotor, the piece of kind $29 drawn in front of it) and
// keeps them in place itself. The recruits get in by walking to it (the
// squad's leader first; the others are put in after a while), drive it by
// holding the left button (it heads for the pointer) and fire with the right
// button; a click on the vehicle lets them out again.
namespace fodder::game {

// Kinds $3F, $40 (the recruits' jeeps, types 2 and 3) and $50-$52 (the
// enemy's, types 2, 3 and 10): $99F92 / $98A40 around $9BBAA.
bool update_jeep(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);
// Kinds $41 (the recruits') and $45 (the enemy's): tanks, $999AA / $99668
// around $999E6.
bool update_tank(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);
// Kinds $4E, $4F (the recruits', types 0, 1) and $54, $55, $69 (the
// enemy's, types 0, 1, 9): gun turrets, $98B6A.
bool update_turret(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);
// Kinds $31-$34 and $65-$68 (the recruits', types 5-8) and $28, $2A-$2C,
// $6B (the enemy's): helicopters, $9A954 / $9ADC0 around $955E0.
bool update_helicopter(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);
// Kinds $2D (`$9B886`) and $2E, $61 (homing, `$9BA52`): missiles.
bool update_missile(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);
// Kind $4D (`$99E5C`): a jeep's gun shot; kind $47 (`$994BC`) likewise.
bool update_heavy_shot(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);
bool update_spear(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);
// Kind $35 (`$9A694`): a shot-down helicopter's rotor flying off, sinking,
// smoking, exploding where it lands.
bool update_falling_rotor(World& world, const Terrain& terrain, const ProgramTables& tables, int slot);

// `$9A99A`: an object of another side a homing missile can be aimed at
// within 22 of the pointer, or -1.
int homing_target_near_pointer(const World& world, const ProgramTables& tables, int side);

// The recruits and their vehicles, once a tick before the objects:
// `$9A8FC` (the boarding counts), `$9A730` (the selected squad's vehicle).
void update_boarding(World& world);
// A soldier in a vehicle (`$9A82C`): hidden at it, thrown out when it is
// destroyed. True when handled.
bool ride(World& world, const ProgramTables& tables, int slot);
// A soldier on foot (`$9A87E`): close enough to the vehicle he walks to,
// he gets in.
void board(World& world, const ProgramTables& tables, int slot);
// States $5A/$5B (`$95A28`): put into the vehicle with a puff.
int board_by_puff(World& world, int slot);

// The left button while the selected squad is in a vehicle (`$9EDDC`,
// `$9EF96`): a click on the vehicle lands a flying helicopter or lets the
// squad out; holding the button elsewhere drives towards the pointer.
// `pointer_x/y` are map positions.
void drive(World& world, const Terrain& terrain, bool press, int pointer_x, int pointer_y, const HitTables& hits);
// `$9EEB0` (in the walk order): a click on one of the recruits' vehicles
// standing on the ground: the squad walks there to get in.
void choose_vehicle(World& world, int x, int y, const HitTables& hits);
// `$9F4F4`: a right-button press while in a vehicle: fire at the pointer.
void fire_vehicle(World& world);

} // namespace fodder::game
