// Collision with the level (bank 1, 6AB6-74B1).
//
// The level is made of 32x32 blocks. Each block number selects a terrain
// profile (bank 1E, table at D2D4): a type byte, a slope byte, two pointers
// to 32-byte tables (the floor height for each X within the block and the
// wall position for each Y) and a final word. Profile type 81 is solid
// ground, 00 empty; the others are special (see collide_floor). In a height,
// 0x20 means "continues into the next block" and bit 6 selects which side
// the surface faces.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
namespace contact_bit {
inline constexpr u8 ceiling = 0x01, floor = 0x02, right_wall = 0x04, left_wall = 0x08;
} // namespace contact_bit

// 6E46: the floor rule of a block number.
enum class Floor { solid, one_way, none };

Floor floor_rule(u8 block) {
    if (block >= 0xF2) {
        return Floor::solid;
    }
    if (block >= 0xDF) {
        return Floor::one_way;
    }
    if (block >= 0x88) {
        return Floor::none;
    }
    if (block >= 0x78) {
        return Floor::one_way;
    }
    if (block >= 0x70) {
        return Floor::none;
    }
    if (block >= 0x40) {
        return Floor::one_way;
    }
    return Floor::solid;
}
} // namespace

// 733E: the terrain at Sonic's position plus (dx, dy + 18): block address
// (D354), number (D353), profile type (D35E), slope (D100), the floor height
// at that X (D362), the wall position at that Y (D361) and the profile's
// last word (D35F). Outside the map everything reads as empty.
void Game::probe_terrain(unsigned dx, unsigned dy) {
    probe_cell(sonic(), dx, dy, true);
}

// 73F2: the same for any object, without the height tables; returns the
// profile type.
u8 Game::probe_type(const Object& o, unsigned dx, unsigned dy) {
    probe_cell(o, dx, dy, false);
    return probe.type;
}

void Game::probe_cell(const Object& o, unsigned dx, unsigned dy, bool heights) {
    const unsigned x = (o.x + dx) & 0xFFFF;
    probe.x = x;
    const unsigned column = (x >> 5) & 0xFF;
    unsigned y = (o.y + dy + 0x12) & 0xFFFF;
    if (y & 0x8000) {
        y = 0;
    }
    probe.y = y;
    const unsigned row = (y >> 5) & 0xFF;
    // The cell's index in the level map (the original's address C001 + ...).
    const unsigned cell = (rom.word(1, level.row_table + ((row * 2) & 0xFF)) + column + 1) & 0xFFFF;
    if (cell >= level.map.size()) {
        // 7475: outside the map
        probe.cell = 0x0FFF;
        probe.block = 0xFF;
        probe.type = 0;
        probe.wall = 0;
        probe.floor = 0;
        return;
    }
    probe.cell = std::uint16_t(cell);
    const u8 block = level.map[cell];
    probe.block = block;
    const unsigned profile = rom.word(0x1E, level.terrain_profiles + block * 2u);
    probe.type = rom.byte(0x1E, profile);
    if (!heights) {
        return;
    }
    probe.block_attribute = rom.byte(0x1E, profile + 1);
    probe.floor = rom.byte(0x1E, rom.word(0x1E, profile + 2) + (x & 0x1F));
    probe.wall = rom.byte(0x1E, rom.word(0x1E, profile + 4) + (y & 0x1F));
    probe.extra = rom.word(0x1E, profile + 6);
}

// 6AB6: floor, walls, ceiling, map items, then the contact bits.
void Game::collide_terrain() {
    collide_floor();     // 6B00
    collide_walls();     // 6FC6
    collide_ceiling();   // 717C
    collect_map_items(); // 723C
    combine_contacts();  // 67FC
}

// 67FC: the terrain bits, plus (unless Sonic is curled and D3B9 is clear)
// the high nibble of touch_sides: sides blocked by solid objects.
void Game::combine_contacts() {
    Object& o = sonic();
    u8 b = o.terrain;
    if (player.platform_slot || !(o.flags & 0x02)) {
        b |= u8(((o.touch_sides >> 4) | (o.touch_sides << 4)) & 0x0F);
    }
    o.contact = b;
}

// 6B00: the floor under Sonic's feet, then the behaviour of its profile
// type (table 6B34).
void Game::collide_floor() {
    Object& o = sonic();
    player.slope_previous = player.slope;
    player.slope = 0;
    probe_terrain(0, 0);
    player.floor_block = probe.block;
    player.floor_block2 = probe.block;
    land_on_floor(probe.block); // 6E46
    player.floor_type = probe.type;
    switch (probe.type & 0x7F) {
    case 0x05: // 6BA6: spikes (blocks F4/F5 are spike walls)
        if ((player.floor_block & 0xFE) == 0xF4) {
            return;
        }
        if (!(o.terrain & contact_bit::floor) || (o.flags & 0x80)) {
            return;
        }
        hurt_sonic(); // 37B5
        return;
    case 0x08: // 6C0D: a slide
        if (o.next_state != 0x1A) {
            to_slide(); // 316D
        }
        return;
    case 0x09: // 6B66: a launcher
        if (o.terrain & contact_bit::floor) {
            spring_up(0xF880); // 30A7
        }
        return;
    case 0x0C: // 6C16
        floor_cell_object();
        return;
    case 0x0D: // 6BBB: a block broken by landing curled
        if (!(o.flags & 0x02)) {
            return;
        }
        o.vy = 0xFBC0;
        o.terrain &= u8(~contact_bit::floor);
        o.flags |= 0x01;
        break_block(); // 70D9
        return;
    case 0x0E: // 6BD1
        conveyor();
        return;
    case 0x10: // 6CF4
        bounce_back();
        return;
    case 0x13: // 6D75
        player.floor_block2 = player.floor_block;
        booster(); // 6D7B
        return;
    case 0x14: // 6B71
        diagonal_launcher();
        return;
    case 0x00:
    case 0x06:
    case 0x07:
    case 0x0A:
        no_floor(); // 6C6E
        return;
    default: // 01-04, 0B, 0F, 11, 12, 15: nothing more
        return;
    }
}

// 6C6E: no floor under the feet. Rolling or launched off a slope's crest
// throws Sonic upward (6CB1); otherwise he falls (6C99).
void Game::no_floor() {
    Object& o = sonic();
    o.terrain &= u8(~contact_bit::floor);
    if ((o.state == 0x09 || o.state == 0x1B) && player.slope_previous) {
        const bool up_slope = player.slope_previous & 0x02;
        if (!(hi(o.vx) & 0x80) ? up_slope : !up_slope) {
            roll_off_slope(); // 6CB1
            return;
        }
    }
    leave_ground(); // 6C99
}

// 6C99: off the ground unless in contact with something solid.
void Game::leave_ground() {
    Object& o = sonic();
    combine_contacts();
    if (o.contact & contact_bit::floor) {
        return;
    }
    if (o.flags & 0x01) {
        return;
    }
    if (o.state != 0x09) {
        to_falling(); // 312B
    } else {
        roll_off(); // 3AE2
    }
}

// 6CB1: thrown up at 1.5 times the speed.
void Game::roll_off_slope() {
    Object& o = sonic();
    unsigned v = o.vx;
    if (v & 0x8000) {
        v = (0x10000 - v) & 0xFFFF;
    }
    const unsigned r = (v + (v >> 1) - 1) & 0xFFFF;
    o.vy = (~r) & 0xFFFF;
    sound_.request(sound::jump);
    to_launched(); // 3091
}

// 6E46: snapping to a floor in the block.
void Game::land_on_floor(u8 block) {
    Object& o = sonic();
    const Floor rule = floor_rule(block);
    if (rule == Floor::none) {
        return;
    }
    if (rule == Floor::solid) {
        // 6E5D
        if ((probe.floor & 0x3F) == 0x20) {
            floor_from_block_above(o); // 6ED4
        }
        const unsigned a = (probe.floor & 0x3F) + (lo(probe.y) & 0x1Fu);
        if (a < 0x20) {
            return;
        }
        o.y -= int(a - 0x20);
        o.terrain |= contact_bit::floor;
        combine_contacts();
        player.slope = probe.block_attribute;
        return;
    }
    // 6E92: one-way floors hold only while not rising, and only when Sonic
    // is less than (fall speed + 9) pixels into them.
    if (hi(o.vy) & 0x80) {
        return;
    }
    if (!(probe.floor & 0x3F)) {
        floor_from_block_above(o);
    }
    o.terrain &= u8(~contact_bit::floor);
    const unsigned limit = u8(hi(o.vy) + 9);
    const unsigned a = (probe.floor + (lo(probe.y) & 0x1Fu)) & 0xFF;
    if (a < 0x20) {
        return;
    }
    if (a - 0x20 >= limit) {
        return;
    }
    o.y -= int(a - 0x20);
    o.terrain |= contact_bit::floor;
    combine_contacts();
    player.slope = probe.block_attribute;
}

// 6ED4: the block D16A further in the map (the one above) decides: profile
// type 82 supplies the height (+20), a profile with bit 7 lifts Sonic by its
// height at once.
void Game::floor_from_block_above(Object& o) {
    // The offset is minus a map row. Above the map's first row the original
    // reads the cartridge below the map's address: bank 1E is mapped there
    // while terrain code runs.
    const int cell = int(probe.cell) + std::int16_t(level.attribute_table);
    const u8 block =
        cell >= 0 ? level.map.at(std::size_t(cell)) : rom.byte(0x1E, unsigned(0xC000 + cell));
    const unsigned profile = rom.word(0x1E, level.terrain_profiles + block * 2u);
    const u8 type = rom.byte(0x1E, profile);
    const unsigned floor = rom.word(0x1E, profile + 2);
    const u8 h = rom.byte(0x1E, floor + (lo(probe.x) & 0x1Fu)) & 0x3F;
    if (type == 0x82) {
        if (h) {
            probe.floor = u8(h + 0x20); // 6F33
        }
        return;
    }
    if (!(type & 0x80)) {
        return;
    }
    probe.block_attribute = rom.byte(0x1E, profile + 1);
    if (h) {
        o.y -= int(h);
    }
}

// 6FC6: the walls, 12 pixels up: left (7036), then right (10 pixels right).
void Game::collide_walls() {
    Object& o = sonic();
    o.terrain &= u8(~(contact_bit::right_wall | contact_bit::left_wall));
    collide_left_wall(); // 7036
    probe_terrain(0x000A, 0xFFF4);
    switch (probe.type) {
    case 0x81:
    case 0x0A:
        push_from_right_wall(); // 6FFD
        return;
    case 0x05: { // 712B: spikes
        if ((probe.block & 0xFE) != 0xF2) {
            if (o.next_state == 0x17) {
                break_wall(true); // 708D
                return;
            }
            if (o.next_state == 0x1E) {
                return;
            }
        }
        push_from_right_wall();
        combine_contacts();
        if (probe.block == 0xF4) {
            spike_wall(); // 716E
        }
        return;
    }
    case 0x8D:
        break_wall(true);
        return;
    case 0x13: // 6FF4
        player.floor_block2 = probe.block;
        booster();
        return;
    default:
        return;
    }
}

// 7036: the left wall (10 pixels left).
void Game::collide_left_wall() {
    Object& o = sonic();
    probe_terrain(0xFFF6, 0xFFF4);
    switch (probe.type) {
    case 0x81:
    case 0x0A:
        push_from_left_wall(); // 7059
        return;
    case 0x05: // 714E
        if ((probe.block & 0xFE) != 0xF2) {
            if (o.next_state == 0x17) {
                break_wall(false); // 70B3
                return;
            }
            if (o.next_state == 0x1E) {
                return;
            }
        }
        push_from_left_wall();
        combine_contacts();
        if (probe.block == 0xF5) {
            spike_wall();
        }
        return;
    case 0x8D:
        break_wall(false);
        return;
    case 0x13:
        player.floor_block2 = probe.block;
        booster();
        return;
    default:
        return;
    }
}

// 6FFD: out of a wall on the right. Positions with bit 6 (or exactly 20)
// face left.
void Game::push_from_right_wall() {
    Object& o = sonic();
    const unsigned c = lo(probe.x) & 0x1F;
    const u8 wall = probe.wall;
    const unsigned w = wall & 0x3F;
    if (!w || (w != 0x20 && !(wall & 0x40))) {
        return;
    }
    const unsigned a = w + c;
    if (a < 0x20) {
        return;
    }
    o.x -= int(a - 0x20);
    o.terrain |= contact_bit::right_wall;
    combine_contacts();
    if (probe.type == 0x0A) {
        launch_rolling(0xFA00); // 310C
    }
}

// 7059: out of a wall on the left (positions without bit 6).
void Game::push_from_left_wall() {
    Object& o = sonic();
    const unsigned c = lo(probe.x) & 0x1F;
    const u8 wall = probe.wall;
    const unsigned w = wall & 0x3F;
    if (!w || (w != 0x20 && (wall & 0x40))) {
        return;
    }
    if (w < c) {
        return;
    }
    o.x += int(w - c);
    o.terrain |= contact_bit::left_wall;
    combine_contacts();
    if (probe.type == 0x0A) {
        launch_rolling(0x0600); // 30ED
    }
}

// 708D / 70B3: a breakable wall gives way to Sonic curled at speed 3 or
// more (and he speeds up a little, up to 7); otherwise it is a wall.
void Game::break_wall(bool right) {
    Object& o = sonic();
    if (!(o.flags & 0x02)) {
        right ? push_from_right_wall() : push_from_left_wall();
        return;
    }
    u8 speed = hi(o.vx);
    if (speed & 0x80) {
        speed = u8(-speed);
    }
    if (speed < 3) {
        right ? push_from_right_wall() : push_from_left_wall();
        return;
    }
    if (right) {
        if (hi(o.vx) < 7) {
            o.vx += 0x40;
        }
    } else if (u8(-hi(o.vx)) < 7) {
        o.vx -= 0x40;
    }
    break_block(); // 70D9
}

// 70D9: the probed block becomes block FD (drawn at once) and breaks into
// four pieces (objects 04, or 07 for block F7).
void Game::break_block() {
    replace_block(0xFD);
    probe.debris_x = probe.x & 0xFFE0;
    probe.debris_y = probe.y & 0xFFE0;
    spawn_debris(); // 710C
}

// 710C (02E4): four pieces at D35A/D35C (objects 04, or 07 when the last
// probed block was F7).
void Game::spawn_debris() {
    const u8 type = probe.block == 0xF7 ? 0x07 : 0x04;
    for (u8 h = 0; h < 4; ++h) {
        spawn_object(type, h);
    }
}

// Changes the probed map cell to `block` (1A71; the original also redraws
// the block's 4x4 background entries, 1A7B).
void Game::replace_block(u8 block) {
    level.map.at(probe.cell) = block;
}

// 717C: the ceiling 24 pixels up, unless standing.
void Game::collide_ceiling() {
    Object& o = sonic();
    if (o.terrain & contact_bit::floor) {
        return;
    }
    o.terrain &= u8(~contact_bit::ceiling);
    probe_terrain(0, 0xFFE8);
    switch (probe.type) {
    case 0x81:
        push_from_ceiling(false); // 71AA
        return;
    case 0x8D:
        break_block(); // 70D9
        return;
    case 0x15: // 71DF: a launcher in the ceiling
        o.vy = 0x0780;
        sound_.request(sound::spring);
        to_launched(); // 3091
        return;
    case 0x13: // 7233
        player.floor_block2 = probe.block;
        booster();
        return;
    case 0x05:
        push_from_ceiling(true); // 71ED
        return;
    default:
        return;
    }
}

// 71AA / 71ED: pushed down out of a ceiling (heights without bit 6; for
// spikes, with it). Hitting spikes hurts.
void Game::push_from_ceiling(bool spikes) {
    Object& o = sonic();
    const unsigned c = lo(probe.y) & 0x1F;
    const u8 floor = probe.floor;
    const unsigned h = floor & 0x3F;
    if (!h) {
        return;
    }
    if (h != 0x20) {
        const bool bit6 = floor & 0x40;
        if (spikes ? !bit6 : bit6) {
            return;
        }
    }
    if (h < c) {
        return;
    }
    o.y += int(h - c);
    o.terrain |= contact_bit::ceiling;
    combine_contacts();
    if (!spikes) {
        o.vy = 0x0100;
        return;
    }
    if (o.state == 0x1E) {
        return;
    }
    if ((probe.block & 0xFE) == 0xF4) {
        o.vy = 0x0100; // 71D8
        return;
    }
    if (o.flags & 0x80) {
        return;
    }
    hurt_sonic();
}

// 716E: spikes in a wall.
void Game::spike_wall() {
    Object& o = sonic();
    if (o.flags & 0x80) {
        return;
    }
    o.vy = 0x0100;
    hurt_sonic();
}

// 6BD1: a conveyor moves Sonic one pixel per frame (left on the blocks
// listed at 6C05).
void Game::conveyor() {
    Object& o = sonic();
    if (!(o.terrain & contact_bit::floor)) {
        return;
    }
    bool left = false;
    for (unsigned i = 0; i < 8; ++i) {
        if (rom.byte(rom::conveyor_blocks, i) == player.floor_block) {
            left = true;
            break;
        }
    }
    add_to_position(o.x_sub, o.x, left ? 0xFF00 : 0x0100);
}

// 6CF4: block 2D sends Sonic back up the way he came.
void Game::bounce_back() {
    Object& o = sonic();
    if (probe.floor & 0x80) {
        return;
    }
    if (!(o.terrain & contact_bit::floor)) {
        return;
    }
    if (player.floor_block != 0x2D) {
        return;
    }
    if (hi(o.vx) & 0x80) {
        return;
    }
    // Each byte is negated on its own (NEG; NEG), not the 16-bit value.
    const std::uint16_t v = std::uint16_t((u8(-hi(o.vx)) << 8) | u8(-lo(o.vx)));
    o.vx = v;
    o.vy = v;
}

// 6D7B: boosters (blocks 3B-3E) shoot Sonic in a fixed direction at 6 pixels
// per frame (state 21), unless already moving that way.
void Game::booster() {
    Object& o = sonic();
    o.terrain &= u8(~contact_bit::floor);
    o.flags |= 0x01;
    unsigned vx = 0, vy = 0;
    switch (probe.block) {
    case 0x3B: // 6DC7: down
        sound_.request(sound::roll);
        if (o.state == 0x21 || (hi(o.vy) & 0x80)) {
            return;
        }
        vy = 0x0600;
        break;
    case 0x3C: // 6D9A: up
        sound_.request(sound::roll);
        if (o.state == 0x21 || !(hi(o.vy) & 0x80)) {
            return;
        }
        vy = 0xFA00;
        break;
    case 0x3D: // 6DF2: right
        sound_.request(sound::roll);
        if (o.state == 0x21 || (hi(o.vx) & 0x80)) {
            return;
        }
        vx = 0x0600;
        break;
    case 0x3E: // 6E1D: left
        sound_.request(sound::roll);
        if (o.state == 0x21 || !(hi(o.vx) & 0x80)) {
            return;
        }
        vx = 0xFA00;
        break;
    default:
        return;
    }
    o.next_state = 0x21;
    o.vx = vx;
    o.vy = vy;
}

// 6B71: a launcher throwing Sonic diagonally up (left on blocks 80 and up).
void Game::diagonal_launcher() {
    Object& o = sonic();
    if (!(o.terrain & contact_bit::floor)) {
        return;
    }
    unsigned vx = 0xF880;
    o.display |= display_flag::mirrored;
    if (player.floor_block < 0x80) {
        vx = 0x0780;
        o.display &= u8(~display_flag::mirrored);
    }
    o.vx = vx;
    sound_.request(sound::spring);
    bounce_up(flow.zone == 3 ? 0xFAF0 : 0xF900); // 30CC
}

// 6C16: a cell that turns into object 2D when Sonic stands exactly on its
// top (height 20): block EF becomes ED or EE (by X bit 4), others FF.
void Game::floor_cell_object() {
    Object& o = sonic();
    if (probe.floor != 0x20) {
        return;
    }
    u8 replacement = 0xFF;
    if (player.floor_block == 0xEF) {
        replacement = (lo(o.x) & 0x10) ? 0xEE : 0xED;
    }
    o.terrain &= u8(~contact_bit::floor);
    Object* n = find_free_slot(); // 6025
    if (!n) {
        return;
    }
    sound_.request(sound::floor_breaks);
    n->type = object_type::falling_block;
    replace_block(replacement);
    leave_ground(); // 6C99
}

// 723C: rings built into the map (profile type 7, blocks 70-7F): the
// quarter of the block Sonic's head is in (table 72CB) is collected, the
// block replaced (table +20), and a sparkle spawned. In zone 2, skimming the
// water surface fast enough bounces (730B).
void Game::collect_map_items() {
    Object& o = sonic();
    const unsigned dy = (o.frame_ticks & 1) ? 0xFFF0 : 0xFFE6;
    water.type_previous = water.type;
    const u8 type = probe_type(o, 0, dy); // 73F2
    water.type = type;
    if (type != 0x07) {
        skim_water(); // 730B
        return;
    }
    const u8 index = u8(probe.block - 0x70);
    const unsigned c = ((lo(probe.x) >> 4) | (lo(probe.x) << 4)) & 0x01;
    const unsigned e = ((lo(probe.y) >> 3) | (lo(probe.y) << 5)) & 0x02;
    const auto entry = rom::map_ring_quarters + ((index * 4u) & 0xFFFF) + c + e;
    if (!rom.byte(entry)) {
        return;
    }
    replace_block(rom.byte(entry, 0x20));
    probe.debris_x = probe.x;
    probe.debris_y = probe.y;
    spawn_object(object_type::ring_sparkle, 0);
    sound_.request(sound::ring);
    add_ring(); // 265A
}

// 730B: in zone 2, rolling fast across a water surface (blocks 8E/8F)
// bounces Sonic up.
void Game::skim_water() {
    Object& o = sonic();
    if (flow.zone != 2) {
        return;
    }
    if ((probe.block | 1) != 0x8F) {
        return;
    }
    if (water.type_previous == 0x06) {
        return;
    }
    if (!(o.flags & 0x02)) {
        return;
    }
    u8 speed = hi(o.vx);
    if (speed & 0x80) {
        speed = u8(-speed);
    }
    if (speed < 3) {
        return;
    }
    o.vy = 0xFD00;
}

// 7490 (vector 0260): an object's own floor check at its position
// (6F51), and profile type 0 sets its flag bit 4 unless airborne.
//
// Quirk: 6F51 chooses its floor rule from register A, which here holds the
// bank number restored by 733E rather than a block number. Objects run in
// banks 1C-1F, so they always use the solid rule (checked over the whole
// comparison suite).
void Game::object_floor(Object& o) {
    o.terrain &= u8(~contact_bit::floor);
    probe_cell(o, 0, 0, true);
    object_land(o, 0x1C); // 6F51 with A = the object's bank (1C-1F): solid
    if ((probe.type & 0x7F) == 0) {
        if (!(o.flags & 0x01)) {
            o.flags |= 0x10;
        }
    }
}

// 6F51: an object snapping to solid floor (rule value below 40; one-way for
// 40-87 only while falling, 6F8B).
void Game::object_land(Object& o, u8 rule) {
    if (rule >= 0x88) {
        return;
    }
    if (rule < 0x40) {
        if ((probe.floor & 0x3F) == 0x20) {
            floor_from_block_above(o); // 6ED4
        }
        const unsigned a = (probe.floor & 0x3F) + (lo(probe.y) & 0x1Fu);
        if (a < 0x20) {
            return;
        }
        o.y -= int(a - 0x20);
        o.terrain |= contact_bit::floor;
        return;
    }
    if (hi(o.vy) & 0x80) {
        return;
    }
    if (!(probe.floor & 0x3F)) {
        floor_from_block_above(o);
    }
    const unsigned limit = u8(hi(o.vy) + 7);
    const unsigned a = (probe.floor + (lo(probe.y) & 0x1Fu)) & 0xFF;
    if (a < 0x20 || a - 0x20 >= limit) {
        return;
    }
    o.y -= int(a - 0x20);
    o.terrain |= contact_bit::floor;
}

} // namespace sonic2_gg
