#pragma once
#include "sonic_advance_gba/game/objects.hpp"

// The item boxes of the layouts' second list (0801E5D8-0801F14E).
namespace sonic_advance_gba {

// 0801E5D8
void create_item_box(Game& g, const ObjectSpawn& o);

} // namespace sonic_advance_gba
