// The title (mode 2, 0FED): the copyright screen, then the title with
// Knuckles, until Start (the character select) or a timeout (the demos).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 77A8: the copyright screen's art (10:839E at tile 0) and its line of
// text (10:84B0, 10 entries at 3AD6).
constexpr Table copyright_tiles{0x10, 0x839E};
constexpr Table copyright_text{0x10, 0x84B0};
// 7727: the title's art (1D:9F14 at 1400, 16:AE14 at 3720, 1F:AE7C
// mirrored at 0280, 04:B136 at 0200) and its map (10:AC72, 18 rows of 20).
constexpr Table title_map{0x10, 0xAC72};
// 0E:B6FC palettes of both screens.
constexpr u8 title_background_palette = 0x05, title_sprite_palette = 0x06;
constexpr u8 title_song = 0x86;
} // namespace

Flow Game::title() {
    flow.frame_work = 0;
    clear_game_state();      // 327E
    co_await clear_screen(); // 273B
    fill_white();            // 340C
    // 77A8
    clear_vram(); // 7BCF
    di();
    unsigned clocks = 211242 + 17 + 53 + 85;
    clocks += load_tiles(0x0000, copyright_tiles.bank, copyright_tiles.address);
    clocks += 53 + 30 + draw_rect(0x3AD6, copyright_text.bank, copyright_text.address, 0x01, 0x0A);
    co_await busy(clocks);
    ei(); // 2C73
    set_fades(2, 0x90, title_background_palette, 0x90, title_sprite_palette);
    co_await wait_frames(0x1E);
    co_await wait_frames_or_button(0x3C);
    set_fades(2, 0x50, palette.fades[0].palette, 0x50, palette.fades[1].palette);
    co_await wait_frames(0x3C);
    co_await title_screen();
}

// 1043: the title screen (also where Start in a demo leads).
Flow Game::title_screen() {
    flow.frame_work = 0;
    flow.zone = 0;
    flow.act = 0;
    flow.restart_point = 0;
    flow.special_entry = 0;
    clear_game_state();           // 327E
    co_await clear_screen();      // 273B
    fill_white();                 // 340C
    co_await load_title_screen(); // 7727
    set_fades(1, 0x90, title_background_palette, 0x90, title_sprite_palette);
    sound_.request(title_song, 2);
    co_await wait_frames(0x3C);
    flow.frame_work = frame_work::build_sprites;
    flow.next_mode_hint = 0; // D168: the selection (0-2)
    // 1091: about 11 seconds; C001 counts them down as well.
    unsigned left = 0x0294;
    flow.title_ticks = std::uint16_t(left);
    level_map.holds_title_count = true;
    bool started = false;
    for (;;) {
        --flow.title_ticks;
        co_await next_frame();
        co_await update_player();          // 3431
        co_await update_objects();         // 4F3F
        co_await run_map_animations();     // 228E
        if (animation.animators[0].type) { // the title is up (D490, B610)
            if (input.pressed & 0x80) {
                started = true;
                break;
            }
            title_select(left); // 1110
        }
        if (!--left) {
            break;
        }
    }
    if (started) {
        // 10C4: Start: the character select (18), or with selection 2 mode 19.
        flow.mode = 0x18;
        stop_music(); // 2288
        if (flow.next_mode_hint == 2) {
            flow.mode = 0x19;
        }
    } else {
        flow.mode = 0x03; // 10DA: the demos
    }
    // 10ED
    flow.zone = 0;
    set_fades(3, 0x50, palette.fades[0].palette, 0x50, palette.fades[1].palette);
    co_await wait_frames(0x78);
}

// 7727: the title's art, Knuckles (slot 8, type 35: state 0A in mode 2)
// and the title's emblem object (slot 10, type 10), the map.
Flow Game::load_title_screen() {
    di();
    unsigned clocks = 17 + 4 + 53 + 85;
    clocks += load_tiles(0x1400, 0x1D, 0x9F14);
    clocks += 4 + 53 + 85 + load_tiles(0x3720, 0x16, 0xAE14);
    clocks += 4 + 53 + 85 + load_tiles(0x0280, 0x1F, 0xAE7C, true);
    clocks += 4 + 53 + 85 + load_tiles(0x0200, 0x04, 0xB136);
    objects[10].type = 0x10;
    objects[10].tile_base = 0x10;
    objects[8].type = 0x35;
    clocks += 13 + 13 + 13 + 4 + 53 + 30 +
              draw_rect(0x38CC, title_map.bank, title_map.address, 0x12, 0x14);
    co_await busy(clocks);
    ei(); // 2C73
}

// 1110: Up and Down change the selection (D168, 0-2, wrapping; sound C0)
// and, when little time is left, give the title another 10 seconds; the
// selection's text is copied in (bank 17, BFBB). Not while a name table
// animation runs.
void Game::title_select(unsigned& left) {
    if (map_animations.records[0].active) {
        return;
    }
    const u8 pressed = input.pressed & 0x03;
    if (pressed) {
        u8& selection = flow.next_mode_hint;
        bool changed = false;
        if (pressed & 0x01) {
            sound_.request(0xC0, 1);
            if (selection == 0) {
                selection = 2;
                changed = true;
            } else {
                --selection;
            }
        } else if (pressed & 0x02) {
            sound_.request(0xC0, 1);
            if (++selection >= 3) {
                selection = 0;
                changed = true;
            }
        }
        if (changed && left <= 0xB4) {
            left = 0x0258; // 1151
        }
    }
    // 17:BFBB: the selection's text art (17:BB3B, BCBB) at 3720.
    if (flow.next_mode_hint == 1) {
        display.put_art(0x3720, rom.bytes(0x17, 0xBB3B, 0x180));
    } else if (flow.next_mode_hint == 2) {
        display.put_art(0x3720, rom.bytes(0x17, 0xBCBB, 0x180));
    }
}

} // namespace triple_trouble_gg
