// Terrain: the block under a point and its properties (6FA6, 709A), and
// objects standing on the floor (0384 -> 713D, 670D).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// Block property records are in bank 0E: a flags byte (bits 0-4 the kind,
// 5 an alternative record 7 bytes on for objects with byte 25 set, 6 and 7
// solid from above or below), a second byte, the floor profile's address,
// the wall profile's address.
constexpr unsigned properties_bank = 0x0E;
// 7188: what each terrain kind (flags & 1F) does to an object (713D).
} // namespace

// 6FA6: the block at the object's position plus (dx, dy) (the probe's Y
// is 12 lower; above the map it is 0): its map cell, number, flags and
// the floor and wall profile values at the point. Outside the map (C000-
// CFFF) everything reads as empty (7122). Tails in states 18 and 27 (and 43
// in zone 2) treats blocks A7-A9 as block 1. Objects with a path (byte 25)
// use a block's alternative record where it has one.
void Game::probe_terrain(const Object& o, unsigned dx, unsigned dy) {
    probe.x = std::uint16_t(o.x + dx);
    const unsigned column = (probe.x >> 5) & 0xFF;
    unsigned y = (o.y + dy + 0x12) & 0xFFFF;
    if (y & 0x8000) {
        y = 0;
    }
    probe.y = std::uint16_t(y);
    // Read as the CPU sees it: with no act loaded (the ending) the table's
    // address is 0 and the offsets come from the program's first bytes.
    const unsigned row = cpu_word(0x01, level.row_table + u8((y >> 5) * 2));
    const unsigned address = (row + column + 0xC001) & 0xFFFF;
    if ((address & 0xF000) != 0xC000) {
        probe_outside(); // 7122
        return;
    }
    probe.cell = std::uint16_t(address - 0xC000);
    u8 block = level_map.cells[probe.cell];
    if (o.type == 2 &&
        (o.state == 0x18 || o.state == 0x27 || (o.state == 0x43 && flow.zone == 2)) &&
        (block == 0xA7 || block == 0xA8 || block == 0xA9)) {
        block = 0x01;
    }
    probe.block = block;
    unsigned record = rom.word(properties_bank, act.block_properties + block * 2u);
    probe.flags = rom.byte(properties_bank, record);
    if ((probe.flags & 0x20) && o.path) {
        record += 7;
        probe.flags = rom.byte(properties_bank, record);
    }
    probe.attribute = rom.byte(properties_bank, record + 1);
    const unsigned floor = rom.word(properties_bank, record + 2);
    probe.floor = rom.byte(properties_bank, floor + (probe.x & 0x1F));
    const unsigned wall = rom.word(properties_bank, record + 4);
    probe.wall = rom.byte(properties_bank, wall + (probe.y & 0x1F));
}

// 709A: only the block's flags (returned); the column uses all 16 bits of
// X / 32 here.
u8 Game::probe_flags(const Object& o, unsigned dx, unsigned dy) {
    probe.x = std::uint16_t(o.x + dx);
    const unsigned column = probe.x >> 5;
    unsigned y = (o.y + dy + 0x12) & 0xFFFF;
    if (y & 0x8000) {
        y = 0;
    }
    probe.y = std::uint16_t(y);
    // Read as the CPU sees it: with no act loaded (the ending) the table's
    // address is 0 and the offsets come from the program's first bytes.
    const unsigned row = cpu_word(0x01, level.row_table + u8((y >> 5) * 2));
    const unsigned address = (row + column + 0xC001) & 0xFFFF;
    if ((address & 0xF000) != 0xC000) {
        probe_outside();
        return probe.flags;
    }
    probe.cell = std::uint16_t(address - 0xC000);
    probe.block = level_map.cells[probe.cell];
    const unsigned record = rom.word(properties_bank, act.block_properties + probe.block * 2u);
    probe.flags = rom.byte(properties_bank, record);
    return probe.flags;
}

// 7122
void Game::probe_outside() {
    probe.cell = 0x0FFF;
    probe.block = 0xFF;
    probe.flags = 0;
    probe.wall = 0;
    probe.floor = 0;
}

// 713D (0384): an object on the terrain: the floor under it (670D) and the
// block kind's effect (table 7188). The players are not checked while more
// than A8 lines below the camera.
void Game::object_terrain(Object& o) {
    o.terrain &= u8(~0x02);
    o.status &= u8(~0x40);
    const bool player = o.type == 1 || o.type == 2;
    if (player && (hi(o.screen_y) || lo(o.screen_y) >= 0xA9)) {
        probe.flags = 0;
        probe.block = 0;
    } else {
        probe_terrain(o, 0, 0); // 6FA6
        land_on_floor(o);       // 670D
    }
    switch (probe.flags & 0x1F) { // 7188
    case 0x00:                    // 71C7
        if (!(o.flags & 0x01)) {
            o.flags |= 0x10;
        }
        break;
    case 0x03: // 71D2
        o.status |= 0x40;
        break;
    case 0x1F:
        throw Unrecovered("terrain kind 1F (table 7188 runs past its end)");
    default: // 71C6, 71D1, 71D7: nothing
        break;
    }
}

// 670D: with a floor (flags bit 7) or a top surface (bit 6, only while not
// rising) the object stands on the profile's height: pushed up to it and
// marked as landed (65F3). A profile value of 0 (or 20) means the block
// above decides (6671).
void Game::land_on_floor(Object& o) {
    const u8 flags = probe.flags;
    if (flags & 0x80) {
        // 6717
        if ((probe.floor & 0x3F) == 0x20) {
            check_block_above(o); // 6671
        }
        const unsigned depth = (probe.y & 0x1F) + (probe.floor & 0x3F);
        if (u8(depth) < 0x20) {
            return;
        }
        o.y = std::uint16_t(o.y - (u8(depth) - 0x20));
        landed(o);
        return;
    }
    if (!(flags & 0x40)) {
        return;
    }
    // 6747
    if (o.vy & 0x8000) {
        return;
    }
    if (!(probe.floor & 0x3F)) {
        check_block_above(o);
    }
    const u8 reach = u8(hi(o.vy) + 7);
    const u8 depth = u8((probe.y & 0x1F) + probe.floor);
    if (depth < 0x20) {
        return;
    }
    const u8 into = u8(depth - 0x20);
    if (into >= reach) {
        return;
    }
    o.y = std::uint16_t(o.y - into);
    landed(o);
}

// 65F3: on the floor (bit 1 of byte 22); Tails also clears DBCE.
void Game::landed(Object& o) {
    o.terrain |= 0x02;
    if (o.type == 2) {
        player.tails_landing = 0;
    }
}

// 6671: the block above the probed one (one map row up): when it is solid
// from above (or of kind 3) the object is pushed up by its floor value
// there; when only solid from above, its floor value (plus 20) is added
// to D415.
void Game::check_block_above(Object& o) {
    // 6681: no check: above the map's first row the read lands below
    // C000, in the cartridge with bank 0E just paged into slot 2 (667B).
    const unsigned address = (0xC000u + probe.cell + level.row_up) & 0xFFFF;
    if (address >= 0xD000) {
        throw Unrecovered("block above read from RAM past the map (6681)");
    }
    const u8 block =
        address >= 0xC000 ? level_map.cells[address - 0xC000] : cpu_byte(properties_bank, address);
    unsigned record = rom.word(properties_bank, act.block_properties + block * 2u);
    if ((rom.byte(properties_bank, record) & 0x20) && o.path) {
        record += 7;
    }
    const u8 flags = rom.byte(properties_bank, record);
    if ((flags & 0x1F) == 0x03 || (!(flags & 0x40) && (flags & 0x80))) {
        // 66B2
        probe.attribute = rom.byte(properties_bank, record + 1);
        const unsigned floor = rom.word(properties_bank, record + 2);
        const u8 v = rom.byte(properties_bank, floor + (probe.x & 0x1F)) & 0x3F;
        if (v) {
            o.y = std::uint16_t(o.y - v);
        }
        return;
    }
    if (!(flags & 0x40)) {
        return;
    }
    // 66E5
    if ((flags & 0x1F) == 0x09) {
        return;
    }
    const unsigned floor = rom.word(properties_bank, record + 2);
    const u8 v = rom.byte(properties_bank, floor + (probe.x & 0x1F)) & 0x3F;
    if (v) {
        probe.floor = u8(probe.floor + 0x20 + v);
    }
}

} // namespace triple_trouble_gg
