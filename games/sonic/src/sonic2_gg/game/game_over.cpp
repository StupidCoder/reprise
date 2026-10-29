// The end of the last life: the continue screen (0766) while continues are
// left, otherwise "GAME OVER" (0703) and back to the attract sequence.

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

namespace {
// The continues count (D2BD) keeps the choice in bit 7: set when a
// continue was taken on the continue screen.
constexpr u8 continue_taken = 0x80;
} // namespace

// 0703
Flow Game::game_over() {
    sound_.state.current = 0; // the driver stops everything (DD03)
    co_await next_frame();
    clear_level_state();   // 223C
    clear_map_state();     // 53B6
    co_await next_frame(); // 17EE
    clear_screen();
    co_await continue_screen(); // 0766
    if (score.continues & continue_taken) {
        // 074B: three lives, no score, and the act starts again.
        score.lives = 3;
        score.points[0] = 0;
        score.points[1] = 0;
        score.points[2] = 0;
        flow.game_flags = u8((flow.game_flags & ~game_flag::life_lost) | game_flag::playing |
                             game_flag::act_start);
        co_return;
    }
    sound_.request(sound::game_over);
    show_game_over(); // 284B
    co_await wait_frames(120);
    // About a minute, or until Start.
    for (unsigned ticks = 0x0D98; ticks; --ticks) {
        co_await wait_frame();
        if (input.held[0] & 0x80) {
            break;
        }
    }
    start_fade_out(); // 12FC
    fade_music();     // 10E7
    co_await wait_frames(60);
    co_await title_sequence(); // 0484
}

// 0766: with continues left, a ten second count (9 to 0, 90 frames per
// step); button 1 or 2 takes a continue.
Flow Game::continue_screen() {
    score.continues &= u8(~continue_taken);
    if (!score.continues) {
        co_return;
    }
    flow.continue_ticks = 0;
    flow.continue_count = 9;
    show_continue_screen(); // 2885
    show_continue_count();  // 28C2
    Object& s = sonic();
    s.type = object_type::sonic;
    s.state = 0x2F;
    s.next_state = 0x2F;
    update_sonic(); // 2F4F
    sound_.request(sound::continue_screen);
    flow.game_flags |= game_flag::playing;
    co_await continue_countdown(); // 07AF
    flow.game_flags &= u8(~game_flag::playing);
    start_fade_out();
    fade_music();
    co_await wait_frames(60);
}

// 07AF
Flow Game::continue_countdown() {
    for (;;) {
        co_await wait_frame();
        update_sonic();
        if (input.pressed[0] & 0x30) {
            // 07D7: Sonic runs off (state 30) for three seconds.
            score.continues = u8(((score.continues & ~continue_taken) - 1) | continue_taken);
            sound_.request(sound::extra_life);
            for (int i = 0; i < 0xB4; ++i) {
                co_await wait_frame();
                update_sonic();
            }
            co_return;
        }
        if (++flow.continue_ticks < 0x5A) {
            continue;
        }
        flow.continue_ticks = 0;
        if (--flow.continue_count == 0xFF) {
            co_return;
        }
        show_continue_count();
        sound_.request(sound::rings_lost);
    }
}

// 284B: "GAME OVER" (bank 0F) on a blank screen, faded in.
void Game::show_game_over() {
    clear_map_state();
    display.map.fill(0x1107);
    display.level_shown = false;
    load_tiles(256, rom::game_over_tiles); // 764B
    draw_rect({8, 11}, rom::game_over_map, 2, 18);
    fade_in_screen(); // 2870
}

// 2885: "CONTINUE" with its count (bank 0F) and Sonic's sprite tiles.
void Game::show_continue_screen() {
    clear_map_state();
    display.map.fill(0x010A);
    display.level_shown = false;
    load_tiles(256, rom::continue_tiles);       // 765F
    load_tiles(274, rom::continue_sonic_tiles); // 7673
    draw_rect({8, 9}, rom::continue_map, 2, 18);
    fade_in_screen();
}

// 2870 / 28AD: both palettes fade in, from palettes 3 and 1E.
void Game::fade_in_screen() {
    palette.fades[0] = 0x80;
    palette.fades[1] = 0x03;
    palette.fades[2] = 0x80;
    palette.fades[3] = 0x1E;
}

// 28C2: the count digit, 2 x 2 tiles (bank 0F, BC0E).
void Game::show_continue_count() {
    draw_rect({15, 13}, rom::continue_digits + u8(flow.continue_count * 8u), 2, 2);
}

// --- Sonic on the continue screen ------------------------------------------

// AA94 (state 2F): standing in the middle until a continue is taken.
void Game::sonic_continue_wait() {
    Object& o = sonic();
    o.x = 0x0080;
    o.y = 0x00A0;
    o.vx = 0;
    o.vy = 0;
    if (!(score.continues & continue_taken)) {
        return;
    }
    o.next_state = 0x30;
    o.timer = 0;
}

// AA7E (state 30): cycles the running frames 0C-0F, four ticks each.
void Game::sonic_continue_frames() {
    Object& o = sonic();
    u8& step = o.step;
    if (++step >= 4) {
        step = 0;
    }
    o.frame = u8(step + 0x0C);
    o.frame_ticks = 0x04;
}

// AAB8 (state 30): after 120 frames Sonic runs off to the right, faster
// every frame.
void Game::sonic_continue_leave() {
    Object& o = sonic();
    u8& ticks = o.timer;
    if (++ticks < 0x78) {
        return;
    }
    ticks = 0x78;
    o.vx = (unsigned(o.vx) + 0x00A0) & 0xFFFF;
    move_object(o); // 0257
}

} // namespace sonic2_gg
