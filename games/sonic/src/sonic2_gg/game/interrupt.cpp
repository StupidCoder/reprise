// The frame interrupt (04B7-05BE) and the routines only it calls.

#include "sonic2_gg/game/game.hpp"
#include <algorithm>

namespace sonic2_gg {

// 05E8: the line interrupt at the water line switches all colours to the
// underwater palette (table 061A) for the rest of the picture and leaves
// D4E5 set, so the next frame interrupt uploads the normal colours again;
// it also writes FF to register 10, so no further line interrupt comes
// until the frame interrupt writes D132 again.
//
// The VDP reloads its line counter from register 10 on the lines after
// the picture (193-261) and counts it down on lines 0-192: a value of N
// fires at the end of line N, so 0-191 split the picture, 192 fires right
// after the next frame interrupt (that whole picture is underwater, and
// the FF written then reaches the reload, so that frame has no split),
// and larger values never fire (the handler's check for line FF is
// unreachable).
//
// Hypothesis: within a frame the reconstruction applies the interrupt
// after the main program, assuming the program does not touch D4E5 after
// the water line.
void Game::water_line_interrupt() {
    if (water_line_ < 0 || water_line_ > 192 || !display.line_interrupt) {
        return; // not armed, below the picture, or line interrupts disabled
    }
    if (water_line_ == 192) {
        water_line_late_ = true;
        return;
    }
    Display::ColorChange change{water_line_ + 1, {}};
    const auto colors = rom.bytes(rom::underwater_colors, 64);
    std::copy(colors.begin(), colors.end(), change.colors.begin());
    display.changes.push_back(change);
    palette.dirty = rom.byte(rom::underwater_colors, 63);
}

// The line 192 case, run right after the frame interrupt handler.
void Game::water_line_after_frame_interrupt() {
    water_line_fired_late_ = water_line_late_;
    if (!water_line_late_) {
        return;
    }
    water_line_late_ = false;
    water_line_ = -1;
    const auto colors = rom.bytes(rom::underwater_colors, 64);
    std::copy(colors.begin(), colors.end(), display.colors.begin());
    palette.dirty = rom.byte(rom::underwater_colors, 63);
}

// 04B7: runs at the start of every displayed frame (vertical blank).
void Game::frame_interrupt() {
    // 04C4: the water line interrupt is re-armed every frame.
    water_line_ = -1;
    if (video.line_interrupt) {
        water_line_ = video.line_interrupt_line;
    }
    if (video.irq_colors_only) {
        upload_colors(); // 05BF
    } else if (!video.skip_video) {
        display_off();
        // 04E4: screen shake offsets the scroll registers (table 05E0).
        std::uint8_t dx = 0, dy = 0;
        if (camera.shake_ticks) {
            --camera.shake_ticks;
            const unsigned e = camera.shake_ticks & 6;
            dy = rom.byte(rom::shake_offsets, e);
            dx = rom.byte(rom::shake_offsets, e + 1);
        }
        display.scroll_x = u8(camera.scroll_x + dx);
        display.scroll_y = u8(camera.scroll_y + dy);
        // 0512: the camera position the main program prepared this frame
        // takes effect (the original uploads the background rows and columns
        // prepared for it here, 57EB / 573F).
        if (camera.map_flags & map_flag::update_pending) {
            camera.map_flags &= u8(~map_flag::update_pending);
            camera.x = camera.target_x;
            camera.y = camera.target_y;
        }
        upload_sprites();      // 144B
        animate_tiles();       // 7EAF
        display_on();          // 13C2
        upload_colors();       // 130A
        read_controller();     // 1A9D
        upload_player_tiles(); // 10F3
        if (!(flow.game_flags & game_flag::paused)) {
            run_fades(); // bank 9, 8000
            if ((flow.game_flags & game_flag::playing) &&
                !(flow.game_flags & game_flag::act_start)) {
                if (!video.line_interrupt) {
                    build_sprites();      // bank 1F, 1821
                    check_player_tiles(); // 12A8
                }
                run_palette_cycles(); // 7BDD (tables in bank 1E)
            }
        }
    }
    // 0584: sound, the demo's Start check, the clock and the frame counters.
    sound_tick();
    if (flow.attract_flags) {
        check_demo_abort();
    }
    update_timer();
    ++video.frame_counter;
    ++video.frame_done;
}

// 1A9D: shifts the two 16-entry histories and reads the controller. In demos
// (D292 bit 3) the recorded input replaces it (1AE6).
void Game::read_controller() {
    for (int i = 15; i > 0; --i) {
        input.held[i] = input.held[i - 1];
        input.pressed[i] = input.pressed[i - 1];
    }
    // 1AD9: port DC (active low, buttons) with bit 7 replaced by Start
    // (port 00 bit 7); then complemented.
    const std::uint8_t held = buttons_;
    input.held[0] = held;
    const std::uint8_t pressed = std::uint8_t((held & 0xBF) & ~input.held[1]);
    input.pressed[0] = pressed;
    input.pressed_latest = pressed;
    if (flow.attract_flags & attract_flag::demo) {
        // 1AE6: the demo bank holds pairs (held, pressed).
        if (flow.demo_input) {
            input.held[0] = rom.byte(flow.demo_bank, flow.demo_input);
            input.pressed[0] = rom.byte(flow.demo_bank, flow.demo_input + 1);
            flow.demo_input += 2;
        }
    }
}

// 144B: the sprite table goes to the display. On odd frames the order of
// sprites 8-63 is reversed, so that when more than eight share a line,
// different ones drop out on alternate frames on the hardware (and
// overlapping sprites take turns in front).
void Game::upload_sprites() {
    if (!sprites.dirty) {
        return;
    }
    sprites.dirty = 0;
    const bool reversed = video.frame_counter & 1; // 15F9
    for (unsigned k = 0; k < 64; ++k) {
        const unsigned i = reversed && k >= 8 ? 71 - k : k;
        const auto& slot = sprites.table[i];
        auto& shown = display.sprites[k];
        shown = {slot.y, slot.x, slot.tile};
        shown.entry = u8(i);
        // The full position holds while the entry has the bytes it was
        // placed with (other code moves some entries by their bytes).
        shown.placed = slot.placed && slot.y == slot.placed_y && slot.x == slot.placed_x;
        if (shown.placed) {
            shown.full_x = slot.full_x;
            shown.full_y = slot.full_y;
            shown.repeat = slot.repeat;
            shown.owner = slot.owner;
        }
    }
}

// 130A: the colour shadow goes to CRAM when marked changed.
void Game::upload_colors() {
    if (!palette.dirty) {
        return;
    }
    display.colors = palette.colors;
    palette.dirty = 0;
}

// 7EAF: every eighth frame, one of six frames of animated tiles (bank 1D,
// 128 bytes each, from the level's table at D395) goes to D397.
void Game::animate_tiles() {
    if (video.frame_counter & 7) {
        return;
    }
    unsigned phase = tiles.animated_phase + 1;
    if (phase >= 6) {
        phase = 0;
    }
    tiles.animated_phase = std::uint8_t(phase);
    const unsigned source = level.animated_tiles_source + phase * 128;
    if (!level.animated_tiles_vram) {
        return;
    }
    display.put_art(level.animated_tiles_vram, rom.bytes(0x1D, source, 128));
}

// Bank 9, 8000: palette fades. Every D2C8 frames, each of the two fade
// controllers at D4E1 (background) and D4E3 (sprites) advances one step.
void Game::run_fades() {
    if (++palette.fade_counter <= palette.fade_speed) {
        return;
    }
    palette.fade_counter = 0;
    for (int i = 0; i < 2; ++i) {
        if (palette.fades[i * 2]) {
            fade_step(i);
            palette.dirty = 0xFF;
        }
    }
}

// Bank 9, 8036. The controller byte is a mode in bits 5-7 and a step count in
// the low bits; the next byte names a 32-byte palette at bank 9, AE45.
//   bit 7  fade in: each channel is (target - (15 - step)), clamped at 0,
//          for steps 0-15
//   bit 6  fade out: every channel of the current colours loses 1 per step,
//          15 steps
//   bit 5  set the palette at once
void Game::fade_step(int index) {
    u8& control = palette.fades[index * 2];
    const auto table = rom::fade_palettes + palette.fades[index * 2 + 1] * 32u;
    u8* colors = &palette.colors[index * 32];
    if (control & 0x80) {
        const int step = control & 15;
        for (int c = 0; c < 16; ++c) {
            const std::uint8_t lo = rom.byte(table, c * 2);
            const std::uint8_t hi = rom.byte(table, c * 2 + 1);
            auto channel = [&](int target) {
                const int v = step - (target ^ 15);
                return v < 0 ? 0 : v;
            };
            colors[c * 2] = std::uint8_t((channel(lo >> 4) << 4) | channel(lo & 15));
            colors[c * 2 + 1] = std::uint8_t(channel(hi & 15));
        }
        ++control;
        if ((control & 0x1F) == 0x10) {
            control = 0;
        }
    } else if (control & 0x40) {
        for (int c = 0; c < 16; ++c) {
            u8& lo = colors[c * 2];
            if (lo & 0xF0) {
                lo = std::uint8_t(((lo & 0xF0) - 0x10) | (lo & 0x0F));
            }
            if (lo & 0x0F) {
                lo = std::uint8_t((lo & 0xF0) | ((lo & 0x0F) - 1));
            }
            u8& hi = colors[c * 2 + 1];
            if (hi & 0x0F) {
                hi = std::uint8_t((hi & 0x0F) - 1);
            }
        }
        ++control;
        if ((control & 0x0F) == 0x0F) {
            control = 0;
        }
    } else if (control & 0x20) {
        for (int i = 0; i < 32; ++i) {
            colors[i] = rom.byte(table, unsigned(i));
        }
        control = 0;
    }
}

// 20FD: the act clock, counting seconds (BCD) up to 9:59.
void Game::update_timer() {
    if (!clock.running) {
        return;
    }
    if (++clock.ticks != 60) {
        return;
    }
    clock.ticks = 0;
    const auto add_bcd = [](u8 v) {
        unsigned r = v + 1;
        if ((r & 0x0F) > 9) {
            r += 6;
        }
        return u8(r);
    };
    clock.seconds = add_bcd(clock.seconds);
    if (clock.seconds != 0x60) {
        update_timer_digits(); // 212E
        return;
    }
    clock.seconds = 0;
    clock.minutes = add_bcd(clock.minutes);
    if (clock.minutes != 0x10) {
        update_timer_digits();
        return;
    }
    // 2124: ten minutes: time over.
    clock.time_over = 0xFF;
    clock.running = 0;
}

// 05D4: in the attract demo, Start (D157 bit 7) aborts it.
void Game::check_demo_abort() {
    if (input.pressed_latest & 0x80) {
        flow.attract_flags |= attract_flag::start;
    }
}

} // namespace sonic2_gg
