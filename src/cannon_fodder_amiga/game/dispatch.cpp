#include "cannon_fodder_amiga/game/dispatch.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/scenery.hpp"

namespace fodder::game {

bool update_object(World& world, Terrain& terrain, const ProgramTables& tables, int slot) {
    const auto& o = world.objects.slots[std::size_t(slot)];
    switch (o.kind) {
    case 0: return update_soldier(world, terrain, tables, slot);
    case 2: return update_grenade(world, terrain, tables, slot);
    case 3: update_follower(world, slot); return true;
    case 4: return true; // freed ($953E0 does nothing)
    case 5: return update_enemy(world, terrain, tables, slot);
    case 6: return update_bullet(world, terrain, tables, slot);
    case 0xc: return update_explosion(world, tables, slot);
    case 0xd: case 0xe: case 0xf: case 0x10: case 0x11: update_bush(world, terrain, tables, slot); return true;
    case 0x14: case 0x19: update_hut(world, terrain, tables, slot); return true;
    case 0x18: update_spawn_point(world, slot); return true;
    case 0x37: update_trap(world, tables, slot); return true;
    case 0x24: return update_bazooka(world, terrain, tables, slot);
    case 0x3d: return update_civilian(world, terrain, tables, slot);
    case 0x3c: update_house(world, slot); return true;
    case 0x15: return update_leader_marker(world, slot);
    case 0x16: update_shadow(world, slot); return true;
    case 0x17: update_blood(world, slot); return true;
    case 0x1b: update_animal(world, terrain, tables, slot); return true;
    case 0x21: return update_rocket(world, terrain, tables, slot);
    case 0x23: update_bush_shadow(world, slot); return true;
    case 0x25: case 0x26: update_crate(world, tables, slot); return true;
    case 0x27: return update_tile_explosion(world, tables, slot);
    case 0x2f: update_debris(world, tables, slot); return true;
    case 0x30: update_smoke(world, slot); return true;
    case 0x42: case 0x43: update_bird(world, slot); return true;
    case 0x56: case 0x57: update_splash(world, slot); return true;
    default: return false;
    }
}

int update_objects(World& world, Terrain& terrain, const ProgramTables& tables) {
    int unhandled = 0;
    for (int i = 0; i < object_slots; ++i) {
        const auto& o = world.objects.slots[std::size_t(i)];
        if (!o.used || o.removed || std::int16_t(std::uint32_t(o.x) >> 16) < 0) continue;
        if (!update_object(world, terrain, tables, i)) ++unhandled;
    }
    return unhandled;
}

} // namespace fodder::game
