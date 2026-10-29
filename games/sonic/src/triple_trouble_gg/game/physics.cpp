// The player against the terrain (0393 -> 5F14, bank 1): the floor and the
// block kinds' effects (5F53), the walls (6780), the ceiling (6A92), the
// sides in the air (6CF2) and the head (6E16).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// -v as the code computes it (DEC HL; CPL; CPL): the magnitude of a
// negative speed.
std::uint16_t negate(std::uint16_t v) {
    return std::uint16_t(-v);
}

std::uint16_t magnitude(std::uint16_t v) {
    return (v & 0x8000) ? negate(v) : v;
}
} // namespace

// 5F14
void Game::player_terrain(Object& o) {
    player_floor(o);                                     // 5F53
    player_walls(o);                                     // 6780
    player_ceiling(o);                                   // 6A92
    player_sides(o);                                     // 6CF2
    player_head(o);                                      // 6E16
    if (flow.zone == 3 && terrain_state.block == 0xEA) { // 6516
        o.next_state = 0x42;
    }
}

// 5F27 (0486): the same without the sides and the zone 3 blast block.
void Game::player_terrain_plain(Object& o) {
    player_floor(o);   // 5F53
    player_walls(o);   // 6780
    player_ceiling(o); // 6A92
    player_head(o);    // 6E16
}

// 5F53: the block under the player: landing on its floor (65AD, with the
// previous frame's block flags) and the block kind's effect (table 5FF2).
// Below the screen (with the camera following, not scripted) there is no
// floor.
void Game::player_floor(Object& o) {
    if (terrain_state.timer) {
        --terrain_state.timer; // D429
    }
    terrain_state.landed_previous = probe.landed_attribute;
    probe.landed_attribute = 0;
    o.status &= u8(~0x20);
    terrain_state.block_previous = terrain_state.block;
    terrain_state.flags_previous = terrain_state.flags;
    if ((camera.map_flags & map_flag::enabled) && (hi(o.screen_y) || lo(o.screen_y) >= 0xA9) &&
        !(camera.mode & camera_mode::scripted)) {
        probe.flags = 0;
        probe.block = 0;
    } else {
        unsigned dy = 0;
        if (o.state == 0x21) {
            dy = 0xFFF2;
        } else if (o.state == 0x12) {
            dy = 0x0008;
        }
        probe_terrain(o, 0, dy); // 6FA6 (5F34 then has no effect)
    }
    terrain_state.block = probe.block;
    if (!o.floor_off) {
        floor_landing(o); // 65AD
    }
    terrain_state.flags = probe.flags;
    block_kind_effect(o, probe.flags & 0x1F); // table 5FF2
}

// 65AD: the floor (flags bit 7) or a top surface (bit 6) of the block the
// flags describe, at the probe's point.
void Game::floor_landing(Object& o) {
    if (o.floor_off) {
        return;
    }
    const u8 flags = terrain_state.flags;
    if (flags & 0x80) {
        land_on_profile(o, true); // 65BE
        return;
    }
    if (!(flags & 0x40)) {
        return;
    }
    // 6628
    const u8 kind = flags & 0x1F;
    if (kind == 0x11 || kind == 0x03) {
        // 6602: steeper than 45 degrees in motion: as a floor even rising.
        land_on_profile(o, magnitude(o.vy) <= magnitude(o.vx) ? false : true);
        return;
    }
    if (o.vy & 0x8000) {
        return;
    }
    if (!(probe.floor & 0x3F)) {
        check_block_above(o); // 6671
    }
    o.terrain &= u8(~0x02);
    const u8 reach = u8(hi(o.vy) + 9);
    const u8 depth = u8((probe.y & 0x1F) + probe.floor);
    if (depth < 0x20) {
        return;
    }
    const u8 into = u8(depth - 0x20);
    if (into >= reach) {
        return;
    }
    o.y = std::uint16_t(o.y - into);
    probe.landed_attribute = probe.attribute;
    landed(o); // 65F3
}

// 65BE (with `check_rising`) / 65C3: on the floor profile: pushed up to it
// and landed.
void Game::land_on_profile(Object& o, bool check_rising) {
    if (check_rising && (o.vy & 0x8000)) {
        return;
    }
    if (probe.floor & 0x40) {
        return;
    }
    if ((probe.floor & 0x3F) == 0x20) {
        check_block_above(o); // 6671
    }
    probe.landed_attribute = probe.attribute;
    const u8 depth = u8((probe.y & 0x1F) + (probe.floor & 0x3F));
    if (depth < 0x20) {
        return;
    }
    o.y = std::uint16_t(o.y - u8(depth - 0x20));
    landed(o); // 65F3
}

// 6258: a block the player sinks into (hypothesis: deep snow / mud), two
// lines every other frame, standing (status bit 5) with no fall speed;
// entering a new one in zone 2 makes a splash (type 2F). Sonic's board
// rides its top (profile 1F); rising, the player is free of it.
void Game::sinking_block(Object& o) {
    if (terrain_state.block != terrain_state.block_previous && flow.zone == 2) {
        spawn_object_at(o, 0x2F, 0x00, o.x, o.y); // 510A
    }
    if (o.state == 0x23) {
        o.vy = 0;
        o.terrain |= 0x02;
        probe.floor = 0x1F;
        land_on_profile(o, true); // 65BE
    } else if (!(o.vy & 0x8000)) {
        o.status |= 0x20;
        o.terrain |= 0x02;
        o.vy = 0;
        const u8 s = o.state;
        if (s == 0x0F || s == 0x1A || s == 0x15 || s == 0x23) {
            return;
        }
        if (++o.scratch[5] & 1) {
            o.y = std::uint16_t(o.y + 2);
        }
        return;
    }
    o.status &= u8(~0x20); // 62C5
    o.flags |= 0x02;
}

// 639E: kind 0C: a block that drops: landing (not rising) on a new one
// spawns its falling piece (type 9) at the probed point, which knows its
// cell (bytes 38-39 its address); the cell is noted (DBC1, D403).
void Game::dropping_block(Object& o) {
    if (o.vy & 0x8000) {
        return;
    }
    if (probe.cell == probe.drop_cell) {
        return;
    }
    Object* piece = spawn_object_at(o, 0x09, 0x00, probe.x, probe.y); // 510A
    if (!piece) {
        return;
    }
    probe.drop_cell = probe.cell;
    probe.switch_cell = probe.cell;
    const std::uint16_t address = std::uint16_t(0xC000 + probe.cell);
    piece->scratch[4] = lo(address);
    piece->scratch[5] = hi(address);
}

// Table 5FF2: what the block kind under the player does.
void Game::block_kind_effect(Object& o, u8 kind) {
    switch (kind) {
    case 0x00:
    case 0x07:
        in_the_air(o); // 6487
        return;
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x08:
    case 0x11:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x1A:
    case 0x1C:
    case 0x1D:
    case 0x1E:
        return; // 6040
    case 0x04:
        block_kind_10(o); // 6041 -> 6B3C
        return;
    case 0x12:
        conveyor_slope(o); // 6075
        return;
    case 0x18:
        if (o.state != 0x32 && !(o.vy & 0x8000) && (o.terrain & 0x02)) { // 6054
            const u8 x = o.x & 0x1F;
            if (x >= 0x08 && x < 0x18) {
                o.next_state = 0x32;
            }
        }
        return;
    case 0x05: // 6207: spikes: standing on them hurts (as when crushed)
        if (!(o.terrain & 0x02) || (o.flags & 0x80)) {
            return;
        }
        player.touch_kind = 0x50;
        hurt_player_now(o); // 384A
        return;
    case 0x06:
        spring_block(o); // 642A
        return;
    case 0x0A: // 6030: (not in states 29, 48) status bit 0, and states 23/26 end (5854)
        if (o.state == 0x29 || o.state == 0x48) {
            return;
        }
        o.status |= 0x01;
        leave_special_state(o); // 5854
        return;
    case 0x0B:
        step_on_switch(o); // 63CE
        return;
    case 0x0D:
        breakable_floor(o); // 6330
        return;
    case 0x0E: // 636A: a conveyor: on it the player moves a pixel per frame (right on A2)
        if (!(o.terrain & 0x02) || o.state == 0x29) {
            return;
        }
        convey(o, terrain_state.block == 0xA2 ? 0x0100 : 0xFF00);
        return;
    case 0x09:
        spring_floor(o); // 61B4
        return;
    case 0x0F: // 6044: a bumper underfoot
        bumper(o, std::uint16_t(-o.vx), 0xFB00);
        return;
    case 0x10:
        path_entry_block(o); // 64BA
        return;
    case 0x17:
        launch_ramp(o); // 6174
        return;
    case 0x19:
        crumble_block(o); // 62CE
        return;
    case 0x16:
        ring_block(o); // 6219
        return;
    case 0x0C:
        dropping_block(o); // 639E
        return;
    case 0x1B:
        sinking_block(o); // 6258
        return;
    case 0x1F:
        throw Unrecovered("block kind 1F (table 5FF2 runs past its end)");
    default:
        throw Unrecovered("block kind " + std::to_string(kind) + " (table 5FF2)");
    }
}

// 642A: kind 6 in zones 0 and 0A, a spring in the map: stepped on within
// its middle (X 4-1B in the block), not in states 11, 43, 0B: its
// animation (type 21; parameter FF for block D5, else FE) and the player
// sprung up (D5: at 3.5 pixels per frame, others 7 with D3F0 set).
void Game::spring_block(Object& o) {
    if (flow.zone != 0x0A && flow.zone != 0x00) {
        return;
    }
    const u8 s = o.state;
    if (s == 0x11 || s == 0x43 || s == 0x0B) {
        return;
    }
    const u8 x = o.x & 0x1F;
    if (x < 0x04 || x >= 0x1C) {
        return;
    }
    const bool weak = terrain_state.block == 0xD5;
    spawn_object_at(o, 0x21, weak ? 0xFF : 0xFE, probe.x, probe.y); // 510A
    player.spring_flag = weak ? 0x00 : 0xFF;
    spring_player(o, weak ? 0xFC80 : 0xF900); // 5186
}

// 61B4: kind 9, a springy floor: standing on it (not rising; not in
// states 29, 11, 43) the player is thrown up at 7 pixels per frame (5 in
// zone 4's water) with D3F0 set.
void Game::spring_floor(Object& o) {
    const u8 s = o.state;
    if (s == 0x29 || s == 0x11 || s == 0x43) {
        return;
    }
    if (!(o.terrain & 0x02) || (o.vy & 0x8000)) {
        return;
    }
    o.status |= 0x01;
    leave_special_state(o); // 5854
    player.spring_flag = 0xFF;
    const std::uint16_t vy = (flow.zone == 4 && (o.flags & 0x04)) ? 0xFB00 : 0xF900;
    launch_player(o, vy); // 3DB3
}

// 72B2: a bumper block throws the player at (vx, vy) (sound CA); outside
// zone 6 it lights up (type 39, parameter 1, at the block, which keeps the
// cell's address); otherwise, or without a free slot, the block is gone
// (block 0) and the player may fall (64A5).
void Game::bumper(Object& o, std::uint16_t vx, std::uint16_t vy) {
    o.vx = vx;
    o.vy = vy;
    sound_.request(0xCA, 1);
    if (flow.zone != 6) {
        const std::uint16_t x = std::uint16_t((probe.x & 0xFF00) | u8((probe.x & 0xE0) + 0x10));
        const std::uint16_t y = std::uint16_t((probe.y & 0xFF00) | (probe.y & 0xE0));
        if (Object* light = spawn_object_at(o, 0x39, 0x01, x, y)) { // 510A
            const std::uint16_t address = std::uint16_t(0xC000 + probe.cell);
            light->scratch[4] = lo(address);
            light->scratch[5] = hi(address);
            return;
        }
    }
    set_map_block(probe.cell, 0x00); // 6528
    // 64A5
    if ((o.contact & 0x02) || (o.flags & 0x01)) {
        return;
    }
    if (o.state == 0x09) {
        to_rolling_fall(o); // 3BB8
    } else {
        to_falling(o); // 3B47
    }
}

// 62CE: kind 19, a block that crumbles under the player: an explosion
// chain (type 07, parameter 4; with no free slot nothing happens) that
// will put the block back; meanwhile it is block 91; sound C5.
void Game::crumble_block(Object& o) {
    Object* e = spawn_object_at(o, 0x07, 0x04, probe.x, probe.y); // 510A
    if (!e) {
        return;
    }
    const std::uint16_t address =
        std::uint16_t(0xC000 + probe.cell); // the explosion keeps the cell's address
    e->scratch[4] = lo(address);
    e->scratch[5] = hi(address);
    set_map_block(probe.cell, 0x91); // with its picture (2CEB)
    probe.ring_x = std::uint16_t((probe.x & 0xFF00) | (probe.x & 0xE0));
    probe.ring_y = std::uint16_t((probe.y & 0xFF00) | (probe.y & 0xE0));
    sound_.request(0xC5, 1);
}

// 6174: kind 17, a ramp's lip: on block 44 running right at 4.5 or more
// (else running left at 4.5 or more), in the block's matching half, the
// player is thrown up at 5 rolling (3D99; not in states 16, 23).
void Game::launch_ramp(Object& o) {
    const std::uint16_t v = o.vx;
    if (terrain_state.block == 0x44) {
        if ((v & 0x8000) || v < 0x0480 || !(o.x & 0x10)) {
            return;
        }
    } else {
        if (!(v & 0x8000) || v >= 0xFB80 || (o.x & 0x10)) {
            return;
        }
    }
    o.vy = 0xFB00;
    if (o.state == 0x16 || o.state == 0x23) {
        return;
    }
    to_bounced_roll(o); // 3D99
}

// 64BA: kind 10, the entries of paths (tubes; blocks 6C on, table 64CD):
// on blocks 74 and 75 a player on path 1 who stood on block 76 the frame
// before enters the path leftward (64EE); on 76 and 77 one not on path 1
// coming from block 75 enters it rightward (6502).
void Game::path_entry_block(Object& o) {
    const u8 block = terrain_state.block;
    if (block < 0x6C) {
        return;
    }
    const unsigned handler = rom.word(Table{0x01, 0x64CD}, (block - 0x6Cu) * 2);
    switch (handler) {
    case 0x64ED:
        return;
    case 0x64EE:
        if (o.path != 0 && (o.terrain & 0x02) && terrain_state.block_previous == 0x76) {
            enter_path(o, true); // 378C
        }
        return;
    case 0x6502:
        if (o.path != 1 && (o.terrain & 0x02) && terrain_state.block_previous == 0x75) {
            enter_path(o, false); // 373D
        }
        return;
    default:
        throw Unrecovered("path entry block past table 64CD");
    }
}

// 373D / 378C: entering a path (not with power-up 9, nor in states 16,
// 23): facing right, state 0C (13 in zone 2), or facing left 32 pixels on,
// state 0D. The position snaps to the 32-pixel grid (Y + 4, low bytes
// only) and becomes the path's origin; the position along it restarts.
void Game::enter_path(Object& o, bool leftward) {
    if (o.power_up == 0x09 || o.state == 0x16 || o.state == 0x23) {
        return;
    }
    if (leftward) {
        o.display |= display_flag::mirrored;
        o.x = std::uint16_t(o.x + 0x20);
        o.next_state = 0x0D;
    } else {
        o.display &= u8(~display_flag::mirrored);
        o.next_state = flow.zone == 2 ? 0x13 : 0x0C;
    }
    o.status &= u8(~0x01);
    o.x = std::uint16_t((o.x & 0xFF00) | (o.x & 0xE0));
    o.origin_x = o.x;
    o.y = std::uint16_t((o.y & 0xFF00) | u8((o.y & 0xE0) + 4));
    o.origin_y = o.y;
    player.path = {};
}

// 6910 / 6937: a wall of kind 0D breaks when the player rolls into it at
// 3 pixels per frame or more (speeding him up by 1/4 pixel up to 7):
// 7228. Returns false where it is an ordinary wall.
bool Game::break_wall(Object& o, bool right) {
    if (!(o.flags & 0x02)) {
        return false;
    }
    if (high_speed(o.vx) < 3) {
        return false;
    }
    if (right) {
        if (hi(o.vx) < 7) {
            o.vx = std::uint16_t(o.vx + 0x40);
        }
    } else if (u8(-hi(o.vx)) < 7) {
        o.vx = std::uint16_t(o.vx - 0x40);
    }
    shatter_block(o);
    return true;
}

// 7228: the probed block becomes block 8F; its corner is noted (D409,
// D40B) for the pieces (type 0A, or 2E for kind 5: parameters 1-8), sound
// B2; the breaking object is noted (DBC4).
void Game::shatter_block(Object& o) {
    set_map_block(probe.cell, 0x8F); // with its picture (2CEB)
    probe.ring_x = std::uint16_t((probe.x & 0xFF00) | (probe.x & 0xE0));
    probe.ring_y = std::uint16_t((probe.y & 0xFF00) | (probe.y & 0xE0));
    sound_.request(0xB2, 1);
    const u8 type = (probe.flags & 0x1F) == 0x05 ? 0x2E : 0x0A;
    act.breaker = u8(slot_number(o) + 1);
    for (u8 piece = 1; piece <= 8; ++piece) {
        spawn_object(o, type, piece); // 50E1
    }
}

// 3DFF / 3E0C: a wall's spring: the player rolls away at `vx` (that
// speed his top speed), sound B5; no longer against that wall or on the
// floor; status bit 0 and (unless in states 16, 23) a 36-frame hold on the
// controls (status bit 1) and rolling (state 9).
void Game::wall_spring(Object& o, std::uint16_t vx) {
    auto& p = objects[0];
    const u8 side = (vx & 0x8000) ? 0x04 : 0x08;
    p.contact &= u8(~side);
    p.terrain &= u8(~side);
    p.terrain &= u8(~0x02);
    p.vx = vx;
    player.top_speed = (vx & 0x8000) ? std::uint16_t(-vx) : vx;
    sound_.request(0xB5, 1);
    p.flags = u8((p.flags | 0x02) & ~0x01);
    p.status |= 0x01;
    if (p.state == 0x16 || p.state == 0x23) {
        return;
    }
    p.status |= 0x02;
    p.status_ticks = 0x24;
    p.next_state = 0x09;
    (void)o;
}

// 63CE: kind 0B, a switch (block 93) the player stands on (not in state
// 29, not again for D429's frames): pressed (block 94, the switch object
// type 3F to raise it again; sound C5), and its effect (18:B858).
void Game::step_on_switch(Object& o) {
    if (terrain_state.timer || o.state == 0x29) {
        return;
    }
    if (terrain_state.block != 0x93 || !(o.terrain & 0x02)) {
        return;
    }
    Object* sw = spawn_object_at(o, 0x3F, 0x00, probe.x, probe.y); // 510A
    if (!sw) {
        return;
    }
    probe.block_x = probe.x;
    probe.block_y = probe.y;
    set_map_block_at(probe.cell, 0x94, probe.block_x, probe.block_y); // 6580 (0348)
    sound_.request(0xC5, 1);
    probe.switch_cell = probe.cell;
    const std::uint16_t address = std::uint16_t(0xC000 + probe.cell);
    sw->scratch[4] = lo(address);
    sw->scratch[5] = hi(address);
    switch_effect(o); // 18:B858
}

// 18:B858: in zones 4 and 5 the act's switch list (tables B8A5 and B8AB by
// act: cell address and handler) names what a switch does.
void Game::switch_effect(Object& o) {
    if (flow.zone < 4 || flow.zone >= 6) {
        return;
    }
    const Table acts{0x18, std::uint16_t(flow.zone == 4 ? 0xB8A5 : 0xB8AB)};
    Table entry{0x18, rom.word(acts, flow.act * 2u)};
    const std::uint16_t address = std::uint16_t(0xC000 + probe.cell);
    for (;; entry = entry + 4) {
        if (rom.byte(entry, 0) == 0xFF) {
            return;
        }
        if (rom.word(entry, 0) == address) {
            break;
        }
    }
    std::uint16_t handler = rom.word(entry, 2);
    if (handler == 0xB907) {
        // A type 3B (parameter 1) 16 pixels below (not in states 26, 29).
        if (o.state == 0x26 || o.state == 0x29) {
            return;
        }
        spawn_object_at(o, 0x3B, 0x01, probe.x, std::uint16_t(probe.y + 0x10));
        return;
    }

    // The other handlers (18:B969-BE71): the switches rest for a while
    // (D429), and objects appear at offsets from the switch's 32-pixel
    // column and its Y: type 45 (a moving block; FF/FE its direction) or
    // 47. These are the handlers' own operands.
    struct Spawn {
        u8 type, param;
        int dx, dy;
    };

    struct Handler {
        std::uint16_t address;
        u8 rest;
        std::array<Spawn, 6> spawns;
        std::uint16_t then; // runs on into this handler
    };

    static constexpr Handler handlers[] = {
        {0xB969, 0x00, {{{0x47, 0x00, 48, -128}}}, 0},
        {0xB985, 0x20, {{{0x45, 0xFF, 48, 64}, {0x45, 0xFF, 80, 96}}}, 0},
        {0xB9C1,
         0x80,
         {{{0x45, 0xFF, 48, 0},
           {0x45, 0xFF, 80, 0},
           {0x45, 0xFF, 112, 0},
           {0x45, 0xFF, -16, 0},
           {0x45, 0xFF, -48, 0},
           {0x45, 0xFF, -80, 0}}},
         0},
        {0xBA69, 0x20, {{{0x45, 0xFF, 48, 32}, {0x45, 0xFF, 80, 64}}}, 0},
        {0xBAA5, 0x20, {{{0x45, 0xFF, 48, -64}}}, 0},
        {0xBAC6, 0x20, {{{0x45, 0xFF, 48, 0}}}, 0},
        {0xBAE7, 0x20, {{{0x45, 0xFF, 48, -32}}}, 0},
        {0xBB08, 0x20, {{{0x45, 0xFE, -16, -64}, {0x45, 0xFE, -48, -32}, {0x45, 0xFE, -80, 0}}}, 0},
        {0xBB5F, 0x20, {{{0x45, 0xFE, -16, -64}, {0x45, 0xFE, -48, -32}, {0x45, 0xFE, -80, 0}}}, 0},
        {0xBBF4, 0x20, {{{0x45, 0xFF, -48, 0}}}, 0},
        {0xBC15, 0x20, {{{0x45, 0xFF, 48, -32}, {0x45, 0xFF, 80, -64}}}, 0},
        {0xBC51, 0x20, {{{0x45, 0xFE, -16, -64}, {0x45, 0xFE, 48, -64}}}, 0},
        {0xBC8D,
         0x20,
         {{{0x45, 0xFE, 80, -96}, {0x45, 0xFE, 112, -96}, {0x45, 0xFE, 144, -96}}},
         0},
        {0xBCE4, 0x00, {{{0x45, 0xFF, 112, -96}}}, 0xBCFF},
        {0xBCFF, 0x20, {{{0x45, 0xFF, 48, -32}, {0x45, 0xFF, 80, -64}}}, 0},
        {0xBD3B, 0x20, {{{0x45, 0xFE, 48, -64}, {0x45, 0xFE, -16, -96}}}, 0},
        {0xBD77, 0x00, {{{0x47, 0x00, 48, -96}}}, 0},
        {0xBD93, 0x00, {{{0x47, 0x00, 48, -64}}}, 0},
        {0xBDAF, 0x00, {{{0x47, 0x00, -16, -64}}}, 0},
        {0xBDD9, 0x20, {{{0x45, 0xFE, 48, -64}, {0x45, 0xFE, 80, -64}, {0x45, 0xFE, 112, -64}}}, 0},
        {0xBE30, 0x20, {{{0x45, 0xFF, 48, 32}}}, 0},
        {0xBE51, 0x20, {{{0x45, 0xFF, 48, -32}}}, 0},
    };
    const std::uint16_t column = std::uint16_t((probe.x & 0xFF00) | (probe.x & 0xE0));
    while (handler) {
        const Handler* h = nullptr;
        for (const auto& k : handlers) {
            if (k.address == handler) {
                h = &k;
            }
        }
        if (!h) {
            throw Unrecovered("switch handler " + std::to_string(handler) + " (18:B858)");
        }
        if (h->rest) {
            terrain_state.timer = h->rest;
        }
        for (const auto& sp : h->spawns) {
            if (!sp.type) {
                break;
            }
            spawn_object_at(o, sp.type, sp.param, std::uint16_t(column + sp.dx),
                            std::uint16_t(probe.y + sp.dy)); // 510A
        }
        handler = h->then;
    }
}

// 6330: kind 0D, a breakable floor (not in states 41, 0F, 10, 15, 1A):
// rolling onto it bounces the player up (FB.C0, FF.00 in water, as a jump
// held for 16 frames) and breaks it (7228); in state 29 it just breaks.
void Game::breakable_floor(Object& o) {
    const u8 s = o.state;
    if (s == 0x41 || s == 0x0F || s == 0x10 || s == 0x15 || s == 0x1A) {
        return;
    }
    if (s != 0x29) {
        if (!(o.flags & 0x02)) {
            return;
        }
        player.jump_ticks = 0x10;
        o.vy = (o.flags & 0x04) ? 0xFF00 : 0xFBC0;
        o.terrain &= u8(~0x02);
        o.flags |= 0x01;
    }
    shatter_block(o); // 7228
}

// 6384: the position moved by a 1/256-pixel amount (X fraction, X).
void Game::convey(Object& o, std::uint16_t amount) {
    const unsigned low = unsigned(o.x_sub | (lo(o.x) << 8)) + amount;
    o.x_sub = u8(low);
    const u8 carry = u8(low >> 16);
    const u8 high = u8(hi(o.x) + carry + ((amount & 0x8000) ? 0xFF : 0x00));
    o.x = std::uint16_t((high << 8) | u8(low >> 8));
}

// 6219: kind 16, a block holding rings: a rolling or jumping player (not
// charging or dashing) breaks it, pushed into it sideways or not rising:
// bounced up at FB.C0 (FF.00 under water), sound B9, the block emptied
// (71D8).
void Game::ring_block(Object& o) {
    if (!(o.flags & 0x02)) {
        return;
    }
    const u8 s = o.state;
    if (s == 0x0F || s == 0x10 || s == 0x15 || s == 0x1A) {
        return;
    }
    if (!(o.terrain & 0x0C) && (o.vy & 0x8000)) {
        return;
    }
    o.vy = (o.flags & 0x04) ? 0xFF00 : 0xFBC0;
    o.terrain &= u8(~0x02);
    o.flags |= 0x01;
    sound_.request(0xB9, 1);
    break_ring_block(o);
}

// 71D8: the block becomes block 6A, an explosion (type 07, parameter 3)
// at its centre, ten rings.
void Game::break_ring_block(Object& o) {
    set_map_block(probe.cell, 0x6A); // with its picture (2CEB)
    const std::uint16_t x = std::uint16_t((probe.x & 0xFFE0) + 0x10);
    const std::uint16_t y = std::uint16_t((probe.y & 0xFFE0) + 0x08);
    spawn_object_at(o, 0x07, 0x03, x, y); // 510A
    add_ten_rings();                      // 3396
}

// 6487: no floor: unless held (states 18, 11, 23, 43, a flag), the player
// starts falling (3B47; from state 9, 3BB8).
void Game::in_the_air(Object& o) {
    if (o.touch_sides & 0x20) {
        return;
    }
    if (o.state == 0x31) {
        return;
    }
    o.terrain &= u8(~0x02);
    const u8 s = o.state;
    if (s == 0x18 || s == 0x11 || s == 0x23 || s == 0x43) {
        return;
    }
    if (o.contact & 0x02) {
        return;
    }
    if (o.flags & 0x01) {
        return;
    }
    if (o.state != 0x09) {
        to_falling(o); // 3B47
    } else {
        to_rolling_fall(o); // 3BB8
    }
}

// 6075: kind 12 (a conveyor-like slope), not in states 13, 0D, 0C, 11, 43.
void Game::conveyor_slope(Object& o) {
    const u8 s = o.state;
    if (s == 0x13 || s == 0x0D || s == 0x0C || s == 0x11 || s == 0x43) {
        return;
    }
    if ((terrain_state.flags_previous & 0x1F) != 0x12) {
        // 6148: entering, the ramp's direction from its block (34, 37 and
        // others: status bit 7 set; 35, 36, 38, 39: clear).
        if (o.floor_off) {
            return;
        }
        const u8 b = terrain_state.block;
        if (b == 0x35 || b == 0x38 || b == 0x36 || b == 0x39) {
            o.status &= u8(~0x80);
        } else {
            o.status |= 0x80;
        }
        return;
    }
    // 6091: on the floor of the ramp.
    if (!(o.terrain & 0x02)) {
        return;
    }
    const u8 b = terrain_state.block;
    const std::uint16_t v = o.vx;
    if (b < 0x37) {
        if (o.status & 0x80) {
            // 60BD: carried right by 3/4 pixel per frame up to the top speed.
            o.display &= u8(~display_flag::mirrored);
            const std::uint16_t next = std::uint16_t(o.vx + 0x00C0);
            if ((next & 0x8000) || player.top_speed >= next) {
                o.vx = next;
            }
            return;
        }
        if (!(v & 0x8000) || v >= 0xFC80) {
            return;
        }
        if (o.x & 0x10) {
            return;
        }
        if (b == 0x35) {
            ramp_launch(o); // 6113
        }
        return;
    }
    if (o.status & 0x80) {
        // 60F7: carried left likewise.
        o.display |= display_flag::mirrored;
        const std::uint16_t next = std::uint16_t(o.vx + 0xFF40);
        if (!(next & 0x8000) || player.top_speed < next) {
            o.vx = next;
        }
        return;
    }
    if ((v & 0x8000) || v < 0x0380) {
        return;
    }
    if (!(o.x & 0x10)) {
        return;
    }
    if (b == 0x38) {
        ramp_launch(o);
    }
}

// 6113: off the ramp's lip: up at its speed plus 2.5, rolling through the
// air (3D99), sound B1 (not in state 23).
void Game::ramp_launch(Object& o) {
    const std::uint16_t v = o.vx;
    const std::uint16_t up = (v & 0x8000) ? v : std::uint16_t(-v);
    o.vy = std::uint16_t(up + 0xFD80);
    o.vx = 0;
    if (o.state == 0x23) {
        return;
    }
    sound_.request(0xB1, 1);
    o.flags |= 0x02;
    o.terrain &= u8(~0x02);
    o.contact &= u8(~0x02);
    to_bounced_roll(o); // 3D99
}

// 6B3C: kind 10 (and 04): in state 12 the player lets go of his holder
// (D3C9: state 2), loses his power-up and falls (3B6F); in states 18 and
// 27 (Tails flying) he drops.
void Game::block_kind_10(Object& o) {
    if (objects[0].state == 0x12) {
        if (!player.holder) {
            return;
        }
        objects[player.holder - 1u].next_state = 0x02;
        player.holder = 0;
        objects[0].power_up = 0;
        to_falling_state(o); // 3B6F
        return;
    }
    if (o.state == 0x18 || o.state == 0x27) {
        player.tails_landing = 0xFF;
        to_falling_from_flight(o); // 3B62
    }
}

// 6780: the walls left and right of the player (6855 the left, then the
// right side), with their blocks' kinds.
void Game::player_walls(Object& o) {
    if (terrain_state.spring_ticks) {
        --terrain_state.spring_ticks; // DBE0
    }
    o.terrain &= u8(~0x0C);
    if (left_wall(o)) { // 6855
        return;
    }
    const auto box = side_probe(o, true); // 6A2E
    probe_terrain(o, box.dx, box.dy);
    if (wall_path_switch(o, 0x7A, 0x00)) { // 69FB
        return;
    }
    const u8 kind = probe.flags & 0x1F;
    if (kind == 0x10 || kind == 0x04) {
        block_kind_10(o);
    }
    switch (probe.flags & 0x1F) {
    case 0x05: // 6960
        spike_wall(o, true);
        return;
    case 0x0D:
        if (!break_wall(o, true)) {  // 6910
            push_from_wall(o, true); // 67D9
        }
        return;
    case 0x16: // 69AB: a ring block breaks when rolled into while not rising
        if (o.state == 0x0F || o.state == 0x15 || !(o.flags & 0x02) || (o.vy & 0x8000)) {
            push_from_wall(o, true);
        } else {
            break_ring_block(o); // 71D8
        }
        return;
    case 0x0F: // 69E7
        bumper(o, 0xFC00, o.vy);
        return;
    case 0x18:
        push_from_wall(o, true);
        return;
    case 0x12:
        return;
    default:
        if (probe.flags & 0x80) {
            push_from_wall(o, true);
        }
        return;
    }
}

// 6855: the left side. Returns true where the original leaves 6780 (69FB/
// 6A14 drop their caller's return address).
bool Game::left_wall(Object& o) {
    const auto box = side_probe(o, false); // 6A6D
    probe_terrain(o, box.dx, box.dy);
    if (wall_path_switch(o, 0x7F, 0x01)) { // 6A14
        return false;                      // 6855 ends; 6780 goes on (6792)
    }
    const u8 kind = probe.flags & 0x1F;
    if (kind == 0x10 || kind == 0x04) {
        block_kind_10(o);
    }
    switch (probe.flags & 0x1F) {
    case 0x05: // 6980
        spike_wall(o, false);
        return false;
    case 0x0D:
        if (!break_wall(o, false)) {  // 6937
            push_from_wall(o, false); // 6896
        }
        return false;
    case 0x16: // 69C9
        if (o.state == 0x0F || o.state == 0x15 || !(o.flags & 0x02) || (o.vy & 0x8000)) {
            push_from_wall(o, false);
        } else {
            break_ring_block(o); // 71D8
        }
        return false;
    case 0x0F: // 69F1
        bumper(o, 0x0400, o.vy);
        return false;
    case 0x18:
        push_from_wall(o, false);
        return false;
    default:
        if (probe.flags & 0x80) {
            push_from_wall(o, false);
        }
        return false;
    }
}

// 6960 / 6980: kind 5 walls: blocks F2 and F3 are plain walls; others in
// the way of a player about to ride a shell (state 16) break like kind 0D
// walls, and do nothing to one about to be thrown back (1E). After the
// push, spikes (F4 on the right, F5 on the left) hurt.
void Game::spike_wall(Object& o, bool right) {
    const u8 block = probe.block;
    if ((block & 0xFE) != 0xF2) {
        if (o.next_state == 0x16) {
            if (!break_wall(o, right)) {
                push_from_wall(o, right);
            }
            return;
        }
        if (o.next_state == 0x1E) {
            return;
        }
    }
    push_from_wall(o, right); // 67D9 / 6896
    if (probe.block != (right ? 0xF4 : 0xF5)) {
        return;
    }
    if (o.flags & 0x80) { // 699D
        return;
    }
    o.vy = 0x0100;
    hurt_player_now(o); // 384A
}

// 67D9 (right) / 6896 (left): out of the wall by its profile at the
// probed height; stopped when facing it; a kind 0A wall's spring throws
// him back (6836 / 68F4).
void Game::push_from_wall(Object& o, bool right) {
    u8 w = probe.wall;
    unsigned push;
    if (right) {
        if (!(w & 0x40)) {
            w &= 0x3F;
            if (!w) {
                return;
            }
            const u8 x = probe.x & 0x1F;
            if (x >= w) {
                return;
            }
            push = x;
        } else {
            w &= 0x3F;
            if (!w) {
                return;
            }
            const unsigned edge = (((probe.x + 0x20) & 0xFFE0) - w) & 0xFFFF;
            const unsigned d = (probe.x - edge) & 0xFFFF;
            if (probe.x < edge) {
                return;
            }
            push = d;
        }
        o.x = std::uint16_t(o.x - push);
    } else {
        if (!(w & 0x40)) {
            w &= 0x3F;
            if (!w) {
                return;
            }
            const u8 x = probe.x & 0x1F;
            if (x >= w) {
                return;
            }
            push = u8(w - x - 1);
        } else {
            w &= 0x3F;
            if (!w) {
                return;
            }
            const unsigned last = (((probe.x + 0x20) & 0xFFE0) - 1) & 0xFFFF;
            if (last < probe.x) {
                return;
            }
            const unsigned d = (last - probe.x) & 0xFFFF;
            if (u8(d) >= w) {
                return;
            }
            push = d;
        }
        o.x = std::uint16_t(o.x + push);
    }
    if ((terrain_state.flags & 0x1F) == 0x12) {
        return;
    }
    o.terrain |= right ? 0x04 : 0x08;
    const bool facing =
        right ? !(o.display & display_flag::mirrored) : (o.display & display_flag::mirrored);
    if (facing) {
        o.vx = 0;
    }
    if ((probe.flags & 0x1F) != 0x0A) {
        return;
    }
    // 6836 / 68F4: a spring in the wall throws him back at 6 (state 43 is
    // excepted only on the right).
    const u8 s = o.state;
    if (s == 0x29 || s == 0x11 || (right && s == 0x43) || terrain_state.spring_ticks) {
        return;
    }
    terrain_state.spring_ticks = 0x08;
    wall_spring(o, right ? 0xFA00 : 0x0600); // 3E0C / 3DFF
}

// 69FB / 6A14: blocks 7A-7E (and 7F-83) set the player's path (byte 25) to
// 0 (1); in state 18 he drops (3B62). The caller's caller goes on.
bool Game::wall_path_switch(Object& o, u8 first, u8 path) {
    if (u8(probe.block - first) >= 5) {
        return false;
    }
    if (o.state == 0x18) {
        to_falling_from_flight(o); // 3B62
    }
    o.path = path;
    return true;
}

// 6A6D / 6A2E: the side probes: the player's box corners (D420-D427);
// in states 41, 18 and 29 fixed offsets with Y from his height (D529).
// Y is 4 up, or on odd counts of D141 his top.
Game::SideProbe Game::side_probe(const Object& o, bool right) {
    SideProbe p{right ? player_box.right_x : player_box.left_x,
                right ? player_box.right_y : player_box.left_y};
    const u8 s = o.state;
    if (s != 0x41 && s != 0x18 && s != 0x29) {
        return p;
    }
    // 6A44-6A6C: 14 out (12 in state 29); 4 up, or on odd counts of
    // D141 at his top (1 - height).
    const std::uint16_t out = s == 0x29 ? 0x0C : 0x0E;
    p.dx = right ? out : std::uint16_t(-out);
    p.dy = (flow.placement_ticks & 1) ? std::uint16_t(1 - o.frame_height) : 0xFFFC;
    return p;
}

// 6C13: an overhang's side walls (not in states 13, 0D, 0C): blocks 54,
// 5D and AE wall the player in from the right (6C8E), others from the
// left (6CC7). Rising faster than 3 under block 5D (FD) he is turned
// into state 35 (36) at 1.25 the other way; slower he is pushed down at 1.
void Game::overhead_wall(Object& o) {
    const u8 s = o.state;
    if (s == 0x13 || s == 0x0D || s == 0x0C) {
        return;
    }
    o.terrain &= u8(~0x01);
    const u8 b = probe.block;
    const bool right = b == 0x54 || b == 0x5D || b == 0xAE;
    if (right) {
        wall_on_right(o); // 6C8E
    } else {
        wall_on_left(o); // 6CC7
    }
    if (!(o.vy & 0x8000)) {
        return;
    }
    if (o.vy >= 0xFD00) {
        o.vy = 0x0100; // 6C87
        return;
    }
    const u8 turn = right ? 0x35 : 0x36;
    if (o.state == turn || b != (right ? 0x5D : 0xFD)) {
        return;
    }
    o.vx = right ? 0xFEC0 : 0x0140;
    o.next_state = turn;
}

// 6C8E: pushed left out of the wall profile (D414) of the probed block;
// stopped unless facing left.
void Game::wall_on_right(Object& o) {
    const u8 w = probe.wall & 0x3F;
    if (!w) {
        return;
    }
    const std::uint16_t face = std::uint16_t(((probe.x + 0x20) & 0xFFE0) - w);
    if (probe.x < face) {
        return;
    }
    o.x = std::uint16_t(o.x - (probe.x - face));
    o.terrain |= 0x04;
    if (!(o.display & display_flag::mirrored)) {
        o.vx = 0;
    }
}

// 6CC7: pushed right; stopped when facing left.
void Game::wall_on_left(Object& o) {
    const u8 w = probe.wall & 0x3F;
    if (!w) {
        return;
    }
    const u8 at = probe.x & 0x1F;
    if (at >= w) {
        return;
    }
    o.x = std::uint16_t(o.x + (w - at - 1));
    o.terrain |= 0x08;
    if (o.display & display_flag::mirrored) {
        o.vx = 0;
    }
}

// 6A92: the ceiling while rising (or in states 29/23, or carried): the
// block above (at minus the player's height); kinds 0D, 15, 05, 1C, 0F,
// 09, 10 have effects; a solid ceiling pushes him down and stops the climb.
void Game::player_ceiling(Object& o) {
    o.terrain &= u8(~0x01);
    if (!player.carried && o.state != 0x29 && o.state != 0x23) {
        if (o.terrain & 0x02) {
            return;
        }
        if (!(o.vy & 0x8000)) {
            return;
        }
    }
    const unsigned dy = (0u - o.frame_height) & 0xFFFF;
    probe_terrain(o, 0, dy);
    ceiling_solid_from_below(o); // 6D91
    const u8 flags = probe.flags;
    switch (flags & 0x1F) {
    case 0x0D: // 6B70: a breakable block breaks; on a platform it crushes
        if (player.carried) {
            throw Unrecovered("crushed under a breakable block (6B77)");
        }
        shatter_block(o); // 7228
        return;
    case 0x15: // 6BDD: bounced away rolling (state 1B, 3D99), sound B5
        if (o.state == 0x29 || !(o.terrain & 0x01)) {
            return;
        }
        o.vy = 0x0380;
        o.vx = 0x0400;
        sound_.request(0xB5, 1);
        to_bounced_roll(o); // 3D99
        return;
    case 0x05: // 6BFC: spikes above: pushed down at 2 and hurt (as when crushed)
        o.vy = 0x0200;
        o.terrain &= u8(~0x01);
        if (o.flags & 0x80) {
            return;
        }
        player.touch_kind = 0x50;
        hurt_player_now(o); // 384A
        return;
    case 0x09: // 6BAB: a spring above throws him down at 7, sound B5
        if (o.state == 0x29 || o.state == 0x11 || o.state == 0x43) {
            return;
        }
        if (!(o.terrain & 0x01) || !(o.vy & 0x8000)) {
            return;
        }
        o.vy = 0x0700;
        o.terrain &= u8(~0x03);
        o.contact &= u8(~0x03);
        sound_.request(0xB5, 1);
        return;
    case 0x10:
        block_kind_10(o); // 6B3C
        return;
    case 0x0F: // 6B9B
        bumper(o, std::uint16_t(-o.vx), 0x0200);
        return;
    case 0x1C:
        overhead_wall(o); // 6C13
        return;
    default:
        break;
    }
    if (!(flags & 0x80)) {
        return;
    }
    // 6AF3: only a block in another 32-pixel row than the player's.
    if ((probe.y & 0xFFE0) == (o.y & 0xFFE0)) {
        return;
    }
    const u8 c = probe.y & 0x1F;
    const u8 f = probe.floor;
    if (!(f & 0x3F)) {
        return;
    }
    const u8 bottom = (f & 0x40) ? u8(f & 0x3F) : 0x20;
    if (bottom < c) {
        return;
    }
    o.y = std::uint16_t(o.y + u8(bottom - c));
    o.terrain |= 0x01;
    o.vy = 0;
    if (o.state == 0x0A) {
        o.next_state = 0x1B;
    }
}

// 6D91: a block solid from below (bit 7) while rising: under a top-only
// profile (bit 6) the player is pushed down to it.
void Game::ceiling_solid_from_below(Object& o) {
    const u8 flags = probe.flags;
    if (!(flags & 0x80)) {
        return;
    }
    if ((flags & 0x1F) == 0x0A) {
        return;
    }
    if (!(o.vy & 0x8000)) {
        return;
    }
    if (!(probe.floor & 0x40)) {
        const u8 kind = flags & 0x1F;
        if (kind != 0x01 && kind != 0x02) {
            return;
        }
        // 6DEA: kinds 1 and 2 (but blocks 19, 1A) are solid to their full
        // height from below: 1F (20 for blocks A4-A7, A9).
        const u8 b = probe.block;
        if (b == 0x19 || b == 0x1A) {
            return;
        }
        const bool tall = b == 0xA4 || b == 0xA5 || b == 0xA6 || b == 0xA7 || b == 0xA9;
        probe.floor = tall ? 0x20 : 0x1F;
    }
    // 6DA9
    const u8 c = probe.y & 0x1F;
    const u8 f = probe.floor & 0x1F;
    if (f < c) {
        return;
    }
    o.y = std::uint16_t(o.y + u8(f - c));
    o.terrain |= 0x01;
    const u8 kind = probe.flags & 0x1F;
    if (kind == 0x1C || kind == 0x09) {
        return;
    }
    o.vy = 0;
}

// 6CF2: in the air, the block ahead at mid height stops the player (except
// passable kinds).
void Game::player_sides(Object& o) {
    if (o.state == 0x29) {
        return;
    }
    if ((probe.flags & 0x1F) == 0x09 || (terrain_state.flags & 0x1F) == 0x09) {
        return;
    }
    if (o.state != 0x29 && o.state != 0x41 && !(o.vy & 0x8000)) {
        return;
    }
    const unsigned dx = (o.vx & 0x8000) ? 0xFFF8u : 0x0008u;
    const unsigned dy = (0u - o.frame_height) & 0xFFFF;
    probe_terrain(o, dx, dy);
    const u8 flags = probe.flags;
    if (!(flags & 0x80)) {
        return;
    }
    switch (flags & 0x1F) {
    case 0x00:
    case 0x0B:
    case 0x16:
    case 0x01:
    case 0x0D:
    case 0x03:
    case 0x11:
    case 0x12:
    case 0x17:
    case 0x15:
    case 0x0A:
    case 0x05:
    case 0x1C:
        return;
    case 0x10:
    case 0x04:
        block_kind_10(o);
        return;
    case 0x0F: // 6B9B: a side bumper: thrown back (the speed negated) and down at 2
        bumper(o, std::uint16_t(-o.vx), 0x0200); // 72B2
        return;
    default:
        break;
    }
    o.vx = 0;
    o.vy = 0;
    o.terrain |= 0x01;
    // 6D81 tests the speed it has just cleared: always the right side.
    o.terrain |= 0x04;
}

// 6E16: the block at the player's head (709A): kinds 10/04 drop him, 07
// collects rings, 1A, 14, 13 and blocks 4F/BF have effects.
void Game::player_head(Object& o) {
    unsigned dy = 0xFFE6;
    if (o.state == 0x16 || o.state == 0x17) {
        dy = 0xFFE2;
    } else if (o.frame_ticks & 0x01) {
        dy = 0xFFF0;
    }
    if (o.placement) { // D53E: the player's byte 3E is a flag (crouched)
        dy = 0xFFF0;
    }
    terrain_state.head_previous = terrain_state.head;
    const u8 flags = probe_flags(o, 0, dy);
    terrain_state.head = flags;
    switch (flags & 0x1F) {
    case 0x10:
    case 0x04:
        block_kind_10(o);
        return;
    case 0x07:
        collect_map_ring(o); // 6E7D
        return;
    case 0x1A: // 6F2C: a booster: on the floor, off at 7 pixels per frame (sound C7)
        if (!(o.terrain & 0x02)) {
            return;
        }
        player.top_speed = 0x0700;
        o.vx = 0x0700;
        o.flags &= u8(~0x01);
        sound_.request(0xC7, 1);
        return;
    case 0x14:
        diagonal_spring(o); // 6F44
        return;
    case 0x13:
        pipe_junction(o); // 72F1
        return;
    default:
        break;
    }
    if (probe.block == 0x4F || probe.block == 0xBF) {
        pipe_junction(o); // 72F1
    }
}

// 72F1: pipe junctions at the player's head (blocks B0-B9; 4F and BF the
// exits), not in states 11, 29, 26, 43 nor on a platform. Each new block
// (D41B against D41C) may take the player into a pipe (state 21, at the
// block's middle) heading up, down, left or right at 5.5; byte 3E keeps
// the heading (B6 up, B7 down, B8 right, B9 left). Reaching a junction of
// its own heading lets him out (state 40, sound C4); an exit block ends
// the pipe (state 22).
void Game::pipe_junction(Object& o) {
    const u8 s = o.state;
    if (s == 0x11 || s == 0x29 || s == 0x26 || s == 0x43 || player.carried) {
        return;
    }
    auto& ts = terrain_state;
    ts.pipe_block_previous = ts.pipe_block;
    ts.pipe_block = probe.block;
    const bool fresh = ts.pipe_block_previous != ts.pipe_block;
    const u8 b = probe.block;
    enum class Go { up, down, left, right };
    const auto travel = [&](Go go) {
        switch (go) {
        case Go::up: // 740F
            o.vx = 0;
            o.vy = 0xFA80;
            break;
        case Go::down: // 73E7
            o.vx = 0;
            o.vy = 0x0580;
            break;
        case Go::left: // 7437
            o.vx = 0xFA80;
            o.vy = 0;
            break;
        case Go::right: // 745F
            o.vx = 0x0580;
            o.vy = 0;
            break;
        }
        // 74D0
        if (go == Go::up || go == Go::down) {
            o.placement = (o.vy & 0x8000) ? 0xB6 : 0xB7;
        } else {
            o.placement = (o.vx & 0x8000) ? 0xB9 : 0xB8;
        }
    };
    const auto enter = [&] { // 74B1
        o.next_state = 0x21;
        o.x = std::uint16_t((probe.x & 0xFF00) | ((probe.x & 0xE0) | 0x10));
        o.y = std::uint16_t((probe.y & 0xFF00) | ((probe.y & 0xE0) | 0x0C));
    };
    // 7493: a junction of the player's own heading lets him out.
    const auto leaves = [&](u8 heading) {
        if (heading == o.placement) {
            sound_.request(0xC4, 1);
            o.next_state = 0x40;
            o.placement = 0xFF;
            return true;
        }
        return o.state == 0x21;
    };
    if (b == 0x4F || b == 0xBF) { // 7472
        if (o.state != 0x21 || !fresh) {
            return;
        }
        enter();
        o.vx = 0;
        o.vy = 0;
        o.next_state = 0x22;
        return;
    }
    switch (b) {
    case 0xB2:
    case 0xB3:
    case 0xB4:
    case 0xB5: { // 735A-73B4: corners
        if (!o.placement || !fresh) {
            return;
        }
        enter();
        const bool moving_sideways = o.vx != 0;
        if (b == 0xB2) {
            travel(moving_sideways ? Go::up : Go::left);
        } else if (b == 0xB3) {
            travel(moving_sideways ? Go::up : Go::right);
        } else if (b == 0xB4) {
            travel(moving_sideways ? Go::down : Go::left);
        } else {
            travel(moving_sideways ? Go::down : Go::right);
        }
        return;
    }
    case 0xB6:
    case 0xB7:
    case 0xB8:
    case 0xB9: { // 73D2-744A: entries
        if (!fresh || leaves(b)) {
            return;
        }
        enter();
        travel(b == 0xB6 ? Go::down : b == 0xB7 ? Go::up : b == 0xB8 ? Go::left : Go::right);
        return;
    }
    default: // B0, B1 and the rest: nothing
        return;
    }
}

// 6F44: kind 14 at the head, a diagonal spring (in its upper half, the
// player not rising; not in states 29, 11, 43): up at 5.5 and sideways at 4
// (right on blocks 5A and 5B, else left, facing that way), sound B5; then
// sprung (state 1C, 3DE4) unless in state 16 (or 23: only the speed).
void Game::diagonal_spring(Object& o) {
    if (o.state == 0x29) {
        return;
    }
    o.status = u8((o.status | 0x01) & ~0x04);
    leave_special_state(o); // 5854
    const u8 s = o.state;
    if (s == 0x11 || s == 0x43) {
        return;
    }
    if (objects[0].y & 0x10) {
        return;
    }
    if (objects[0].vy & 0x8000) {
        return;
    }
    const bool right = probe.block == 0x5A || probe.block == 0x5B;
    o.display =
        right ? u8(o.display & ~display_flag::mirrored) : u8(o.display | display_flag::mirrored);
    objects[0].vx = right ? 0x0400 : 0xFC00;
    constexpr std::uint16_t vy = 0xFA80;
    sound_.request(0xB5, 1);
    if (o.state == 0x16) {
        return;
    }
    if (o.state == 0x23) {
        o.vy = vy;
        return;
    }
    player.spring_flag = 0;
    // 3DE4
    if (objects[0].vy & 0x8000) {
        return;
    }
    objects[0].next_state = 0x1C;
    objects[0].vy = vy;
    objects[0].flags |= 0x03;
    objects[0].terrain &= u8(~0x02);
}

// 6E7D: a ring in the map (blocks 64 on): table 6EFB says which quarters
// of the block hold rings (by the probe's X and Y halves); a ring there is
// taken: the block becomes the one without it (table 6EFB + 18), its
// sparkle appears, sound C9, a ring counted.
void Game::collect_map_ring(Object& o) {
    const unsigned index = u8(probe.block - 0x64);
    terrain_state.ring_block = u8(index);
    const unsigned x_half = (probe.x >> 4) & 0x01;
    const unsigned y_half = (probe.y >> 3) & 0x02;
    const Table rings{0x01, 0x6EFB};
    const unsigned entry = index * 4 + x_half + y_half;
    if (!rom.byte(rings, entry)) {
        return;
    }
    const u8 block = rom.byte(rings, entry + 0x18);
    set_map_block(probe.cell, block); // the block and its picture (2CEB)
    probe.ring_x = probe.x;           // D409: where the sparkle appears
    probe.ring_y = probe.y;           // D40B
    spawn_object(o, 0x03, 0x00);      // 50E1: the sparkle
    sound_.request(0xC9, 1);
    add_ring(); // 33AB
}

} // namespace triple_trouble_gg
