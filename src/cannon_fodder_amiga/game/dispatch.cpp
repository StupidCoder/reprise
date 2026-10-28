#include "cannon_fodder_amiga/game/dispatch.hpp"
#include "cannon_fodder_amiga/game/combat.hpp"
#include "cannon_fodder_amiga/game/explosives.hpp"
#include "cannon_fodder_amiga/game/mission.hpp"
#include "cannon_fodder_amiga/game/scenery.hpp"
#include "cannon_fodder_amiga/game/vehicles.hpp"

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
    case 0x38: update_trap(world, tables, slot); return true;
    case 0x36: update_mine(world, tables, slot); return true;
    case 0x37: update_trip_mine(world, tables, slot); return true;
    case 0x58: case 0x64: update_enemy_hole(world, terrain, tables, slot); return true;
    case 0x4a: case 0x4b: case 0x4c: update_civilian_building(world, terrain, tables, slot); return true;
    case 0x1c: case 0x1d: case 0x1e: case 0x22: case 0x3a: case 0x3b: update_end_text(world, slot); return true;
    case 0x3f: case 0x40: case 0x50: case 0x51: case 0x52: return update_jeep(world, terrain, tables, slot);
    case 0x41: case 0x45: return update_tank(world, terrain, tables, slot);
    case 0x4e: case 0x4f: case 0x54: case 0x55: case 0x69: return update_turret(world, terrain, tables, slot);
    case 0x31: case 0x32: case 0x33: case 0x34: case 0x65: case 0x66: case 0x67: case 0x68:
    case 0x28: case 0x2a: case 0x2b: case 0x2c: case 0x6b: return update_helicopter(world, terrain, tables, slot);
    case 0x2d: case 0x2e: case 0x61: return update_missile(world, terrain, tables, slot);
    case 0x4d: return update_heavy_shot(world, terrain, tables, slot);
    case 0x35: return update_falling_rotor(world, terrain, tables, slot);
    case 0x3d: case 0x3e: case 0x46: case 0x53: return update_civilian(world, terrain, tables, slot);
    case 0x44: case 0x5b: return update_bystander(world, terrain, tables, slot);
    case 0x48: case 0x6a: return update_hostage(world, terrain, tables, slot);
    case 0x49: update_rescue_tent(world, slot); return true;
    case 0x5a: update_civilian_house(world, terrain, tables, slot); return true;
    case 0x5c: update_booby_trap(world, tables, slot); return true;
    case 0x5d: case 0x5e: case 0x5f: case 0x60: case 0x62: update_power_up(world, tables, slot); return true;
    case 0x63: update_rescue_marker(world, tables, slot); return true;
    case 0x6c: case 0x6d: case 0x6e: update_sturdy_tree(world, slot); return true;
    case 0x47: return update_spear(world, terrain, tables, slot);
    case 0x12: update_flicker(world, slot); return true;
    case 0x13: update_drifter(world, slot); return true;
    case 0x1a: update_animated(world, slot); return true;
    case 0x29: update_vehicle_piece(world, slot); return true;
    case 0x39: update_puff(world, slot); return true;
    case 0x59: return update_explosion(world, tables, slot);
    case 0x24: return update_bazooka(world, terrain, tables, slot);
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
