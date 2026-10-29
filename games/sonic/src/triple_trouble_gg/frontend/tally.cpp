// An act cleared (mode 6, 1352): the results screen counts the rings and
// the time into bonuses (bank 19, BD4A-BF7A), adds them to the points (a
// hundred per step), then the next act's title card (or, after zone 5,
// mode 14).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 7871: the screen: art 19:9B16 at tile 0, the map for Sonic (19:B20A) or
// Tails (19:B4DA), the act's number (table 1:790E), an emblem per reward
// (19:B1CA Sonic, 19:B1D6 Tails; at most four).
constexpr Table tally_tiles{0x19, 0x9B16};
constexpr Table act_numbers{0x01, 0x790E};
// Bank 19: the digits (B19E: per digit its 2x1 entries), a field's blank
// (B192: 2x3) and the ring/time/points labels' marks (B16A).
constexpr unsigned tally_bank = 0x19;
constexpr Table digit_entries{0x19, 0xB19E};
constexpr std::array<u8, 3> hundred_points{0x00, 0x01, 0x00}; // BF7B
// 1A:BA03: per zone the boss act's bonus (3 bytes of BCD points).
constexpr Table boss_bonus{0x1A, 0xBA03};
constexpr u8 tick_sound = 0xB8;
constexpr u8 done_sound = 0xD2;
} // namespace

// 1352
Flow Game::act_cleared() {
    di();
    flow.frame_work = 0;
    clear_continue_state(); // 32F4
    flow.special_entry = 0; // 3302
    flow.special_return = {};
    co_await clear_screen(); // 273B
    clear_sprites();         // 2757
    clear_map_state();       // 428B
    stop_level_animations(); // 25B6
    reset_camera();          // 3310
    if (flow.zone == 5 && flow.act == 2) {
        ++flow.emeralds;
    }
    co_await load_tally_screen(); // 7871
    co_await tally();             // 240B
    set_fades(2, 0x50, palette.fades[0].palette, 0x50, palette.fades[1].palette);
    co_await wait_frames(0x78);
    if (++flow.act >= 3) {
        flow.act = 0;
        if (++flow.zone >= 6) {
            flow.mode = 0x14;
            co_return;
        }
    }
    flow.mode = 0x04;
}

// 1633: mode 0A: a special stage's results on a white screen: the
// emeralds won, the rings and time bonuses (240B); then the act again
// (mode 5).
Flow Game::special_results() {
    co_await clear_screen(); // 273B
    clear_sprites();         // 2757
    clear_game_state();      // 327E
    clear_map_state();       // 428B
    fill_white();            // 340C
    flow.frame_work = 0x40;
    co_await load_special_results(); // 7914
    co_await tally();                // 240B
    palette.fade_period = 2;
    palette.fades[0].mode = 0x40;
    palette.fades[1].mode = 0x40;
    co_await wait_frames(0x78); // 21BA
    flow.frame_work = 0;
    flow.mode = 0x05;
}

// 7914: the tally's art, the character's picture (19:B7AA Sonic, BA7A
// Tails) and an emerald (19:B1E2 on, 8 bytes each) per emerald won from
// 3C8E (6 apart; with none the count wraps to 256).
Flow Game::load_special_results() {
    di();
    unsigned clocks = 17 + 4 + 53 + load_tiles(0x0000, tally_tiles.bank, tally_tiles.address);
    const unsigned map = flow.character == 1 ? 0xB7AA : 0xBA7A;
    clocks += 60 + draw_rect(0x38CC, tally_bank, map, 0x12, 0x14);
    const unsigned count = flow.emeralds ? flow.emeralds : 256;
    for (unsigned i = 0; i < count; ++i) {
        clocks += 60 + draw_rect(0x3C8E + 6 * i, tally_bank, 0xB1E2 + 8 * i, 0x02, 0x02);
    }
    co_await busy(clocks);
    ei();
}

// 25B6: no tile animations, no ring art, no water line.
void Game::stop_level_animations() {
    for (auto& a : animation.animators) {
        a.type = 0;
    }
    animation.ring_vram = 0;
    video.water_line_mode = 0;
}

// 7871
Flow Game::load_tally_screen() {
    di();
    video.loading = 0xFF;
    co_await busy(17 + 4 + 53);
    co_await load_tiles_slowly(0x0000, tally_tiles.bank, tally_tiles.address);
    video.loading = 0;
    const unsigned map = flow.character == 1 ? 0xB20A : 0xB4DA;
    di();
    co_await busy(60 + draw_rect(0x38CC, tally_bank, map, 0x12, 0x14));
    ei(); // 2C73
    co_await busy(60 +
                  draw_rect(0x39EC, tally_bank, rom.word(act_numbers, flow.act * 2u), 0x04, 0x01));
    ei();
    if (!score.point_rewards) {
        co_return;
    }
    const unsigned count = score.point_rewards >= 5 ? 4 : score.point_rewards;
    const unsigned emblem = flow.character == 1 ? 0xB1CA : 0xB1D6;
    for (unsigned i = 0; i < count; ++i) {
        co_await busy(60 + draw_rect(0x3C5A + 6 * i, tally_bank, emblem, 0x03, 0x02));
        ei();
    }
}

// 78C2: an emblem per reward (at most four) on the results screen.
void Game::draw_reward_emblems() {
    if (!score.point_rewards) {
        return;
    }
    const unsigned count = score.point_rewards >= 5 ? 4 : score.point_rewards;
    const unsigned emblem = flow.character == 1 ? 0xB1CA : 0xB1D6;
    for (unsigned i = 0; i < count; ++i) {
        draw_rect(0x3C5A + 6 * i, tally_bank, emblem, 0x03, 0x02);
    }
}

// 240B: the tally (19:BD4A), the boss act's bonus (1A:B9DE), the points
// shown once more (19:BECE), a second's pause.
Flow Game::tally() {
    co_await tally_bonuses(); // 19:BD4A
    // 1A:B9DE: after a zone's third act (in mode 6) the zone's bonus.
    if (flow.mode == 0x06 && flow.act == 2) {
        score.seconds = 0;
        score.minutes = 0;
        const Table bonus{0x1A, rom.word(boss_bonus, flow.zone * 2u)};
        add_points(std::array<u8, 3>{rom.byte(bonus, 0), rom.byte(bonus, 1),
                                     rom.byte(bonus, 2)}); // 2430 (049B)
    }
    co_await busy(show_points());
    ei();
    co_await wait_frames(0x3C);
}

// 19:BD4A: the labels' marks; 99 rings at most count; the bonuses (D156
// rings, D158 time; BCD) start at 0. The screen fades in (from white with
// D144 bit 6). The rings count into their bonus one per frame (a tick
// every fourth frame), then the time (unless act 3 or 6 on: 99 minus the
// time in tens of seconds, 99 for 0) into its bonus; then both bonuses
// into the points, 100 a step. Sound D2, and rings and clock are cleared.
Flow Game::tally_bonuses() {
    unsigned clocks = 0;
    clocks += draw_rect(0x3AEE, tally_bank, 0xB16A, 0x02, 0x01);
    clocks += draw_rect(0x3B6E, tally_bank, 0xB16A, 0x02, 0x01);
    clocks += draw_rect(0x3BEE, tally_bank, 0xB16A, 0x02, 0x01);
    if (score.rings_high) {
        score.rings_low = 0x99;
    }
    score.ring_bonus = {};
    score.time_bonus = {};
    clocks += show_points() + show_ring_bonus() + show_time_bonus();
    co_await busy(clocks);
    ei();
    palette.fade_period = 2;
    const u8 mode = (flow.frame_work & 0x40) ? 0x90 : 0x80;
    palette.fades[0] = {mode, 0x0D};
    palette.fades[1] = {mode, 0x0E};
    co_await wait_frames(0x78); // 21BA (0498)
    video.busy = 0;
    // BDF4: the rings.
    for (;;) {
        co_await next_frame(); // 0300
        tally_tick();          // BEC0
        co_await busy(show_ring_bonus());
        ei();
        if (!score.rings_low) {
            break;
        }
        score.rings_low = bcd_sub(score.rings_low, 1);
        score.ring_bonus[1] = bcd_add(score.ring_bonus[1], 1);
    }
    // BE13: the time.
    const bool no_time_bonus =
        (flow.mode == 0x0A && (flow.emeralds == 0 || flow.emeralds == 2 || flow.emeralds == 4)) ||
        (flow.zone < 6 && flow.act == 2);
    if (!no_time_bonus) {
        const u8 tens = u8(((score.minutes << 8 | score.seconds) >> 4) & 0xFF);
        const u8 left = bcd_sub(0, tens);
        score.rings_low = left ? left : 0x99;
        for (;;) {
            co_await next_frame();
            tally_tick();
            co_await busy(show_time_bonus());
            ei();
            if (!score.rings_low) {
                break;
            }
            score.rings_low = bcd_sub(score.rings_low, 1);
            score.time_bonus[1] = bcd_add(score.time_bonus[1], 1);
        }
    }
    // BE73: the ring bonus into the points.
    for (;;) {
        co_await next_frame();
        tally_tick();
        co_await busy(show_ring_bonus() + show_points());
        ei();
        if (!score.ring_bonus[1]) {
            break;
        }
        score.ring_bonus[1] = bcd_sub(score.ring_bonus[1], 1);
        add_points(hundred_points); // 2430 (049B)
    }
    // BE93: the time bonus into the points.
    co_await busy(show_ring_bonus() + show_points());
    ei();
    for (;;) {
        co_await next_frame();
        tally_tick();
        co_await busy(show_time_bonus() + show_points());
        ei();
        if (!score.time_bonus[1]) {
            break;
        }
        score.time_bonus[1] = bcd_sub(score.time_bonus[1], 1);
        add_points(hundred_points);
    }
    co_await busy(show_time_bonus() + show_points());
    ei();
    sound_.request(done_sound, 1);
    co_await wait_frames(0x78);
    score.rings_low = score.rings_high = 0;
    score.seconds = score.minutes = score.clock_extra = 0;
}

// BEC0: a tick on every fourth frame.
void Game::tally_tick() {
    if ((video.frame_counter & 0x03) == 0x03) {
        sound_.request(tick_sound, 1);
    }
}

// BECE: the points (6 digits at 3BE4).
unsigned Game::show_points() {
    return draw_number(0x3BE4, score.points.data(), 3);
}

// BEDD / BEF2: a bonus: its field blanked (B192), then 4 digits.
unsigned Game::show_ring_bonus() {
    return draw_rect(0x3AE8, tally_bank, 0xB192, 0x02, 0x03) +
           draw_number(0x3AE8, score.ring_bonus.data(), 2);
}

unsigned Game::show_time_bonus() {
    return draw_rect(0x3B68, tally_bank, 0xB192, 0x02, 0x03) +
           draw_number(0x3B68, score.time_bonus.data(), 2);
}

// 19:BF13: `count` BCD bytes, highest first (from `low[count - 1]`), as
// digits two entries apart from `name_address`; leading zeros are left
// out (their place kept).
unsigned Game::draw_number(unsigned name_address, const u8* low, unsigned count) {
    unsigned clocks = 0;
    bool shown = false;
    for (unsigned i = 0; i < count; ++i) {
        const u8 b = low[count - 1 - i];
        for (const u8 d : {u8(b >> 4), u8(b & 0x0F)}) {
            if (d || shown) {
                shown = true;
                clocks += draw_rect(name_address, tally_bank, rom.word(digit_entries, d * 2u), 0x02,
                                    0x01);
            }
            name_address += 2;
        }
    }
    return clocks;
}

} // namespace triple_trouble_gg
