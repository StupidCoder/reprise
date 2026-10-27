#pragma once
#include "cannon_fodder_amiga/game/tables.hpp"
#include "cannon_fodder_amiga/game/walking.hpp"
#include "cannon_fodder_amiga/game/world.hpp"

// The object pass of a game tick ($953B4): each object in slot order by its
// kind ($94608). Objects with a negative x are passed over (free slots).
namespace fodder::game {

// One object's update; false when its kind (or its situation) is not
// rebuilt yet, in which case the object is left as it was.
bool update_object(World& world, Terrain& terrain, const ProgramTables& tables, int slot);

// The whole pass; returns how many updates were not handled.
int update_objects(World& world, Terrain& terrain, const ProgramTables& tables);

} // namespace fodder::game
