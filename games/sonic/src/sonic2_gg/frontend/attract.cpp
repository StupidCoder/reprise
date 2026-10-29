// Boot (0000, 0457), the attract sequence (2472) and the SEGA logo (28F4).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
// Port 00 bit 6 on the Game Gear: set on export (non-Japanese) consoles. The
// logo uses a different palette on Japanese ones (28F4).
constexpr bool export_console = true;
} // namespace

// EI; HALT, `count` times.
Flow Game::wait_frames(int count) {
    for (int i = 0; i < count; ++i) {
        co_await next_frame();
    }
}

// 05C5: waits until the frame interrupt has run since the last call (it may
// already have), and allows full interrupts again.
Flow Game::wait_frame() {
    ei(); // 05C5
    video.irq_colors_only = 0;
    if (wait_hook) {
        wait_hook(); // the test harness's snapshot point (05CA)
    }
    while (!video.frame_done) {
        co_await next_frame();
    }
    video.frame_done = 0;
}

// 0000 and 0457: power-on initialisation, then the attract sequence until
// Start begins a game, then the main loop.
Flow Game::boot() {
    // 0000: work RAM C001-DFEF cleared (C000 keeps its power-on value).
    sound_init();               // bank 2, 441E
    init_display();             // 132E
    clear_cram();               // 7700
    clear_vram();               // 76C6
    load_level_tiles();         // 750A: the first act's tiles, as the cartridge does
    input.controller2_code = 0; // 04A9: port DD; no second controller
    display_on();               // 13C2
    interrupts_ = true;
    co_await title_sequence(); // 0484
    co_await main_loop();      // 065C
}

// 0484: the attract sequence until Start begins a game (also after a game
// over); then the main loop starts the first act.
Flow Game::title_sequence() {
    flow.attract_flags |= attract_flag::logo;
    palette.fade_speed = 1;
    if (start_zone_ >= 0) {
        // As after the zone select (0A61): the chosen act, Start pressed.
        flow.zone = u8(start_zone_);
        flow.act = u8(start_act_);
        flow.zone_select_used = 0xFF;
        flow.attract_flags = attract_flag::start;
        input.controller2_code = 0;
        start_zone_ = -1;
    }
    co_await attract();
    // 0491: a game starts.
    co_await next_frame(); // 17EE
    clear_screen();
    clear_level_state();
    flow.attract_flags = 0;
    new_game(); // 22B7
    if (direct_start) {
        // Shell option: the launcher's act was passed like a zone select
        // choice. The original never clears that flag (only the zone select
        // does), so after a game over the next game would start in the intro's
        // zone 9, where the original hangs in its tile decoder.
        flow.zone_select_used = 0;
    }
    co_await next_frame();
    flow.game_flags = (game_flag::playing | game_flag::act_start);
    if (ending_start) {
        // As 08C5 leaves it after the last zone.
        ending_start = false;
        if (flow.act == 1) {
            score.emeralds = 0x3F;
        }
        flow.game_flags = (game_flag::playing | game_flag::scene);
    }
}

// 2472: the attract sequence, driven by the bits of D292. It returns when a
// game is to start.
Flow Game::attract() {
    for (;;) {
        const std::uint8_t f = flow.attract_flags;
        if (f & attract_flag::start) {
            // 253D: Start pressed. Controller 2 code B6 opens the zone select.
            if (input.controller2_code == 0xB6) {
                flow.zone_select_used = 0;
                co_await zone_select(); // 0A61
                start_fade_out();
                co_await wait_frames(42);
            }
            flow.attract_flags = 0;
            flow.game_flags = 0;
            co_return;
        }
        if (f & attract_flag::logo) {
            // 248B: the SEGA logo.
            palette.fade_speed = 1;
            clear_map_state(); // 53B6
            clear_level_state();
            co_await wait_frames(30);
            co_await sega_logo();
            flow.attract_flags =
                u8((flow.attract_flags & ~attract_flag::logo) | attract_flag::intro);
        } else if (f & attract_flag::intro) {
            // 24A9: the intro scene.
            co_await intro();    // 0ED8
            objects[8].type = 0; // D700
            start_fade_out();
            fade_music(); // 10E7
            co_await wait_frames(42);
            flow.attract_flags =
                u8((flow.attract_flags & ~(attract_flag::start | attract_flag::intro)) |
                   attract_flag::title);
        } else if (f & attract_flag::title) {
            // 24C9: the title screen.
            co_await title_screen(); // 0FD4
            if (input.controller2_code) {
                co_await wait_frames(60);
            }
            start_fade_out();
            fade_music();
            co_await wait_frames(42);
            flow.attract_flags =
                u8((flow.attract_flags & ~attract_flag::title) | attract_flag::demo);
        } else if (f & attract_flag::demo) {
            // 24EE: a recorded demo.
            co_await demo();
        } else {
            co_await next_frame(); // 2488: loops (not expected)
        }
    }
}

// 28F4: the SEGA logo with its sampled voice.
Flow Game::sega_logo() {
    co_await next_frame(); // 17EE begins with EI; HALT
    clear_screen();
    load_tiles(0, rom::sega_logo_tiles);
    draw_rect({5, 9}, rom::sega_logo_map, 6, 20);
    palette.fades[0] = 0x80;
    palette.fades[1] = export_console ? 0x30 : 0x2F;
    palette.fades[2] = 0x80;
    palette.fades[3] = 0x04;
    co_await wait_frames(60);
    // 294B: the voice plays with interrupts off (bank 3, 8000); everything
    // else stops meanwhile.
    co_await play_voice();
    co_await wait_frame();
    co_await wait_frames(120);
    start_fade_out();
    co_await wait_frames(42);
    co_await next_frame(); // 17EE
    clear_screen();
    flow.attract_flags &= u8(~attract_flag::start);
}

} // namespace sonic2_gg
