// An act's title card (mode 4, 11DD) and play (mode 5, 1256).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 7827: the title card's art: 09:B700 at tile 10, 16:91E2 at tile C0,
// 03:BBEE at tile B0.
constexpr Table card_letters{0x09, 0xB700};
constexpr Table card_art{0x16, 0x91E2};
constexpr Table card_names{0x03, 0xBBEE};
constexpr u8 card_song = 0x88;
constexpr u8 card_sprite_palette = 0x37;
// 7AE0 / 7ABB: the game over and time over screens: art at tile C0 and a
// three-row banner at 3A90.
constexpr Table game_over_tiles{0x1A, 0xABF1};
constexpr Table game_over_banner{0x1F, 0xBE8E};
constexpr Table time_over_tiles{0x10, 0xAF42};
constexpr Table time_over_banner{0x10, 0xB0F2};
constexpr u8 game_over_song = 0x8E;
// 7B0B: the continue screen: art 14:BBC0 at tile 100, its banner (bank 19,
// 9A20: 3 rows of 15 at 3992), the countdown's digits (19:9A8A, 9 down to
// 0, each 3x2 at 3B1E) and two 2x2 marks swapping places every 8 frames
// (19:9A7A and 9A82, at 3B18 and 3B24).
constexpr Table continue_tiles{0x14, 0xBBC0};
constexpr unsigned continue_bank = 0x19;
constexpr Table continue_digits{0x19, 0x9A8A};
constexpr u8 continue_song = 0x9F;
} // namespace

// 11DD: the screen fades out; outside zones 6 on, the act's title card
// shows for 240 frames (its object, type 14, and the player running
// across, state 3B), then fades. Next: play.
Flow Game::title_card() {
    flow.frame_work = 0;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x3C); // 252C
    act.line_effect = 0;
    if (flow.zone < 0x06) {
        co_await clear_screen();    // 273B
        clear_game_state();         // 327E
        co_await load_title_card(); // 7827
        palette.fades[1].mode |= 0x20;
        palette.fades[1].palette = card_sprite_palette;
        palette.fade_period = 1;
        sound_.request(card_song, 2);
        flow.frame_work = frame_work::build_sprites;
        for (unsigned i = 0; i < 0xF0; ++i) {
            co_await next_frame();
            co_await update_player();      // 3431
            co_await update_objects();     // 4F3F
            co_await run_map_animations(); // 228E
        }
        palette.fade_period = 1;
        palette.fades[0].mode = 0x40;
        palette.fades[1].mode = 0x40;
        co_await wait_frames(0x3C); // 21BA
    }
    flow.mode = 0x05;
}

// 7827
Flow Game::load_title_card() {
    di();
    unsigned clocks = 17 + 4 + 53 + 85;
    clocks += load_tiles(0x0200, card_letters.bank, card_letters.address);
    clocks += 4 + 53 + 85 + load_tiles(0x1800, card_art.bank, card_art.address);
    clocks += 4 + 53 + 85 + load_tiles(0x1600, card_names.bank, card_names.address);
    objects[8].type = 0x14;           // the card
    objects[0].type = flow.character; // running across it
    objects[0].param = 0x3A;
    co_await busy(clocks);
}

// 1256: the act is played until its loop ends (1F2E); then the screen
// fades out (with the water's colours) and the next mode follows from
// D144: bit 2 mode 7, bit 5 a special stage, otherwise mode 6.
Flow Game::play_mode() {
    di();
    flow.frame_work = 0;
    co_await start_act(); // 1327
    flow.frame_work = frame_work::build_sprites;
    palette.fade_period = 3;
    score.rings_low = flow.rings_kept; // DC99
    flow.rings_kept = 0;
    show_rings();        // 33BA
    co_await act_loop(); // 1F2E
    co_await finish_play();
}

// 1256 after the act's loop: the act's end, the next mode.
Flow Game::finish_play() {
    act.palette_cycle_off = 0xFF; // 21D6
    flow.frame_work &= u8(~frame_work::build_sprites);
    co_await wait_frames(0x08);
    if (flow.frame_work & 0x20) {
        sound_.request(0xD3, 1);
    }
    co_await wait_frames(0x3C);
    // 1298: everything fades out, the water's colours too.
    palette.fade_period = 1;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    palette.water_fades[0].mode = 0x40;
    palette.water_fades[1].mode = 0x40;
    co_await wait_frames(0x3C);
    video.line_split = 0xFF;
    co_await wait_frames(0x0A);
    di();
    video.water_line_mode = 0;
    act.effect_flags |= 0x80;
    act.line_effect = 0;
    if (flow.frame_work & 0x04) {
        flow.mode = 0x07; // the life is lost
    } else if (flow.frame_work & 0x20) {
        // 12F3: the act is kept; the special stage by the emeralds so far
        // (table 1317: mode and act).
        flow.saved_zone = flow.zone;
        flow.saved_act = flow.act;
        const Table next{0x00, std::uint16_t(0x1317 + flow.emeralds * 2u)};
        flow.mode = rom.byte(next, 0);
        flow.act = rom.byte(next, 1);
    } else {
        flow.mode = 0x06; // the act is cleared
    }
}

// A quick save's program: the act's loop from the wait it was saved at,
// the act's end, then the modes as usual.
Flow Game::resumed_play() {
    co_await act_loop(true);
    co_await finish_play();
    co_await run_modes();
}

void Game::resume_play() {
    program_.start(resumed_play());
    powered_on_ = true;
    in_frame_wait_ = false;
    at_act_loop_wait_ = false;
}

// 15A1: modes 8 and 9: a special stage: zone 6 (act from 12F3) played as
// an act against a 1:30 clock counting down. Won (D144 bit 6) the screen
// fades to white and the results follow (mode 0A); otherwise back to the
// act left (mode 5, from where the player was).
Flow Game::special_stage() {
    di();
    flow.frame_work = 0;
    flow.zone = 0x06;
    clock.running = 0;
    co_await start_act(); // 1327
    flow.frame_work = frame_work::build_sprites;
    palette.fade_period = 3;
    show_rings(); // 33BA
    score.seconds = 0x30;
    score.minutes = 0x01;
    clock.running = 0xFF;
    show_clock();                 // 3035
    co_await act_loop();          // 1F2E
    act.palette_cycle_off = 0xFF; // 21D6
    stop_music();                 // 2288
    const bool won = flow.frame_work & 0x40;
    palette.fade_period = 2;
    palette.fades[0].mode = won ? 0x50 : 0x40;
    palette.fades[1].mode = won ? 0x50 : 0x40;
    co_await wait_frames(0x78); // 21BA
    flow.zone = flow.saved_zone;
    flow.act = flow.saved_act;
    if (!won) {
        flow.mode = 0x05;
        co_return;
    }
    // 162E: the results follow directly (not through the mode loop's
    // music stop and camera reset).
    flow.mode = 0x0A;
    co_await special_results(); // 1633
}

// 13C8: mode 7: a life lost: the screen cleared; with lives left the act
// again from its title card (after the time-over screen when the clock
// ran out), otherwise the continue screen or game over (1418).
Flow Game::life_lost() {
    input.disabled = 0;
    flow.frame_work = 0;
    display_off();           // 26E4
    co_await clear_screen(); // 273B
    clear_map_state();       // 428B
    display_on();            // 26D6
    const bool none_left = score.lives == 0;
    score.lives = bcd_sub(score.lives, 1);
    if (none_left) {
        // 1418: with a continue (D14B) the continue screen.
        bool continued = false;
        if (score.point_rewards) {
            co_await continue_screen(continued);
        }
        if (continued) {
            co_return;
        }
        co_await game_over(); // 156B
        co_return;
    }
    if (act.time_over == 0x80) {
        // 7ABB: the time over banner, faded in for 180 frames.
        di();
        co_await busy(
            17 + 53 + 30 + load_tiles(0x1800, time_over_tiles.bank, time_over_tiles.address) + 53 +
            30 + draw_rect(0x3A90, time_over_banner.bank, time_over_banner.address, 0x03, 0x10));
        ei();
        palette.fade_period = 1;
        palette.fades[0] = {0x80, 0x0B};
        palette.fades[1] = {0x80, 0x0B};
        co_await wait_frames(0xB4); // 21BA
    }
    flow.mode = 0x04;
}

// 141F: the continue screen. The player (BA18: reset, parameter 46: state
// 47) stands while a digit counts from 9 down, a step a second, for 630
// frames; a button sends him running (D536) and once off to the right
// (X low byte E0) the game continues: three lives, a continue less, the
// act's title card. Run out (D539 FF) he falls away for 180 frames and the
// game is over. The countdown keeps its counters in the player's own bytes
// 34-39, as his routines read them.
Flow Game::continue_screen(bool& continued) {
    reset_player_for_continue(); // 1A:BA18
    clear_sprites();             // 2757
    clear_vram();                // 7BCF
    co_await busy(sprite_clear_clocks() + 211242);
    co_await next_frame(); // 084C
    di();
    display.map.fill(0x0100);
    unsigned clocks = fill_clocks(0x380) + 17 + 53 + 30;
    clocks += load_tiles(0x2000, continue_tiles.bank, continue_tiles.address);
    clocks += 53 + 30 + draw_rect(0x3992, continue_bank, 0x9A20, 0x03, 0x0F);
    co_await busy(clocks);
    ei();
    sound_.request(continue_song, 2);
    palette.fade_period = 1;
    palette.fades[0] = {0x80, 0x0B};
    palette.fades[1] = {0x80, 0x0F};
    flow.frame_work = 0x02;
    auto& p = objects[0];
    u8& digit = p.scratch[0];         // D534
    u8& digit_ticks = p.scratch[1];   // D535
    const u8& running = p.scratch[2]; // D536 (set by state 47's routine)
    u8& still_running = p.scratch[3]; // D537
    u8& timed_out = p.scratch[5];     // D539
    digit = 0x09;
    digit_ticks = 0x01;
    still_running = 0;
    std::uint16_t ticks = 0x0276;
    for (;;) {
        if (!--digit_ticks) {
            digit_ticks = 0x3C;
            di();
            co_await busy(draw_rect(0x3B1E, continue_bank, rom.word(continue_digits, digit * 2u),
                                    0x03, 0x02));
            ei();
            digit = digit ? u8(digit - 1) : 0;
        }
        co_await next_frame();    // 084C
        co_await update_player(); // 3431
        if (!(video.frame_counter & 0x07)) {
            const bool swap = video.frame_counter & 0x08;
            di();
            co_await busy(draw_rect(0x3B18, continue_bank, swap ? 0x9A82 : 0x9A7A, 0x02, 0x02) +
                          draw_rect(0x3B24, continue_bank, swap ? 0x9A7A : 0x9A82, 0x02, 0x02));
            ei();
        }
        if (running && lo(p.x) >= 0xE0) {
            // 153B
            stop_music(); // 2288
            palette.fade_period = 1;
            palette.fades[0].mode = 0x40;
            palette.fades[1].mode = 0x40;
            co_await wait_frames(0x78); // 21BA
            score.lives = 0x03;
            --score.point_rewards;
            flow.mode = 0x04;
            continued = true;
            co_return;
        }
        if (--ticks || still_running) {
            continue;
        }
        break;
    }
    timed_out = 0xFF;
    for (unsigned i = 0; i < 0xB4; ++i) {
        co_await next_frame();
        co_await update_player();
    }
    stop_music();
    palette.fade_period = 1;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x3C); // 21BA
}

// 1A:BA18: the player object made afresh for the continue screen: the
// character, no script, no contacts, parameter 46 (state 47); no checks
// after his routine (D428), no water.
void Game::reset_player_for_continue() {
    player.checks_off = 0xFF;
    auto& p = objects[0];
    p.type = flow.character;
    p.state = p.next_state = 0;
    p.display = 0;
    p.frame_ticks = 0;
    p.script = {};
    p.touch = p.touch_sides = p.terrain = p.contact = 0;
    p.flags = 0;
    p.status = 0;
    p.param = 0x46;
    act.water_level = 0;
}

// 156B: the game over banner (7AE0) with its song, faded in; after 480
// frames the program starts again from the SEGA logo (mode 0).
Flow Game::game_over() {
    clear_sprites(); // 2757
    clear_vram();    // 7BCF
    di();
    co_await busy(sprite_clear_clocks() + 211242 + 17 + 53 + 30 +
                  load_tiles(0x1800, game_over_tiles.bank, game_over_tiles.address) + 53 + 30 +
                  draw_rect(0x3A90, game_over_banner.bank, game_over_banner.address, 0x03, 0x11));
    ei();
    sound_.request(game_over_song, 2);
    palette.fade_period = 1;
    palette.fades[0] = {0x80, 0x0B};
    palette.fades[1] = {0x80, 0x0B};
    co_await wait_frames(0x01E0); // 21BA
    flow.mode = mode::sega_logo;
}

} // namespace triple_trouble_gg
