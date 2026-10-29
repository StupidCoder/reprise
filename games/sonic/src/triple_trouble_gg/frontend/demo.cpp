// The demos (mode 3, 1167) and the act's main loop (1F2E).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 2519: per demo the character, zone, act, and the bank and address of its
// recorded input (two bytes per frame: held, newly pressed).
constexpr Table demos{0x00, 0x2519};
} // namespace

// 1167: after a fade the next demo plays (with 1 life shown); Start ends it
// and returns to the title screen, otherwise the SEGA logo follows.
Flow Game::demo_mode() {
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    palette.fade_period = 2;
    co_await wait_frames(0x3C);
    score.lives = 0x01;
    co_await play_demo(); // 24A1
    act.water_level = 0;
    stop_music(); // 2288
    if (!flow.demo_aborted) {
        set_fades(2, 0x40, palette.fades[0].palette, 0x40, palette.fades[1].palette);
        co_await wait_frames(0x78);
        flow.mode = mode::sega_logo;
        co_return;
    }
    set_fades(2, 0x50, palette.fades[0].palette, 0x50, palette.fades[1].palette);
    co_await wait_frames(0x78);
    flow.mode = mode::title;
    co_await title_screen(); // JP 1043
}

// 24A1: demo D160 (0 or 1): its act with the recorded input for 0348 frames
// (or until Start).
Flow Game::play_demo() {
    flow.demo_aborted = 0;
    const Table d = demos + (flow.demo & 0x7F) * 6u;
    flow.character = rom.byte(d, 0); // D13F: the character
    flow.zone = rom.byte(d, 1);
    flow.act = rom.byte(d, 2);
    flow.demo_bank = rom.byte(d, 3);
    const std::uint16_t input = rom.word(d, 4);
    flow.demo_frames = 0x0348;
    flow.demo |= 0x80;
    flow.restart_point = 0;
    flow.special_entry = 0;
    co_await start_act(); // 1327
    flow.demo_input = input;
    flow.frame_work = frame_work::build_sprites;
    palette.fade_period = 3;
    co_await act_loop();          // 1F2E
    act.palette_cycle_off = 0xFF; // 21D6
    flow.demo_input = 0;
    flow.demo &= 0x7F;
    flow.demo = u8((flow.demo + 1) & 0x7F);
    if (flow.demo >= 2) {
        flow.demo = 0;
    }
}

// 1327: an act set up for play: the screen cleared, the act loaded, its
// music, its colours faded in (not zones 8 and 9), the camera following.
Flow Game::start_act() {
    input.disabled = 0;
    video.line_split = 0;
    display_off();             // 26E4
    co_await clear_screen();   // 273B
    clear_map_state();         // 428B
    co_await load_act();       // 3167
    co_await play_act_music(); // 21DC
    if (flow.zone != 9 && flow.zone != 8) {
        fade_in_act(); // 7650
    }
    di();
    enable_map(); // 4CC3
    display_on(); // 26D6
}

// 1F2E: one frame of play per pass: the camera, the player and partner,
// the objects, streamed art, the power-up music's end, the placements
// (every 8 frames). It ends when bits 2, 4 or 5 of D144 are set, or a
// demo ends.
Flow Game::act_loop(bool resumed) {
    bool banded = false;
    for (;;) {
        // The variant is chosen before the frame: with the water line armed
        // (by the last frame interrupt) the program builds the sprites (2069).
        // The band effect's pass comes back to that variant directly (20C4).
        // (A quick save keeps the choice: loading runs the interrupt first.)
        const bool water = resumed ? act_loop_water_ : banded || bool(video.water_line_mode);
        if (resumed) {
            // A quick save's program starts here, past the wait it was saved at.
            resumed = false;
            video.frame_done = 0;
            ++frame_waits;
        } else {
            at_act_loop_wait_ = true;
            act_loop_water_ = water;
            co_await next_frame();
            at_act_loop_wait_ = false;
        }
        banded = false;
        co_await check_pause(); // 20E2
        if (!(camera.map_flags & map_flag::update_pending)) {
            if (water) {
                if (!act.line_effect) {
                    update_map(); // 3F81
                }
            } else if (act.variant != 0x05) {
                update_map(); // 3F81
            }
            co_await update_player();  // 3431
            co_await update_partner(); // 34B9
            co_await update_objects(); // 4F3F
            if (water) {
                build_sprites();    // 2A14
                track_player_art(); // 0B01
                if (video.water_line_mode == 0xFE) {
                    // 20B8: with the band effect no more this frame: the
                    // partner's art, streamed art, the music timer, the
                    // placements and the act's events wait.
                    if (flow.frame_work & 0x34) {
                        co_return;
                    }
                    banded = true;
                    continue;
                }
                track_partner_art(); // 0B2E
            }
            stream_object_art();           // 7C15
            co_await update_music_timer(); // 20C7
            if (!water && act.extra_loader) {
                // 1F63: after a boss: the line interrupt off (D130), the
                // common art again (797B: 1F:AE7C at tile 10, with frame
                // waits) and 0A:BD9A at tile 9C (79A2).
                video.line_split = 0xFF;
                video.loading = 0xFF;
                co_await load_tiles_slowly(0x0200, 0x1F, 0xAE7C);
                video.loading = 0;
                load_tiles(0x1380, 0x0A, 0xBD9A);
                act.extra_loader = 0;
            }
            if (++flow.placement_ticks >= 8) {
                flow.placement_ticks = 0;
                spawn_placements(); // 1C:8000
            }
        }
        if (act.loop_exit) {
            co_return; // DBCD
        }
        co_await act_events(); // 1FC6
        if (flow.frame_work & 0x34) {
            co_return;
        }
        if (!(flow.demo & 0x80)) {
            continue;
        }
        if (buttons_ & button::start) { // 1FA2: the real controller's Start
            flow.demo_aborted = 0xFF;
            co_return;
        }
        if (--flow.demo_frames == 0) {
            co_return;
        }
    }
}

// 20E2: Start pauses the act (not in mode 11 / with DBCB, D3EB).
Flow Game::check_pause() {
    if (act.no_pause) {
        co_return;
    }
    if (!act.object_mode && flow.mode == 0x11) {
        throw Unrecovered("pause in mode 11 (212D)");
    }
    if (!(input.pressed & 0x80)) {
        co_return;
    }
    // 20FB: the sound held, no level services, the clock stopped, until
    // Start again.
    sound_.set_pause(0x80);
    flow.frame_work |= frame_work::no_level_services;
    clock.running = 0; // 21AA
    do {
        co_await next_frame(); // 084C
    } while (!(input.pressed & 0x80));
    sound_.set_pause(0);
    if (!act.variant) { // 21AF
        clock.running = 0xFF;
    }
    flow.frame_work &= u8(~frame_work::no_level_services);
    co_await next_frame();
}

// 20C7: when the power-up's song ends (D3EE frames), the act's song again
// (or song 94 in player state 29).
Flow Game::update_music_timer() {
    if (!act.music_ticks) {
        co_return;
    }
    if (--act.music_ticks) {
        co_return;
    }
    if (objects[0].state != 0x29) {
        co_await play_act_music(); // 21DC
        co_return;
    }
    sound_.request(0x94, 2);
}

// 1FC6: D3FE events (Atomic Destroyer act 3's drop into the boss).
Flow Game::act_events() {
    if (!act.event) {
        co_return;
    }
    if (flow.zone == 5 && flow.act == 2) {
        co_await destroyer_drop(); // 1FDB
    }
    act.event = 0; // 2064: elsewhere it only paused the tile animations for a frame
}

// 1FDB: in Atomic Destroyer act 3, the event (block 95) high up (Y below
// 100): sound D3, a fade to white; the map of "act 4" (its fourth entry:
// the last section) is loaded and shown at its start with its art (7B6E),
// then faded in as act 3 again. The camera's lowest point is 218 either
// way.
Flow Game::destroyer_drop() {
    camera.max_y = 0x0218;
    camera.act_max_y = 0x0218;
    if (objects[0].y >= 0x0100) {
        co_return;
    }
    sound_.request(0xD3, 1);
    palette.fade_period = 0;
    palette.fades[0].mode = 0x50;
    palette.fades[1].mode = 0x50;
    co_await wait_frames(0x1E); // 21BA
    flow.frame_work = 0;
    flow.act = 0x03;
    video.busy = 0xFF;
    display_off();           // 26E4
    co_await clear_screen(); // 273B
    clear_map_state();       // 428B
    load_act_header();       // 4299
    load_start_position();   // 41C0 (4160)
    co_await draw_first_screen();
    // 7B6E: 0A:BE3C at tile E1, 0A:9040 at tile 50 (75C5), the list 1A:B8C8.
    display.put_art(0x1C20, rom.bytes(0x0A, 0xBE3C, 0x40));
    display.put_art(0x0A00, rom.bytes(0x0A, 0x9040, 0xC0));
    di();
    co_await load_tile_list_slowly(Table{0x1A, 0xB8C8}); // 7A58
    di();
    enable_map(); // 4CC3
    display_on(); // 26D6
    flow.frame_work = 0x02;
    flow.act = 0x02;
    palette.fade_period = 2;
    palette.fades[0] = {0x90, 0x25};
    palette.fades[1] = {0x90, 0x14};
}

// 34B9: the partner slot (1): the rival in his scenes (type 8, scripts in
// bank 16) or another object.
Flow Game::update_partner() {
    auto& p = objects[1];
    if (!p.type) {
        co_return;
    }
    if (p.type != 0x08) {
        co_await update_object(p); // 4FC0
        co_return;
    }
    // 34C7: the rival (rival.cpp): his script and routine (bank 16), then
    // whether he is seen (5456).
    video.busy = 0xFF;
    script_bank_ = 0x16;
    co_await animate_object(p); // 5A60
    script_bank_ = 0;
    video.busy = 0xFF;
    co_await run_routine(p, p.routine); // 50D6
    video.busy = 0;
    if (p.state) {
        check_visibility(p); // 5456
    }
}

} // namespace triple_trouble_gg
