// Colours: their upload and the fades (bank 0E, B4D3).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 0E:B6FC: the palettes, 16 colours (32 bytes) each.
constexpr Table palettes{0x0E, 0xB6FC};
} // namespace

// 2582: the colours go to the chip when changed (not while the underwater
// colours are being uploaded, DBCA).
void Game::upload_colors() {
    if (!palette.dirty) {
        return;
    }
    if (!palette.water_colors_shown) {
        display.colors = palette.colors;
    }
    palette.dirty = 0;
}

// 340C: all 32 colours white (the start of a fade in from white).
void Game::fill_white() {
    for (unsigned i = 0; i < 64; i += 2) {
        palette.colors[i] = 0xFF;
        palette.colors[i + 1] = 0x0F;
    }
}

// The program's usual way to start fades: the step period (D4FE) and both
// halves' fade mode and palette (D4F6-D4F9).
void Game::set_fades(u8 period, u8 bg_mode, u8 bg_palette, u8 sprite_mode, u8 sprite_palette) {
    palette.fade_period = period;
    palette.fades[0] = {bg_mode, bg_palette};
    palette.fades[1] = {sprite_mode, sprite_palette};
}

// B4D3: every D4FE+1 frames each running fade takes a step: the background
// colours (D4B3) and the sprite colours (D4D3), then with water in the act
// the two halves of the underwater colours (DC58).
void Game::run_fades() {
    if (palette.fade_period >= ++palette.fade_counter) {
        return;
    }
    palette.fade_counter = 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (palette.fades[i].mode) {
            fade_step(palette.fades[i], std::span<u8>(palette.colors).subspan(i * 32, 32));
            palette.dirty = 0xFF;
        }
    }
    if (!act.water_level) {
        return;
    }
    // B50E: the underwater colours' fades (D4FA, D4FC; DC58, DC78).
    for (unsigned i = 0; i < 2; ++i) {
        if (palette.water_fades[i].mode) {
            fade_step(palette.water_fades[i],
                      std::span<u8>(palette.water_colors).subspan(i * 32, 32));
            palette.water_dirty = 0xFF;
        }
    }
}

// B539: one step of a fade. The mode's bits: 7 fade in toward the palette
// (from black, or with bit 4 from white), 6 fade out (to black, or with bit
// 4 to white), 5 the palette at once; the low bits count the steps.
void Game::fade_step(PaletteState::Fade& fade, std::span<u8> colors) {
    const u8 mode = fade.mode;
    const unsigned base = fade.palette * 32u; // B6EC
    auto target = [&](unsigned i) {
        return rom.byte(palettes, base + i);
    };
    if (mode & 0x80) {
        if (mode & 0x10) {
            // B610: each channel one step down toward the palette (from a
            // channel below the palette it keeps going, wrapping at 0).
            for (unsigned c = 0; c < 32; c += 2) {
                u8 v = colors[c];
                if ((v & 0x0F) != (target(c) & 0x0F)) {
                    v = u8(((v - 0x01) & 0x0F) | (v & 0xF0));
                }
                if ((v & 0xF0) != (target(c) & 0xF0)) {
                    v = u8(((v - 0x10) & 0xF0) | (v & 0x0F));
                }
                colors[c] = v;
                const u8 b = colors[c + 1];
                if ((b & 0x0F) != (target(c + 1) & 0x0F)) {
                    colors[c + 1] = u8((b - 1) & 0x0F);
                }
            }
            const u8 step = u8((mode & 0x0F) + 1);
            fade.mode = step == 0x10 ? 0 : u8(step | (mode & 0xF0));
            return;
        }
        // B55C: from black: each channel is the palette's minus (15 - step),
        // at least 0.
        const int step = mode & 0x0F;
        auto channel = [&](unsigned value) {
            const int v = step - int(value ^ 0x0F);
            return unsigned(v < 0 ? 0 : v);
        };
        for (unsigned c = 0; c < 32; c += 2) {
            const u8 t = target(c);
            colors[c] = u8((channel(t >> 4) << 4 & 0xF0) | (channel(t & 0x0F) & 0x0F));
            colors[c + 1] = u8(channel(target(c + 1) & 0x0F) & 0x0F);
        }
        fade.mode = u8(mode + 1);
        if ((fade.mode & 0x1F) == 0x10) {
            fade.mode = 0;
        }
        return;
    }
    if (mode & 0x40) {
        if (mode & 0x10) {
            // B689: each channel one step up toward white.
            for (unsigned c = 0; c < 32; c += 2) {
                u8 v = colors[c];
                if ((v & 0x0F) != 0x0F) {
                    v = u8(((v + 0x01) & 0x0F) | (v & 0xF0));
                }
                if ((v & 0xF0) != 0xF0) {
                    v = u8(((v + 0x10) & 0xF0) | (v & 0x0F));
                }
                colors[c] = v;
                u8 b = u8(colors[c + 1] & 0x0F);
                if (b != 0x0F) {
                    b = u8((b + 1) & 0x0F);
                }
                colors[c + 1] = b;
            }
            const u8 step = u8(((mode & 0x0F) + 1) & 0x0F);
            fade.mode = u8(step | (mode & 0xF0));
            if (step == 0x0F) {
                fade.mode = 0;
            }
            return;
        }
        // B5C2: each channel one step down toward black.
        for (unsigned c = 0; c < 32; c += 2) {
            u8 v = colors[c];
            if (v & 0xF0) {
                v = u8(((v - 0x10) & 0xF0) | (v & 0x0F));
            }
            if (v & 0x0F) {
                v = u8((v & 0xF0) | ((v & 0x0F) - 1));
            }
            colors[c] = v;
            if (colors[c + 1] & 0x0F) {
                colors[c + 1] = u8((colors[c + 1] & 0x0F) - 1);
            }
        }
        fade.mode = u8(mode + 1);
        if ((fade.mode & 0x0F) == 0x0F) {
            fade.mode = 0;
        }
        return;
    }
    if (mode & 0x20) {
        // B547
        for (unsigned i = 0; i < 32; ++i) {
            colors[i] = target(i);
        }
        fade.mode = 0;
    }
}

} // namespace triple_trouble_gg
