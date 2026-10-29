// Level loading (750A and the routines it calls).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 750A: clears the tile art and loads the act's tiles. The table at 7903 has seven
// bytes per act (zone * 3 + act): bank, VRAM address, compressed block, and
// the address of a list of further blocks (5 bytes each: bank with bit 7 set
// for mirrored tiles, VRAM address, block; FF ends the list).
void Game::load_level_tiles() {
    di();
    clear_vram(); // 76C6
    const auto entry = rom::level_tiles + (flow.zone * 3u + flow.act) * 7u;
    load_tiles(rom.word(entry, 1) / 32, {rom.byte(entry), rom.word(entry, 3)});
    for (Table item{0x01, rom.word(entry, 5)};; item = item + 5) {
        const u8 bank = rom.byte(item);
        if (bank == 0xFF) {
            break;
        }
        load_tiles(rom.word(item, 1) / 32, {u8(bank & 0x1F), rom.word(item, 3)}, bank & 0x80);
    }
}

} // namespace sonic2_gg

namespace sonic2_gg {

// 2206: sets up an act: tiles, HUD sprites, the map header, the layout and
// the first screen, the act header, Sonic, and the clock.
Flow Game::init_act() {
    di();                // 2206
    clear_level_state(); // 223C
    load_level_tiles();  // 750A
    init_hud_sprites();  // 224A
    clear_map_state();   // 53B6
    load_map_header();   // 53C4
    co_await load_map(); // 518A
    load_act_header();   // 22ED
    objects[0].type = 1;
    if (!clock.keep) {
        clock.seconds = 0;
        clock.minutes = 0;
        clock.running = 0xFF;
    }
    co_await next_frame();
}

// 224A: the twelve fixed HUD sprites (table 227B: tile, Y, X) at the end of
// the sprite table. In the final act (zone 6, act 2) the ring counter is
// hidden.
void Game::init_hud_sprites() {
    if (flow.attract_flags) {
        return;
    }
    di();
    clear_sprites();
    spend(17 + 12 * 64); // 229F
    for (unsigned i = 0; i < 12; ++i) {
        auto& slot = sprites.table[hud_first + i];
        slot.y = rom.byte(rom::hud_sprites, i * 3);
        slot.x = rom.byte(rom::hud_sprites, i * 3 + 1);
        slot.tile = rom.byte(rom::hud_sprites, i * 3 + 2);
    }
    if (flow.zone == 6 && flow.act == 2) {
        sprites.table[hud_rings].y = 0xE0;
        sprites.table[hud_rings + 1].y = 0xE0;
    }
    ei(); // 2279
}

// The act's entry in a two-level table: word per zone, then word per act.
Table Game::act_entry(Table table) const {
    const unsigned acts = rom.word(table, flow.zone * 2u);
    return {table.bank, rom.word(table.bank, acts + flow.act * 2u)};
}

// 22ED: no rings; the act header (2338): terrain profile table, animated
// tiles, and the four palette cycle kinds.
void Game::load_act_header() {
    score.rings = 0;
    const auto h = act_entry(rom::act_headers);
    level.terrain_profiles = rom.word(h);
    level.animated_tiles_source = rom.word(h, 2);
    level.animated_tiles_vram = rom.word(h, 4);
    palette.cycle1[0] = rom.byte(h, 6);
    palette.cycle2[0] = rom.byte(h, 7);
    palette.cycle3[0] = rom.byte(h, 8);
    palette.cycle4[0] = rom.byte(h, 9);
}

// 53C4: the map header (table 546A, 22 bytes per act) and the camera limits.
void Game::load_map_header() {
    const auto h = act_entry(rom::map_headers);
    level.map_bank = rom.byte(h);
    level.block_table = rom.word(h, 1);
    level.layout_bank = rom.byte(h, 3);
    level.layout_source = rom.word(h, 4);
    level.attribute_table = rom.word(h, 8);
    camera.min_x = rom.word(h, 12);
    camera.min_y = rom.word(h, 14);
    camera.max_x = rom.word(h, 16);
    camera.max_y = rom.word(h, 18);
    level.row_table = rom.word(h, 20);
    camera.offset_x = 0x80;
    camera.offset_y = 0x78;
    reset_camera_window(); // 5D26
}

// 518A: decompresses the layout into level_map (FD n v is a run of n copies
// of v, n = 0 ends; anything else is a literal) until the map RAM is full;
// then places the camera and draws the first screen one row per frame, 29
// rows down from the start position.
Flow Game::load_map() {
    di();
    unsigned clocks = 4 + 14 + 19 + 53 + 19 + 19 + 10 + 11 + 14;
    unsigned in = level.layout_source;
    std::size_t out = 1; // C001: the map starts one byte in
    while (out < level.map.size()) {
        const u8 v = rom.byte(level.layout_bank, in);
        clocks += 4 + 7 + 7 + 7 + 19 + 7 + 10;
        if (v == 0xFD) {
            const unsigned n = rom.byte(level.layout_bank, in + 1);
            clocks += 19 + 4 + 10;
            if (!n) {
                break;
            }
            const u8 fill = rom.byte(level.layout_bank, in + 2);
            clocks += 4;
            bool full = false;
            for (unsigned i = 0; i < n; ++i) {
                clocks += 4 + 7 + 7 + 7;
                if (out >= level.map.size()) {
                    full = true;
                    break;
                }
                level.map[out++] = fill;
                clocks += 19 + 7 + 6 + 13;
            }
            if (full) {
                break;
            }
            clocks += 10 + 15 + 10;
            in += 3;
        } else {
            level.map[out++] = v;
            ++in;
            clocks += 19 + 7 + 6 + 10 + 10;
        }
    }
    spend(clocks);
    ei();                  // 51D8: the frame interrupt held off since 750A runs here
    spend(17 + 393);       // 5245
    load_start_position(); // 5245
    camera.x = level.start_camera_x;
    camera.target_x = level.start_camera_x;
    camera.y = level.start_camera_y;
    camera.target_y = level.start_camera_y;
    display.level_shown = true; // the rows below draw the act's map
    for (int i = 0; i < 29; ++i) {
        di(); // 51F0
        camera.target_y = camera.y + 8u;
        spend(11 + 16 + 10 + 11 + 16 + 13 + 53 + 17 + 3969 + 23 + 17);
        camera.map_flags |=
            map_flag::update_pending; // 57A5 prepares a row, shown as the camera moves
        co_await wait_frame();
    }
    camera.offset_x = 0x80;
    camera.offset_y = 0x78;
    reset_camera_window();
    camera.target_x = level.start_camera_x + 8u;
    camera.x = camera.target_x;
    camera.target_y = level.start_camera_y;
    camera.y = level.start_camera_y;
    unsigned y = (level.start_camera_y + 0x10u) & 0xFFFF;
    while (y >= 0xE0) {
        y -= 0xE0;
    }
    camera.scroll_y = u8(y);
    ei(); // 5243
}

// 5245: the act's start: camera position and Sonic's position (table 5286).
void Game::load_start_position() {
    const auto h = act_entry(rom::start_positions);
    level.start_camera_x = rom.word(h);
    level.start_camera_y = rom.word(h, 2);
    objects[0].x = rom.word(h, 4);
    objects[0].y = rom.word(h, 6);
}

// 2643: the lives digit in the HUD (at most 9 shown).
void Game::show_lives() {
    if (flow.attract_flags) {
        return;
    }
    u8 a = score.lives;
    if (a >= 9) {
        a = 9;
    }
    sprites.table[hud_lives].tile = u8((u8((a << 1) | (a >> 7)) & 0x1E) + 0x10);
}

// 2672: the two ring digits, except in the final act.
void Game::show_rings() {
    if (flow.attract_flags) {
        return;
    }
    if (flow.zone == 6 && flow.act == 2) {
        return;
    }
    const u8 r = score.rings;
    sprites.table[hud_rings].tile = u8((u8((r >> 3) | (r << 5)) & 0x1E) + 0x10);
    sprites.table[hud_rings + 1].tile = u8((u8((r << 1) | (r >> 7)) & 0x1E) + 0x10);
}

// 265A: one ring more; a hundred give an extra life.
void Game::add_ring() {
    unsigned r = score.rings + 1u;
    if ((r & 0x0F) > 9) {
        r += 6;
    }
    if (r > 0x99) {
        r += 0x60;
    }
    score.rings = u8(r);
    if (!score.rings) {
        ++score.lives;
        show_lives();
        sound_.request(sound::extra_life);
    }
    show_rings();
}

// 10B8: the act's music (table 10CF).
void Game::play_act_music() {
    sound_.request(rom.byte(rom::act_music, flow.zone * 3u + flow.act));
}

// 7578: fade in to the act's palettes (table 7BA1: background, sprites).
void Game::fade_in_act() {
    const unsigned e = (flow.zone * 3u + flow.act) * 2u;
    palette.fades[1] = rom.byte(rom::act_palettes, e);
    palette.fades[3] = rom.byte(rom::act_palettes, e + 1);
    palette.fades[0] = 0x80;
    palette.fades[2] = 0x80;
}

} // namespace sonic2_gg
