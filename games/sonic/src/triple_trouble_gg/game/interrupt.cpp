// The frame interrupt (0038 -> 0576) and the services only it runs.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 09DC: the screen shake's scroll offsets (X, Y) by D3BE bits 1-2.
constexpr Table shake_offsets{0x00, 0x09DC};
// 09E4: the band effect's first line counter, then (09E5) the counter each
// band's interrupt sets for the reload after next.
constexpr Table band_counters{0x00, 0x09E4};
constexpr unsigned band_count = 7;
} // namespace

// 0576: at the start of each frame. (The nested-interrupt guard DC9A, set
// while the sound driver runs with interrupts on, has no counterpart: the
// reconstruction never interrupts itself.)
void Game::frame_interrupt() {
    // The colours the last line interrupt left stay in the colour memory.
    if (!display.changes.empty()) {
        display.colors = display.changes.back().colors;
        display.changes.clear();
    }
    // So does the scroll the last band left.
    if (!display.scroll_changes.empty()) {
        display.scroll_x = display.scroll_changes.back().scroll_x;
        display.scroll_changes.clear();
    }
    const bool full = !video.loading && !video.busy;
    if (!video.loading) {
        // 0785: the flight stages and the ending with all emeralds have
        // their own frame (078D); mode 14 otherwise the usual one.
        if (flow.mode == 0x0D || (flow.mode == 0x14 && flow.emeralds == 0x06)) {
            flight_frame_interrupt();
            return;
        }
        // 059E: the water line's interrupt for this frame.
        video.water_line_mode = 0;
        palette.water_colors_shown = 0;
        if (act.water_level) {
            arm_water_line(); // 05A7
        } else if (flow.mode != 0x06 && act.line_effect) {
            // 0625: the scroll bands' interrupts, from the first band.
            video.band = 0;
            video.band_counter = rom.byte(band_counters, 0);
            video.registers[0] |= 0x10;
            display.line_interrupt = true;
            video.water_line_mode = 0xFE;
        }
        // 0645
        palette.water_upload = 0;
        if (palette.fades[0].mode) {
            palette.dirty = 0xFF;
        }
    }
    if (full) {
        display_off(); // 065B
        // 065E: the scroll, moved by the screen shake.
        u8 dx = 0, dy = 0;
        if (camera.shake_ticks) {
            --camera.shake_ticks;
            const unsigned i = camera.shake_ticks & 0x06;
            dy = rom.byte(shake_offsets, i);
            dx = rom.byte(shake_offsets, i + 1);
        }
        display.scroll_x = u8(camera.scroll_x + dx);
        display.scroll_y = u8(camera.scroll_y + dy);
        // The act's picture follows the camera only while the map streams
        // (068C): a stopped map keeps what the name table last received.
        if (!display.level_shown || picture_origin.pending ||
            (camera.map_flags & map_flag::update_pending)) {
            picture_origin.x = picture_x();
            picture_origin.y = picture_y();
            picture_origin.pending = false;
        }
        apply_camera();   // 068C
        upload_sprites(); // 27A0
        if (!(flow.frame_work & frame_work::no_level_services)) {
            animate_tiles(); // bank 1D, 8770
        }
        display_on();        // 26D6
        upload_colors();     // 2582
        read_controller();   // 2D43
        upload_player_art(); // 0A0C
        // 06DE: without level services (D144 bit 0: the pause) none of the
        // rest either.
        if (!(flow.frame_work & frame_work::no_level_services)) {
            run_fades(); // bank 0E, B4D3
            if (flow.frame_work & frame_work::build_sprites) {
                if (!video.water_line_mode) {
                    build_sprites();     // 2A14
                    track_player_art();  // 0B01
                    track_partner_art(); // 0B2E
                }
                spin_hud_ring(); // bank 1D, 87D5
            }
            run_animators(); // bank 1D, 8000
        }
    } else if (!video.loading) {
        upload_colors(); // 077F: the program is busy: colours only
    }
    // 0713: a song asked for by the program, then the sound driver.
    if (act.music_request) {
        sound_.request(act.music_request, 2);
        act.music_request = 0;
    }
    sound_.tick();
    update_clock();        // 2F5F
    ++video.frame_counter; // D12E
    if (flow.zone == 1 && flow.act == 2 && flow.mode != 4 && flow.mode != 6 && flow.mode != 7) {
        sunset_park_3_service(); // 1C72
    }
    ++video.frame_done; // D132
}

// 1C72: zone 1 act 3's requests (DC56, from its objects; bit 7 none):
// bit 0 stops the line effects and the checks after the player's routine;
// bits 1 and 2, once byte DBEB (in the decoder's buffer, used here as a
// counter; hypothesis) reaches D8, draw block E3 (bit 1, then bit 3 marks
// it shown) or block A0 (bit 2, clearing bits 2 and 3) at name table 3900.
void Game::sunset_park_3_service() {
    u8& f = act.effect_flags;
    if (f & 0x80) {
        return;
    }
    if (f & 0x01) {
        f &= u8(~0x01);
        act.line_effect = 0xFF;
        video.water_line_mode = 0xFF;
        act.variant = 0xFF;
        player.checks_off = 0xFF;
        return;
    }
    const u8 counter = act.work[5]; // DBEB
    unsigned block;
    if (f & 0x02) {
        if (counter < 0xD8) {
            return;
        }
        f = u8((f & ~0x02) | 0x08);
        block = 0xE3;
    } else if (f & 0x04) {
        if (counter < 0xD8) {
            return;
        }
        f &= u8(~0x0C);
        block = 0xA0;
    } else {
        return;
    }
    draw_block_at(0x3900, block); // 1DE4
}

// 0930: the band effect's interrupts over the frame. The chip's line
// counter runs down once per line from the counter loaded at the top
// (09E4) and, run out, interrupts and reloads from register 10, which each
// interrupt sets for the reload after next (table 09E5). Each interrupt
// scrolls the following lines by the next band's byte (DBE7-DBEC); after
// the seventh the scroll is 0 and the interrupt off. Applied after the
// frame's program, with the bands it left.
void Game::band_interrupts() {
    unsigned reload = video.band_counter;
    unsigned counter = reload;
    for (int line = 0; line <= 192 && display.line_interrupt; ++line) {
        if (counter) {
            --counter;
            continue;
        }
        counter = reload;
        const unsigned band = video.band;
        reload = rom.byte(band_counters, 1 + band);
        if (++video.band == band_count) {
            // 0999
            video.band = 0;
            display.scroll_changes.push_back({line + 1, 0});
            video.registers[0] &= u8(~0x10);
            display.line_interrupt = false;
            break;
        }
        display.scroll_changes.push_back({line + 1, band_scroll(band)});
    }
    video.band_counter = u8(reload);
}

// DBE7 + band: zone 1 act 3's band scrolls.
u8 Game::band_scroll(unsigned band) const {
    return act.work[1 + band];
}

// 05A7: with water on screen the line interrupt is armed at the water's
// line (D12F = FF): where the water line is above the screen and its
// colours changed, from line 2 with the normal colours' upload held back
// (DBCA). Below the screen, or above it unchanged, no interrupt: the
// colour memory keeps what the last one left.
void Game::arm_water_line() {
    if (flow.zone == 4 && flow.act == 2) {
        return;
    }
    const unsigned d = (act.water_level - camera.y) & 0xFFFF;
    u8 line;
    if (palette.water_dirty) {
        palette.water_dirty = 0;
        if (d & 0x8000) {
            palette.water_colors_shown = 0xFF;
            line = 2;
        } else {
            if (d >= 0xA8) {
                return;
            }
            line = u8(d);
        }
    } else {
        if (d & 0x8000) {
            return;
        }
        palette.dirty |= palette.water_upload;
        if (d >= 0xA8) {
            return;
        }
        line = u8(d);
    }
    video.water_line = line;
    video.registers[0] |= 0x10;
    display.line_interrupt = true;
    video.water_line_mode = 0xFF;
}

// 085B: at the water line the underwater colours replace all 32 colours
// (and the interrupt is disabled again); when the line is more than 20
// lines below the camera's top the normal colours are uploaded again at
// the next frame (D4F4). Applied after the frame's program, where the
// original's line comes.
void Game::water_line_interrupt() {
    if (display.line_interrupt && !video.line_split &&
        (flow.mode == 0x0D || (flow.mode == 0x14 && flow.emeralds == 0x06))) {
        horizon_interrupts(); // 095A
        return;
    }
    if (display.line_interrupt && video.line_split) {
        // 0999 (D130 set as an act ends): the band count back to 0, the
        // scroll 0, the line interrupt off.
        video.band = 0;
        display.scroll_changes.push_back({0, 0});
        video.registers[0] &= u8(~0x10);
        display.line_interrupt = false;
        return;
    }
    if (act.line_effect && display.line_interrupt) {
        band_interrupts(); // 0930
        return;
    }
    if (video.water_line_mode != 0xFF || !display.line_interrupt) {
        return;
    }
    video.registers[0] &= u8(~0x10);
    display.line_interrupt = false;
    display.changes.push_back({video.water_line + 1, palette.water_colors});
    if (((camera.y + 0x20) & 0xFFFF) < act.water_level) {
        palette.water_upload = 0xFF;
    }
}

// 2D43: the controller (ports DC and 00). Opposite directions pressed
// together cancel. A demo replaces the buttons (2DA7); while the background
// fades in or out the buttons read as released.
void Game::read_controller() {
    if (input.disabled) {
        return;
    }
    input.held_previous = input.held;
    input.pressed_previous = input.pressed;
    u8 held = u8(buttons_ & 0xBF);
    if ((held & 0x03) == 0x03) {
        held &= u8(~0x03);
    }
    if ((held & 0x0C) == 0x0C) {
        held &= u8(~0x0C);
    }
    input.held = held;
    const u8 pressed = u8(held & 0xBF & ~input.held_previous);
    input.pressed = pressed;
    input.pressed_copy = pressed;
    if (flow.demo & 0x80 && flow.demo_input) {
        // 2DA7: the demo's recorded buttons.
        input.held = rom.byte(flow.demo_bank, flow.demo_input);
        input.pressed = rom.byte(flow.demo_bank, flow.demo_input + 1u);
        flow.demo_input = std::uint16_t(flow.demo_input + 2);
    }
    if (palette.fades[0].mode & 0xC0) {
        input.held = 0;
        input.pressed = 0;
    }
}

// 2F5F: the act's clock: a second every 60 frames (BCD); at 10:00 time
// is over (D3CF = C0) and the clock stops. The HUD shows it (3035).
void Game::update_clock() {
    if (clock.stopped || !clock.running) {
        return;
    }
    if (flow.zone == 6) {
        count_down_clock(); // 2FE8
        return;
    }
    if (flow.mode == 0x11) {
        throw Unrecovered("the clock of mode 11 (2FA4)");
    }
    if (++clock.ticks != 0x3C) {
        return;
    }
    clock.ticks = 0;
    score.seconds = bcd_add(score.seconds, 1);
    if (score.seconds == 0x60) {
        score.seconds = 0;
        score.minutes = bcd_add(score.minutes, 1);
        if (score.minutes == 0x10) {
            act.time_over = 0xC0;
            clock.running = 0;
            return;
        }
    }
    show_clock(); // 3035
}

// 2FE8: zone 6 (the special stages) counts down a second every 60 frames,
// beeping (sound C0) over the last 10 seconds; at 0:00 the clock stops and
// the stage ends as a life lost would (D144 bit 2).
void Game::count_down_clock() {
    if (++clock.ticks != 0x3C) {
        return;
    }
    clock.ticks = 0;
    if ((score.seconds | score.minutes << 8) <= 0x0010) { // 3024
        sound_.request(0xC0, 1);
    }
    const bool seconds_out = score.seconds == 0;
    score.seconds = bcd_sub(score.seconds, 1);
    if (seconds_out) {
        score.seconds = 0x59;
        const bool minutes_out = score.minutes == 0;
        score.minutes = bcd_sub(score.minutes, 1);
        if (minutes_out) {
            score.minutes = 0;
            clock.running = 0;
            score.seconds = 0;
            flow.frame_work |= 0x04;
        }
    }
    show_clock(); // 3035
}

// 3035: the clock's digits in the HUD (entries 60, 62, 63; tiles 2E, 30,
// ... are 0-9). Not in act 3 (outside mode 9).
void Game::show_clock() {
    if (flow.mode != 0x09 && flow.act == 2) {
        return;
    }
    const auto rlc = [](u8 v) {
        return u8((v << 1) | (v >> 7));
    };
    const auto rrc3 = [](u8 v) {
        return u8((v >> 3) | (v << 5));
    };
    sprites.table[60].tile = u8((rlc(score.minutes) & 0x1E) + 0x2E);
    sprites.table[62].tile = u8((rrc3(score.seconds) & 0x1E) + 0x2E);
    sprites.table[63].tile = u8((rlc(score.seconds) & 0x1E) + 0x2E);
    if (flow.mode == 0x11) {
        throw Unrecovered("the clock's hundredths in mode 11 (306B)");
    }
}

} // namespace triple_trouble_gg
