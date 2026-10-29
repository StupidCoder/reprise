// Palette cycles (7BDD-7EAE) and the flashing ring colours (7E0F).

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// 7BDD: two palette cycle controllers, D481 and D489, run unless a fade is
// active. Their tables are in bank 1E.
void Game::run_palette_cycles() {
    if (palette.fades[0]) {
        return;
    }
    palette_cycle(palette.cycle1.data());
    palette_cycle(palette.cycle2.data());
}

// 7BF5. A controller is {kind, -, phase, ticks, counter (word)}; each kind
// copies colours from a table into the colour shadow every few frames.
void Game::palette_cycle(u8* c) {
    u8& kind = c[0];
    u8& phase = c[2];
    u8& ticks = c[3];
    // Advances `ticks`; on reaching `period` resets it and steps the phase
    // through `phases` values. Returns the new phase, or -1 meanwhile.
    auto step = [&](int period, int phases) {
        if (++ticks < period) {
            return -1;
        }
        ticks = 0;
        int p = phase + 1;
        if (p >= phases) {
            p = 0;
        }
        phase = u8(p);
        return p;
    };
    // `count` bytes of colours from `table` from colour byte `first` on.
    auto copy = [&](Table table, unsigned first, int count) {
        for (int i = 0; i < count; ++i) {
            palette.colors[first + unsigned(i)] = rom.byte(table, unsigned(i));
        }
        palette.dirty = 0xFF;
    };
    int p;
    switch (kind) {
    case 2: // 7C35
        if ((p = step(4, 3)) >= 0) {
            copy(rom::palette_cycle_2 + unsigned(p) * 6, 8, 6);
        }
        break;
    case 3: // 7C67: after 120 frames, 16 frames of alternating colour 11
        if (++ticks < 0x78) {
            break;
        }
        ++phase;
        palette.colors[22] = (phase & 2) ? 0xF0 : 0x00; // D4B7 = 0FF0 / 0400
        palette.colors[23] = (phase & 2) ? 0x0F : 0x04;
        palette.dirty = 0xFF;
        if (phase >= 0x10) {
            ticks = 0;
            phase = 0;
        }
        break;
    case 4: // 7C99
        if ((p = step(8, 3)) >= 0) {
            copy(rom::palette_cycle_4 + unsigned(p) * 6, 26, 6);
        }
        break;
    case 5: // 7CCB
        if ((p = step(8, 3)) >= 0) {
            copy(rom::palette_cycle_5 + unsigned(p) * 6, 26, 6);
        }
        break;
    case 6: // 7D2F
        if ((p = step(4, 12)) >= 0) {
            copy(rom::palette_cycle_6 + unsigned(p) * 4, 22, 4);
        }
        break;
    case 7: // 7CFD
        if ((p = step(4, 3)) >= 0) {
            copy(rom::palette_cycle_7 + unsigned(p) * 6, 26, 6);
        }
        break;
    case 8:  // 7D8F
    case 12: // (same routine)
        if ((p = step(4, 14)) >= 0) {
            copy(rom::palette_cycle_8 + unsigned(p) * 4, 26, 4);
        }
        break;
    case 9: // 7DBF
        if ((p = step(2, 3)) >= 0) {
            copy(rom::palette_sets_9 + unsigned(p) * 32, 0, 32);
        }
        break;
    case 10: // 7DF2: restore the first set once and stop
        copy(rom::palette_sets_9, 0, 32);
        kind = 0;
        ticks = 0;
        phase = 0;
        break;
    case 11: // 7D5F
        if ((p = step(4, 6)) >= 0) {
            copy(rom::palette_cycle_11 + unsigned(p) * 2, 8, 2);
        }
        break;
    case 13: { // 7E40: every 640 frames switch to the next of ten palettes
        unsigned counter = unsigned(c[4] | (c[5] << 8)) + 1;
        c[4] = u8(counter);
        c[5] = u8(counter >> 8);
        if (counter < 0x280) {
            break;
        }
        c[4] = c[5] = 0;
        if (++phase >= 10) {
            kind = 0;
            phase = 0;
            break;
        }
        palette.fades[0] |= 0x20;
        palette.fades[1] = u8(phase + 0x25);
        break;
    }
    case 14: // 7E78
        if ((p = step(6, 6)) >= 0) {
            const auto t = rom::palette_cycle_14 + unsigned(p) * 4;
            palette.colors[18] = rom.byte(t);
            palette.colors[19] = rom.byte(t, 1);
            palette.colors[30] = rom.byte(t, 2);
            palette.colors[31] = rom.byte(t, 3);
            palette.dirty = 0xFF;
        }
        break;
    default: // 0, 1, 15: nothing
        break;
    }
}

// 7E0F: every fourth frame, colours 9-14 alternate between two sets.
void Game::flash_ring_colors() {
    if (video.frame_counter & 3) {
        return;
    }
    const auto table = (video.frame_counter & 4) ? rom::ring_flash_b : rom::ring_flash_a;
    for (int i = 0; i < 12; ++i) {
        palette.colors[18 + i] = rom.byte(table, unsigned(i));
    }
    palette.dirty = 0xFF;
}

} // namespace sonic2_gg
