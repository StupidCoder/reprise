// The main loop (065C) and the per-frame game update (09BB).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 065C: waits for each frame, then runs the state the bits of D293 select.
Flow Game::main_loop() {
    for (;;) {
        ++main_loop_iterations;
        if (main_loop_hook) {
            main_loop_hook();
        }
        in_main_wait_ = true;
        co_await wait_frame();
        in_main_wait_ = false;
        const u8 f = flow.game_flags;
        if (f & game_flag::scene) {
            co_await ending_state(); // 0686
        } else if (!(f & game_flag::playing)) {
            co_await game_over(); // 0703
        } else if (f & game_flag::act_complete) {
            co_await act_complete(); // 07F4
        } else if (f & game_flag::zone_complete) {
            co_await zone_complete(); // 086F
        } else if (f & game_flag::life_lost) {
            co_await lose_life(); // 08FE
        } else if (f & game_flag::act_start) {
            co_await start_act(); // 0953
        } else if (f & game_flag::paused) {
            co_await pause(); // 099D
        } else {
            game_frame();  // 09BB
            check_pause(); // 0A3E
        }
    }
}

// 09BB: one frame of play. Nothing moves while the previous frame's map
// update is still waiting for the frame interrupt. While the water line
// interrupt is in use (D131), the sprite table is built here instead of in
// the interrupt, and D47E keeps any interrupt in between to colours only.
void Game::game_frame() {
    if (camera.map_flags & map_flag::update_pending) {
        return;
    }
    update_map(); // 49EF
    if (!video.line_interrupt) {
        update_sonic();   // 2F4F
        update_objects(); // 5DA4
    } else {
        video.irq_colors_only = 0xFF;
        update_sonic();
        video.irq_colors_only = 0;
        update_objects();
        video.irq_colors_only = 0xFF;
        build_sprites();      // 1821
        check_player_tiles(); // 12A8
        video.irq_colors_only = 0;
    }
    stream_object_tiles(); // 7743
    update_rain();         // 681A
    if (!(flow.frame_parity & 0x0B)) {
        spawn_objects(); // bank 1E, 8000
    }
    ++flow.frame_parity;
}

// 0A3E: Start pauses the game during play (not while fading).
void Game::check_pause() {
    if (palette.fades[0]) {
        return;
    }
    if ((flow.game_flags & u8(~game_flag::paused)) != game_flag::playing) {
        return;
    }
    if (!(input.pressed[0] & 0x80)) {
        return;
    }
    sound_.state.pause = 0x80;
    flow.game_flags |= game_flag::paused;
}

// 099D: paused until Start; the clock stops and restarts.
Flow Game::pause() {
    clock.running = 0;
    do {
        co_await next_frame();
    } while (!(input.pressed[0] & 0x80));
    sound_.state.pause = 0;
    clock.running = 0xFF;
    flow.game_flags &= u8(~game_flag::paused);
    co_await next_frame();
}

// 08FE: a life lost: Sonic falls off the screen (only he is updated), then
// a fade; with lives left the act restarts, otherwise the game is over.
Flow Game::lose_life() {
    clock.running = 0; // 0A5C
    --score.lives;
    show_lives();
    do {
        co_await wait_frame();
        update_sonic(); // 2F4F
        if (sonic().next_state != 0x1F) {
            sonic().next_state = 0x1F;
        }
    } while (hi(sonic().screen_y) != 0x02);
    co_await wait_frames(120);
    sound_.request(sound::stop);
    stop_palette_cycles(); // 7C26
    start_fade_out();
    co_await wait_frames(60);
    if (score.lives) {
        flow.game_flags = u8((flow.game_flags & ~game_flag::life_lost) | game_flag::act_start);
        co_return;
    }
    flow.game_flags &= u8(~game_flag::playing);
}

// 7C26: palette cycles 1 and 2 off, no animated tiles.
void Game::stop_palette_cycles() {
    palette.cycle1[0] = 0;
    palette.cycle2[0] = 0;
    level.animated_tiles_vram = 0;
}

// 0953: starting an act: title card, fade, set-up, then play.
Flow Game::start_act() {
    co_await next_frame(); // 17EE
    clear_screen();
    // Shell option (direct_start): the screen is still black and silent, so
    // neither the card nor the fade out (to black) is needed.
    if (!direct_start) {
        co_await title_card();      // 269D
        co_await hold_title_card(); // 2970
        start_fade_out();
        fade_music();
        co_await wait_frames(42);
    }
    co_await init_act();   // 2206
    direct_start = false;  // from here on, loading takes its original time
    show_lives();          // 2643
    show_rings();          // 2672
    update_timer_digits(); // 212E
    play_act_music();      // 10B8
    fade_in_act();         // 7578
    enable_map();          // 4A0F
    if (flow.zone == 2 && flow.act == 1) {
        // Aqua Lake act 2 starts with its rising water (object 0C in slot 8).
        objects[8].type = object_type::rising_water;
        water.rise_y = 0x0100;
    }
    flow.game_flags &= u8(~game_flag::act_start);
}

// 269D: the title card (or, between acts, the score tally): tiles, colours
// and the three lines of text revealed from the right, one column per frame.
Flow Game::title_card() {
    di();              // 269D
    clear_map_state(); // 53B6
    spend(30 + fill_clocks(0x380));
    display.map.fill(0x1170);
    display.level_shown = false;
    co_await load_card_tiles(); // 75B6
    if (flow.game_flags & game_flag::act_start) {
        sound_.request(sound::act_start);
    }
    ei(); // 26BC
    palette.fades[0] = 0x80;
    palette.fades[1] = 0x03;
    palette.fades[2] = 0x80;
    palette.fades[3] = u8(flow.zone + 0x1D);
    co_await wait_frames(12);
    // 26E5: the zone name (or "  -  ACT CLEAR" style text between acts).
    unsigned text = rom.word(rom::zone_names, flow.zone * 2u) + 0x28;
    if (!(flow.game_flags & game_flag::act_start)) {
        text = rom::card_act_clear_text;
    }
    co_await reveal_text({6, 4}, text, 0x28, 0x14, false);
    // 271F: the act number, five rows tall.
    co_await reveal_text({31, 7}, rom.word(rom::act_numbers, flow.act * 2u), 0x1C, 0x0E, true);
    // 274A: the bottom line.
    text = (flow.game_flags & (game_flag::act_complete | game_flag::zone_complete))
               ? rom::card_passed_text
               : rom::card_zone_text;
    co_await reveal_text({1, 7}, text, 0x28, 0x14, false);
}

// 2778 / 27B9: each frame draws one more column of a two-row (or five-row)
// text block whose rows are `stride` bytes apart, so the text slides in. The
// two-row form grows from the right end; the five-row form moves the
// destination left instead.
Flow Game::reveal_text(Display::Cell at, unsigned text, unsigned stride, int steps, bool tall) {
    int columns = 1;
    for (int i = 0; i < steps; ++i) {
        co_await next_frame();
        const int rows = tall ? 5 : 2;
        for (int r = 0; r < rows; ++r) {
            draw_rect({at.column, at.row + r},
                      Table{0x00, std::uint16_t(text + unsigned(r) * stride)}, 1, columns);
        }
        ++columns;
        if (tall) {
            --at.column;
        } else {
            text -= 2;
        }
    }
}

// 75B6: the card's letters and frame (bank 9 and 7) plus the zone's own
// tiles (770E); for an act start, the zone picture (bank 19, table 762F);
// otherwise the score tally texts and numbers (7687).
Flow Game::load_card_tiles() {
    di(); // 75B6
    load_tiles(256, rom::card_letters);
    load_tiles(385, rom::card_frame);
    load_tiles(390, rom::card_corners);
    // 770E
    const auto z = rom::zone_card_tiles + flow.zone * 3u;
    load_tiles(393, {rom.byte(z), rom.word(z, 1)});
    load_tiles(128, rom::card_digits);
    if (flow.game_flags & game_flag::act_start) {
        const auto e = rom::zone_pictures + flow.zone * 4u;
        load_tiles(0, {0x19, rom.word(e, 2)});
        draw_rect({8, 13}, {0x19, rom.word(e)}, 8, 16);
        co_return;
    }
    // 7687
    di();
    draw_rect({8, 14}, rom::tally_rings_label, 2, 5);
    draw_rect({8, 16}, rom::tally_bonus_label, 2, 5);
    draw_rect({8, 18}, rom::tally_score_label, 2, 5);
    co_await next_frame(); // 76AC EI; HALT
    di();                  // 76AE
    draw_rect({20, 14}, rom::tally_sonic, 6, 4);
    show_ring_tally();  // 1DAB
    show_bonus_tally(); // 1DBC (continues into the score)
    show_score_tally(); // 1DCB
    ei();               // 76C4
}

// 2970: up to 120 frames (a button skips) with the streaks moving across.
Flow Game::hold_title_card() {
    // 2993: sixteen streak sprites.
    flow.title_card_ticks = 0x0078;
    for (int i = 0; i < 8; ++i) {
        sprites.table[i].y = 0x18;
        sprites.table[8 + i].y = 0x30;
    }
    for (int b = 16, i = 0; b >= 1; --b, ++i) {
        sprites.table[i].x = rom.byte(rom::card_streaks_x, unsigned(i));
        sprites.table[i].tile = ((b - 1) & 4) ? 0x82 : 0x80;
    }
    for (;;) {
        sprites.dirty = 0xFF;
        move_card_streaks(); // 29D7
        flash_ring_colors(); // 7E0F
        co_await next_frame();
        if (input.pressed[0] & 0x30) {
            co_return;
        }
        if (--flow.title_card_ticks == 0) {
            co_return;
        }
    }
}

// 29D7: streaks move left (tile 82 at double speed) and wrap to X = D0.
void Game::move_card_streaks() {
    for (int i = 0; i < 16; ++i) {
        u8& x = sprites.table[i].x;
        if (!x) {
            continue;
        }
        const u8 tile = sprites.table[i].tile;
        --x;
        if (tile == 0x82) {
            --x;
        }
        const u8 limit = sprites.table[i].y == 0x18 ? 0x10 : 0x50;
        if (x < limit) {
            x = 0xD0;
        }
    }
}

} // namespace sonic2_gg
