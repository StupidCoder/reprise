// Loading an act (3167): its header, map, start position, properties,
// tile art and colours.

#include "triple_trouble_gg/game/game.hpp"
#include <algorithm>

namespace triple_trouble_gg {

namespace {
// 434F (bank 1): per zone a table of acts; per act the 22-byte header.
constexpr Table act_headers{0x01, 0x434F};
// 0E:9A09: per zone and act the start: camera X, Y, the player's X, Y.
constexpr Table start_positions{0x0E, 0x9A09};
// 1D:87F8: per zone and act the block properties table (bank 0E), the
// ring animation's art and its place, and the four tile animations.
constexpr Table act_properties{0x1D, 0x87F8};
// 1A:B678: per act (zone * 3 + act) 7 bytes: the main tile art (bank,
// VRAM address, source) and a list (bank 1A) of further art: 5 bytes each
// (bank with bit 7 mirrored, VRAM address, source), FF ends.
constexpr Table act_tiles{0x1A, 0xB678};
// 0E:BF1C: per act the background and sprite palette numbers.
constexpr Table act_palettes{0x0E, 0xBF1C};
// Art loaded with every act: 0A:A370 (the rings, at the address given),
// 0A:A546 and 1D:8A94.
constexpr Table ring_art{0x0A, 0xA370};
} // namespace

// The per-zone, per-act entry of a two-level table (zone, then act).
Table Game::act_entry(Table table) const {
    const unsigned zone_table = rom.word(table, flow.zone * 2u);
    return {table.bank, rom.word(table.bank, zone_table + flow.act * 2u)};
}

// 3167: an act, from cleared memory to its first frame: the map and
// header, the start, the properties, the art. (Paths for play, the special
// stages and zone 9 are added as they are reconstructed.)
Flow Game::load_act() {
    di();
    clear_vram();       // 7BCF
    clear_game_state(); // 327E
    score.rings_low = 0;
    score.rings_high = 0;
    clear_sprites();   // 2757
    clear_map_state(); // 428B
    load_act_header(); // 4299
    co_await busy(211242 + 59677 + 3593 + 12074 + 1250 + 200);
    co_await load_map(); // 40A5
    di();
    if (flow.mode == 0x0D) {
        // 3222: the second kind of special stage: video memory cleared
        // again and its art (74F7: 1A:B720 Sonic, B727 Tails), the digits
        // (76F6), the rings' HUD (1B:BE43) and their count.
        clear_vram(); // 7BCF
        const Table entry{0x1A,
                          flow.character == 1 ? std::uint16_t(0xB720) : std::uint16_t(0xB727)};
        unsigned clocks = 211242 + load_tile_entry(entry);
        clocks += 17 + 85 + load_tiles(0x05C0, 0x0A, 0xACEA); // 76F6
        set_special_hud();                                    // 1B:BE43
        show_rings();                                         // 33BA
        co_await busy(clocks);
        co_await finish_act_load();
        co_return;
    }
    if (flow.zone == 9) {
        // 3235: zone 9: its properties and art, the rings' art and the
        // common art; no HUD; the partner and the player's character.
        load_act_properties();                                                             // 3321
        unsigned clocks = load_act_tiles();                                                // 7524
        clocks += 17 + 17 + 53 + 85 + load_tiles(0x0480, ring_art.bank, ring_art.address); // 7600
        clocks += 17 + 85 + load_tiles(0x0400, 0x0A, 0xA546);                              // 761C
        clocks += 17 + 85 + load_tiles(0x0200, 0x1D, 0x8A94);                              // 762C
        place_partner();                  // 1B:BEF5
        objects[0].type = flow.character; // D13F
        co_await busy(clocks);
        co_await finish_act_load();
        co_return;
    }
    load_act_properties(); // 3321: bank 1D, 872A
    unsigned clocks =
        17 + 17 + 53 + 85 + load_tiles(0x0480, ring_art.bank, ring_art.address); // 7600
    if (flow.mode == 0x11) {
        throw Unrecovered("act tiles of mode 11 (7513)");
    }
    clocks += load_act_tiles(); // 7524
    if (flow.mode != mode::intro) {
        clocks += load_play_art(); // 31BA-3220
    }
    co_await busy(clocks);
    co_await finish_act_load();
}

// 3258: act 3 (outside zone 6) has more object sprites; the clock runs
// (unless D149); a frame's wait.
Flow Game::finish_act_load() {
    if (flow.zone != 6 && flow.act == 2) {
        sprites.hud_small = 0xFF;
    }
    if (!flow.no_clock) {
        clock.running = 0xFF;
    }
    co_await next_frame();
}

// 4299: the act's header: the block table and the map (banks and
// addresses), the map's row layout, the camera's limits; the camera window
// starts at screen X 60, Y 78 (4C2D).
void Game::load_act_header() {
    const Table h = act_entry(act_headers);
    auto b = [&](unsigned i) {
        return rom.byte(h, i);
    };
    auto w = [&](unsigned i) {
        return rom.word(h, i);
    };
    level.block_bank = b(0);
    level.block_table = w(1);
    level.map_bank = b(3);
    level.map_source = w(4);
    level.row_stride = w(6);
    level.row_up = w(8);
    level.bottom_offset = w(10);
    camera.min_x = camera.act_min_x = w(12);
    camera.min_y = camera.act_min_y = w(14);
    camera.max_x = camera.act_max_x = w(16);
    camera.max_y = camera.act_max_y = std::uint16_t(w(18) - 0x18);
    level.row_table = w(20);
    camera.aim_x = 0x60;
    camera.aim_y = 0x78;
    reset_camera_window(); // 4C2D
}

// Widescreen (enhancements/widescreen.cpp): the header's camera limits
// (4299; bosses and scripts later only narrow them, or widen the right one
// back to the header's), widened by any later limits.
Game::Area Game::camera_area() const {
    const Table h = act_entry(act_headers);
    Area a{int(rom.word(h, 12)), int(rom.word(h, 14)), int(rom.word(h, 16)),
           int(rom.word(h, 18)) - 0x18};
    a.left = std::min(a.left, int(camera.min_x));
    a.top = std::min(a.top, int(camera.min_y));
    a.right = std::max(a.right, int(camera.max_x));
    a.bottom = std::max(a.bottom, int(camera.max_y));
    return a;
}

// 40A5 (outside the special stages): the map decompressed, the start
// position, then the 29 rows of the first screen made ready one per frame
// with the camera moving down 8 pixels each; then the camera at the start.
Flow Game::load_map() {
    if (flow.mode == 0x0D) {
        // 40A5: the flight stage has no map: the name table filled with
        // entry 00FE and the plane's first picture (bank 16: 9AAC Sonic,
        // 9C50 Tails) at 3ADA, 6 by 7.
        di();
        display.level_shown = false;
        display.map.fill(0x00FE);
        unsigned clocks = fill_clocks(0x0380);
        clocks +=
            draw_rect(0x3ADA, 0x16, flow.character == 1 ? 0x9AAC : 0x9C50, 0x06, 0x07); // 2C3B
        ei();                                                                           // 2C73
        co_await busy(clocks);
        co_return;
    }
    unsigned clocks = decompress_map(); // 459F
    clocks += 17 + 400;
    load_start_position(); // 41C0
    co_await busy(clocks);
    co_await draw_first_screen(); // 4163
}

// 4163 (the rest of 4160): the 29 rows of the first screen made ready one
// per frame with the camera moving down 8 pixels each; then the camera at
// the start.
Flow Game::draw_first_screen() {
    ei();
    video.busy = 0;
    camera.x = camera.next_x = camera_targets.start_x;
    camera.y = camera.next_y = camera_targets.start_y;
    for (int i = 0; i < 0x1D; ++i) {
        di();
        camera.next_y = std::uint16_t(camera.y + 8);
        fetch_map_row(); // 46AA
        camera.map_flags |= map_flag::update_pending;
        co_await next_frame();
    }
    camera.next_x = camera.x = camera_targets.start_x;
    camera.next_y = camera.y = camera_targets.start_y;
    // 41AD: the scroll for the start (Y + 10, where 4034 adds 11).
    camera.scroll_y = u8((camera.y + 0x10) % 0xE0);
    display.level_shown = true; // the name table now holds the act's map
    picture_origin.pending = true;
    ei();
}

// 459F: the map's compression: a control byte gives eight steps, lowest bit
// first: 1 copies a byte; 0 reads a word: 0 ends, otherwise the low 12 bits
// count back from the output (as 1000 - n) and the top 4 bits plus 3 give
// the length of the copy. Output from C001. Returns the CPU clocks.
unsigned Game::decompress_map() {
    const unsigned bank = level.map_bank;
    unsigned src = level.map_source;
    unsigned out = 1; // C001
    level_map.holds_title_count = false;
    unsigned t = 17 + 14 + 19 + 53 + 19 + 19 + 10;
    auto put = [&](u8 v) {
        if (out >= level_map.cells.size()) {
            throw Unrecovered("map larger than C000-CFFF");
        }
        level_map.cells[out++] = v;
    };
    for (;;) {
        u8 control = rom.byte(bank, src++);
        t += 7 + 6 + 7;
        for (int bit = 0; bit < 8; ++bit) {
            t += 8;
            const bool literal = control & 1;
            control >>= 1;
            if (literal) {
                put(rom.byte(bank, src++));
                t += 7 + 16 + 6 + 12;
            } else {
                const unsigned c = rom.byte(bank, src), b = rom.byte(bank, src + 1);
                src += 2;
                t += 12 + 11 + 7 + 6 + 7 + 6 + 4 + 4;
                if (!(b | c)) {
                    return t + 12 + 10 + 10; // 45E3
                }
                t += 7 + 11 + 4 + 7 + 4 + 4 + 11 + 4 + 7 + 16 + 7 + 4 + 7 + 10 + 10;
                const unsigned length = (b >> 4) + 3u;
                const unsigned from = (0xC000 + out + ((0xF0u | b) << 8 | c)) & 0xFFFF;
                if (from < 0xC000) {
                    throw Unrecovered("map copy from before C000");
                }
                for (unsigned i = 0; i < length; ++i) {
                    put(level_map.cells[from - 0xC000 + i]);
                }
                t += 21 * length - 5;
            }
            t += bit < 7 ? 13 : 8;
        }
        t += 12;
    }
}

// 41C0: the start (table 0E:9A09, for the intro and when no checkpoint or
// special stage return applies). A checkpoint (D171) or a special
// stage's return point (D172, with D17A) applies once.
void Game::load_start_position() {
    if (flow.mode != 0x09 && flow.zone != 6) {
        if (flow.special_entry && flow.special_return[0]) {
            // 424F: back from a special stage where the player left: his
            // place, the camera, the clock; once.
            objects[0].x = flow.special_return[0];
            objects[0].y = flow.special_return[1];
            camera.x = camera_targets.start_x = flow.special_camera[0];
            camera.y = camera_targets.start_y = flow.special_camera[1];
            score.seconds = flow.checkpoint_time[0];
            score.minutes = flow.checkpoint_time[1];
            score.clock_extra = flow.checkpoint_time[2];
            flow.special_return[0] = 0;
            return;
        }
        if (flow.restart_point) {
            // 4222: once at the checkpoint, its camera, the clock at 0.
            flow.restart_point = 0;
            objects[0].x = flow.checkpoint[0];
            objects[0].y = flow.checkpoint[1];
            camera.x = camera_targets.start_x = flow.checkpoint_camera[0];
            camera.y = camera_targets.start_y = flow.checkpoint_camera[1];
            score.seconds = score.minutes = score.clock_extra = 0;
            return;
        }
    }
    const Table s = act_entry(start_positions);
    camera_targets.start_x = rom.word(s, 0);
    camera_targets.start_y = rom.word(s, 2);
    objects[0].x = rom.word(s, 4);
    objects[0].y = rom.word(s, 6);
}

// 3321 (bank 1D, 872A): the act's block properties, ring animation art and
// tile animations.
void Game::load_act_properties() {
    const Table p = act_entry(act_properties);
    act.block_properties = rom.word(p, 0);
    animation.ring_art = rom.word(p, 2);
    animation.ring_vram = rom.word(p, 4);
    for (unsigned i = 0; i < 4; ++i) {
        animation.animators[i].type = rom.byte(p, 6 + i);
    }
}

// 7524: the act's tile art (table 1A:B678). Returns the CPU clocks.
unsigned Game::load_act_tiles() {
    return load_tile_entry(act_tiles + (flow.zone * 3u + flow.act) * 7u);
}

// 7548: an art entry (bank, video address, source; then the address of a
// list in bank 1A: bank byte (bit 7 mirrored), video address, source; FF
// ends).
unsigned Game::load_tile_entry(Table entry) {
    unsigned clocks = 17 + 4 + 53 + 300;
    clocks += load_tiles(rom.word(entry, 1), rom.byte(entry, 0), rom.word(entry, 3));
    Table list{0x1A, rom.word(entry, 5)};
    for (;;) {
        const u8 bank = rom.byte(list, 0);
        clocks += 120;
        if (bank == 0xFF) {
            break;
        }
        clocks += load_tiles(rom.word(list, 1), bank & 0x1F, rom.word(list, 3), bank & 0x80);
        list = list + 5;
    }
    // 75A2: Atomic Destroyer act 3 entered at a checkpoint (D169) forgets
    // it and loads more art.
    if (flow.zone == 5 && flow.act == 2 && flow.checkpoint[0]) {
        throw Unrecovered("Atomic Destroyer act 3 from a checkpoint (75B4)");
    }
    return clocks;
}

// 31BA-3220: for play: the common art (0A:A546 at 0400, 1D:8A94 at 0200),
// the HUD (1B:BD12, the clock, rings and lives digits, the player's face,
// 780B), act 3's extra art; the rings and the clock reset; the player's
// character, the act's partner object (1B:BEF5) and its water (1B:BE6C).
unsigned Game::load_play_art() {
    unsigned clocks = 17 + 85 + load_tiles(0x0400, 0x0A, 0xA546); // 761C
    clocks += 17 + 85 + load_tiles(0x0200, 0x1D, 0x8A94);         // 762C
    video.loading = 0;
    // 31C3: a special stage (mode 9) has the HUD whatever its act.
    const bool special = flow.mode == 0x09;
    if (!special && flow.zone == 5 && flow.act == 2) {
        // 75D7: no HUD; the art of 0A:9040 at 1D00 and the player's (0A:9700
        // Sonic, 9B80 Tails) at 1C40.
        display.put_art(0x1D00, rom.bytes(0x0A, 0x9040, 0xC0));
        display.put_art(0x1C40, rom.bytes(0x0A, flow.character == 1 ? 0x9700 : 0x9B80, 0xC0));
        clocks += 2 * (0xC0 * 30 + 200);
    } else if (special || flow.act != 2 || flow.zone == 4) {
        if (special || !(flow.zone == 4 && flow.act == 2)) {
            // 31ED: the HUD.
            set_hud_layout(); // 1B:BD12
            show_clock();     // 3035
            show_rings();     // 33BA
            show_lives();     // 3370
            // 780B: the player's face for the lives counter.
            const unsigned face = flow.character == 1 ? 0xA040 : 0x9DC0;
            display.put_art(0x0BC0, rom.bytes(0x0A, face, 0x80));
            clocks += 0x80 * 30 + 200;
        }
        // 3201: the digits (76F6; 1D:BD18 in modes 14 and 19) and 75C5.
        if (flow.mode == 0x19 || flow.mode == 0x14) {
            clocks += 17 + 85 + load_tiles(0x05C0, 0x1D, 0xBD18);
        } else {
            clocks += 17 + 85 + load_tiles(0x05C0, 0x0A, 0xACEA);
        }
        display.put_art(0x0A00, rom.bytes(0x0A, 0x9040, 0xC0));
        clocks += 0xC0 * 30 + 200;
    }
    clear_rings_and_time();           // 32C3
    objects[0].type = flow.character; // D13F: the character (1 Sonic, 2 Tails)
    place_partner();                  // 1B:BEF5
    load_water_level();               // 1B:BE6C
    return clocks;
}

// 1B:BE43: the second special stages' HUD: the rings' digits only
// (entries 59, 60, 62, 63 at Y 18).
void Game::set_special_hud() {
    sprites.table[62] = {0x18, 0x3E, sprites.table[62].tile};
    sprites.table[63] = {0x18, 0x46, sprites.table[63].tile};
    sprites.table[59] = {0x18, 0x30, sprites.table[59].tile};
    sprites.table[60] = {0x18, 0x38, sprites.table[60].tile};
}

// 1B:BD12: the HUD's sprites: rings (entries 55-59), clock (60-63, the
// colon tile 42), lives (51-54, the face tiles 5E/60).
void Game::set_hud_layout() {
    if (flow.mode == 0x11) {
        throw Unrecovered("HUD of mode 11 (1B:BDBA)");
    }
    auto put = [&](unsigned entry, u8 y, u8 x) {
        sprites.table[entry].y = y;
        sprites.table[entry].x = x;
    };
    put(55, 0x18, 0x40);
    put(56, 0x18, 0x48);
    put(57, 0x18, 0x50);
    put(58, 0x18, 0x30);
    put(59, 0x18, 0x38);
    put(60, 0x28, 0x30);
    put(61, 0x28, 0x38);
    put(62, 0x28, 0x40);
    put(63, 0x28, 0x48);
    sprites.table[61].tile = 0x42;
    for (unsigned e = 51; e <= 54; ++e) {
        sprites.table[e].y = 0x38;
    }
    sprites.table[53].x = 0x30;
    sprites.table[54].x = 0x38;
    sprites.table[51].x = 0x40;
    sprites.table[52].x = 0x48;
    sprites.table[53].tile = 0x5E;
    sprites.table[54].tile = 0x60;
}

// 33BA: the rings in the HUD (entries 56, 57 and the hundreds in 55);
// not in act 3 (outside mode 9); the flight stage's two digits in entries
// 62 and 63 (33F3).
void Game::show_rings() {
    const auto rlc = [](u8 v) {
        return u8((v << 1) | (v >> 7));
    };
    const auto rrc3 = [](u8 v) {
        return u8((v >> 3) | (v << 5));
    };
    if (flow.mode != 0x09) {
        if (flow.act == 2) {
            return;
        }
        if (flow.mode == 0x0D) {
            sprites.table[62].tile = u8((rrc3(score.rings_low) & 0x1E) + 0x2E);
            sprites.table[63].tile = u8((rlc(score.rings_low) & 0x1E) + 0x2E);
            return;
        }
    }
    sprites.table[56].tile = u8((rrc3(score.rings_low) & 0x1E) + 0x2E);
    sprites.table[57].tile = u8((rlc(score.rings_low) & 0x1E) + 0x2E);
    sprites.table[55].tile = u8((rlc(score.rings_high) & 0x1E) + 0x2E);
}

// 3370: the lives in the HUD (entries 51, 52; D140 is BCD).
void Game::show_lives() {
    if (flow.mode == 0x11) {
        show_rings();
        return;
    }
    sprites.table[51].tile = u8(((score.lives >> 4) & 0x0F) * 2 + 0x2E);
    sprites.table[52].tile = u8((score.lives & 0x0F) * 2 + 0x2E);
    show_rings();
}

// 1B:BEF5: the act's partner object in slot 1 (table 1B:BF14 by zone * 3
// + act; 0 none).
void Game::place_partner() {
    const u8 type = rom.byte(Table{0x1B, 0xBF14}, flow.zone * 3u + flow.act);
    if (type) {
        objects[1].type = type;
    }
}

// 1B:BE6C: the act's water line (table 1B:BEA7; 0 none); with water in
// zones 0 and 4 slot 2 gets the surface object (type 24, parameter 5).
void Game::load_water_level() {
    act.water_kind = 0; // D3E9
    act.water_level = rom.word(Table{0x1B, 0xBEA7}, (flow.zone * 3u + flow.act) * 2u);
    if (!act.water_level) {
        return;
    }
    if (flow.zone == 4 || flow.zone == 0) {
        objects[2].type = 0x24;
        objects[2].param = 0x05;
    }
}

// 7650: the act's colours fade in from black (table 0E:BF1C); with water
// the underwater colours too (0E:BF5E).
void Game::fade_in_act() {
    const Table p = act_palettes + (flow.zone * 3u + flow.act) * 2u;
    palette.fades[0] = {0x80, rom.byte(p, 0)};
    palette.fades[1] = {0x80, rom.byte(p, 1)};
    if (act.water_level) {
        // 7685: the underwater colours fade in too (0E:BF5E by zone).
        const Table w = Table{0x0E, 0xBF5E} + flow.zone * 2u;
        palette.water_fades[0] = {0x80, rom.byte(w, 0)};
        palette.water_fades[1] = {0x80, rom.byte(w, 1)};
    }
}

} // namespace triple_trouble_gg
