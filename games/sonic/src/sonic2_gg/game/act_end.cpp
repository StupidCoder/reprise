// The end of an act (07F4) or of a zone's last act (086F): the tally of
// rings and time bonus, then the next act, the next zone or the ending.

#include "sonic2_gg/game/bcd.hpp"
#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 07F4
Flow Game::act_complete() {
    clock.running = 0; // 0A5C
    record_emerald();  // 084C
    co_await wait_frames(180);
    score.lives_at_start = score.lives;
    stop_palette_cycles(); // 7C26
    start_fade_out();
    co_await wait_frames(30);
    sound_.request(sound::stop);
    co_await show_tally(); // 0817
    flow.game_flags &= u8(~game_flag::act_complete);
    ++flow.act;
    flow.game_flags |= game_flag::act_start;
}

// 086F: play goes on for 300 more frames (the capsule opening) first.
Flow Game::zone_complete() {
    clock.running = 0;
    record_emerald();
    for (int i = 0; i < 300; ++i) {
        co_await wait_frame();
        game_frame(); // 09BB
    }
    sound_.state.current = 0; // the driver stops everything (DD03)
    stop_palette_cycles();
    start_fade_out();
    co_await wait_frames(30);
    co_await show_tally();
    flow.act = 0;
    score.emerald_found = 0;
    ++flow.zone;
    // 08C5: after zone 5 the last zone needs five emeralds; after it, all
    // six choose the second ending (act 1). Otherwise the ending (zone 7).
    if (flow.zone < 6 || (flow.zone == 6 && (score.emeralds & 0x1F) == 0x1F)) {
        flow.game_flags = u8((flow.game_flags & ~game_flag::zone_complete) | game_flag::act_start);
        co_return;
    }
    if (flow.zone > 6 && (score.emeralds & 0x3F) == 0x3F) {
        flow.act = 1;
    }
    flow.zone = 7;
    flow.game_flags = u8((flow.game_flags & ~game_flag::zone_complete) | game_flag::scene);
}

// 0817 / 0896: the tally screen (the title card without bit 2: "ACT CLEAR"
// style text with the ring, bonus and score lines), counted down, faded out.
Flow Game::show_tally() {
    clear_level_state();   // 223C
    clear_map_state();     // 53B6
    co_await next_frame(); // 17EE
    clear_screen();
    add_time_bonus();       // 1F4A
    co_await title_card();  // 269D
    co_await count_tally(); // 1C6C
    co_await wait_frames(60);
    start_fade_out();
    co_await wait_frames(60);
}

// 084C: an emerald found in this act gives a continue and sets the zone's
// bit (SCF, then RLA zone+1 times: 1 << zone).
void Game::record_emerald() {
    if (!score.emerald_found) {
        return;
    }
    ++score.continues;
    score.emeralds |= u8(1u << flow.zone);
    score.emerald_found = 0;
}

// 20D5: adds a 3-byte BCD value to the bonus (not in demos), capping at
// 999,990.
void Game::add_bonus(const u8* value) {
    if (flow.attract_flags) {
        return;
    }
    bool carry = false;
    for (int i = 0; i < 3; ++i) {
        score.bonus[i] = add_bcd(score.bonus[i], value[i], carry);
    }
    if (carry) {
        score.bonus[0] = 0x90;
        score.bonus[1] = 0x99;
        score.bonus[2] = 0x99;
    }
}

// 1F4A: the time bonus. Under a minute, a table by the seconds (1FCC:
// pointers into the amounts at 1EC9; 0-19 seconds share one). Otherwise
// table 2040 by minutes and tens of seconds, one step less from the fifth
// second of each ten; exactly 9:59 gives the amount at 1F2C.
void Game::add_time_bonus() {
    u8 value[3];
    if (!clock.minutes) {
        u8 seconds = clock.seconds;
        if (seconds < 0x20) {
            seconds = 0x20;
        }
        const unsigned pointer = rom.word(rom::time_bonus_seconds, u8((seconds - 0x20) * 2));
        for (unsigned i = 0; i < 3; ++i) {
            value[i] = rom.byte(0, pointer + i);
        }
        add_bonus(value);
        return;
    }
    score.time_bonus[0] = 0;
    score.time_bonus[1] = 0;
    score.time_bonus[2] = 0;
    const unsigned time = clock.seconds | (clock.minutes << 8);
    if (time == 0x0959) {
        for (unsigned i = 0; i < 3; ++i) {
            value[i] = rom.byte(rom::time_bonus_959, i);
        }
        add_bonus(value);
        return;
    }
    u8 step = u8(time >> 4);
    step = step >= 0x10 ? u8(step - 0x10) : 0;
    score.time_bonus[0] = rom.byte(rom::time_bonus_steps, step);
    if ((clock.seconds & 0x0F) >= 5 && score.time_bonus[0] != 0x01) {
        bool borrow = false;
        score.time_bonus[0] = sub_bcd(score.time_bonus[0], 0x01, borrow);
    }
    score.time_bonus[1] = 0;
    score.time_bonus[2] = 0;
    add_bonus(score.time_bonus.data());
}

// 1C6C: rings, then the bonus, are counted into the score 100 points at a
// time with a tick sound every 8 frames; holding button 1 or 2 counts
// without waiting for frames (and ticks every 4).
Flow Game::count_tally() {
    auto tick = [this] {
        const u8 mask = (input.held[0] & 0x30) ? 0x03 : 0x07;
        if (!(video.frame_counter & mask)) {
            sound_.request(sound::tally);
        }
    };
    for (;;) {
        if (!(input.held[0] & 0x30)) {
            co_await next_frame();
        } else {
            co_await fast_tally_step();
        }
        if (!score.rings) {
            break;
        }
        bool borrow = false;
        score.rings = sub_bcd(score.rings, 0x01, borrow);
        add_score(rom::points_100); // 1D2C
        show_ring_tally();          // 1DAB
        show_score_tally();         // 1DCB
        tick();
    }
    co_await wait_frames(60);
    do {
        if (!(input.held[0] & 0x30)) {
            co_await next_frame();
        } else {
            co_await fast_tally_step();
        }
        bool borrow = false;
        score.bonus[0] = sub_bcd(score.bonus[0], rom.byte(rom::points_100), borrow);
        score.bonus[1] = sub_bcd(score.bonus[1], rom.byte(rom::points_100, 1), borrow);
        add_score(rom::points_100);
        show_bonus_tally(); // 1DBC
        show_score_tally();
        tick();
    } while (score.bonus[0] | score.bonus[1]);
    // 1D05: the tally's closing tiles.
    draw_rect({21, 16}, rom::tally_end, 2, 2);
}

// Holding a button, the original repeats the tally step without waiting for
// the frame interrupt, as fast as the drawing allows. The reconstruction
// lets a frame pass after every few steps instead (hypothesis: the step
// count per frame is an estimate, see the research notes).
Flow Game::fast_tally_step() {
    if (++fast_tally_steps_ >= 3) {
        fast_tally_steps_ = 0;
        co_await next_frame();
    }
}

} // namespace sonic2_gg
