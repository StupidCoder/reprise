// Widescreen enhancement, presentation (see wide_view.hpp).
//
// The Game Gear's picture is 256x192 around a 32x28-cell background, so it
// cannot show more than the area around the LCD crop. The expanded view is
// drawn the same way over a larger area: the background from the level map
// (Game::level_background), the tile art and colours of the display, and the
// sprites at the full positions the game placed them at (Display::Sprite);
// the HUD is moved to the corners of the view and drawn over everything,
// after the arena force field.
//
// Within the LCD crop the result is the original view, apart from the moved
// HUD.
//
// The water enhancement uses the same drawing at any size, the LCD's
// included, and changes it as described in enhancements/water.hpp.

#include "sonic2_gg/render/wide_view.hpp"
#include "sonic2_gg/enhancements/water.hpp"
#include "sonic2_gg/enhancements/widescreen.hpp"
#include "sonic2_gg/game/game.hpp"
#include <algorithm>
#include <optional>

namespace sonic2_gg {

namespace {

// One pixel of a 4-plane tile row: four bytes, bit 7 is the leftmost pixel.
inline unsigned row_pixel(const std::uint8_t* row, int x) {
    const int bit = 7 - x;
    return ((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1) | (((row[2] >> bit) & 1) << 2) |
           (((row[3] >> bit) & 1) << 3);
}

// The HUD: the twelve fixed entries at the end of the table (224A).
bool hud_entry(unsigned entry) {
    return entry >= 52;
}

} // namespace

// The block from level_map via the row table, then the 8x8 cell's entry in
// the block's 4x4 entries.
unsigned map_entry(const Game& g, int x, int y) {
    return g.level_entry(x, y);
}

void set_game_view(Game& g, int width, int height) {
    g.view_width = width;
    g.view_height = height;
}

bool wide_play(const Game& g) {
    // D293: in game (bit 6), not the title (7) or loading (2: title cards,
    // tallies); the map enabled; not an attract demo, not the ending.
    const auto& r = g;
    return !r.flow.attract_flags &&
           (r.flow.game_flags & (game_flag::scene | game_flag::playing | game_flag::act_start)) ==
               game_flag::playing &&
           (r.camera.map_flags & map_flag::enabled) && r.flow.zone < 7;
}

bool wide_view_active(const Game& g) {
    return g.widescreen() && wide_play(g);
}

gg::Screen render_frame(const Game& g, int width, int height, const ArenaBarrier* barrier,
                        const WaterEffects* water) {
    if (!(water && enhanced_water_act(g))) {
        water = nullptr;
    }
    if (!water && (!wide_view_active(g) || (width <= 160 && height <= 144))) {
        return g.picture();
    }
    const auto& d = g.display;
    const auto level = g.level_background();
    const auto& r = g;
    // The view in VDP picture coordinates, as the game's margins were set.
    const int x0 = lcd_left - g.wide_left, y0 = lcd_top - g.wide_top;
    width = g.wide_left + 160 + g.wide_right;
    height = g.wide_top + 144 + g.wide_bottom;
    gg::Screen image(width, height);

    // The camera the scroll was set for (4AE1), including any screen shake
    // (04E4): the horizontal scroll is -(camera X + 1), the vertical one
    // camera Y + 17 within the 224-line background map.
    const int camera_x = lift(-int(d.scroll_x) - 1, 256, int(r.camera.x));
    const int camera_y = lift(int(d.scroll_y) - 17, 224, int(r.camera.y));

    // Colours: CRAM, then the changes made during the picture (the water
    // line), which hold for the lines below it.
    auto colors = gg::decode_palette(d.colors);
    std::vector<Display::ColorChange> changes = d.changes;
    std::stable_sort(changes.begin(), changes.end(),
                     [](const auto& a, const auto& b) { return a.line < b.line; });
    // Widescreen: B4E0 only arms the water line inside the picture; a
    // surface further down, inside the larger view, splits it there. That
    // includes line 192, whose interrupt only fires after the next frame
    // interrupt: without this the view below it would flash dry colours.
    const bool rising_water = r.flow.zone == 2 && r.flow.act == 1 && r.water.rise_y;
    if (rising_water && r.video.line_interrupt && r.video.line_interrupt_line >= 192 &&
        changes.empty()) {
        Display::ColorChange c{r.video.line_interrupt_line + 1, {}};
        const auto bytes = g.rom.bytes(rom::underwater_colors, 64);
        std::copy(bytes.begin(), bytes.end(), c.colors.begin());
        changes.push_back(c);
    }
    // Widescreen: with the camera under the surface (B527) the whole
    // picture uses the underwater colours; above the surface, inside the
    // larger view, the normal ones (palettes 12/08, bank 9 AE45) apply.
    // The same split replaces the frame after a line-192 interrupt, which
    // the VDP shows entirely in underwater colours: this view differs from
    // the LCD there, rather than flashing for one frame.
    const int surface_line = int(r.water.rise_y) - camera_y;
    std::vector<std::uint8_t> above(64);
    const bool split_above =
        rising_water && (!r.video.line_interrupt || g.water_line_fired_late()) && surface_line > y0;
    if (split_above) {
        for (unsigned i = 0; i < 32; ++i) {
            above[i] = g.rom.byte(rom::fade_palettes, 0x12 * 32 + i);
            above[32 + i] = g.rom.byte(rom::fade_palettes, 0x08 * 32 + i);
        }
    }
    const auto above_colors = gg::decode_palette(above);

    // Water enhancement: the dithered water tiles, and what the layer over
    // the picture needs (water.hpp).
    std::optional<WaterArt> art;
    WaterLayers layers;
    if (water) {
        if (r.flow.zone == 2) {
            art.emplace(d);
        }
        layers.width = width;
        layers.height = height;
        layers.view_x = camera_x + 1 + x0;
        layers.view_y = camera_y + 17 + y0;
        layers.flags.assign(std::size_t(width * height), 0);
        layers.palettes.resize(std::size_t(height));
        if (rising_water) {
            // The colours change from the line after the interrupt's line
            // (D47F - camera Y).
            layers.surface_row = int(r.water.rise_y) - int(r.camera.y) + 1 - y0;
        }
    }

    constexpr int sprite_height = 16, shift = 0;

    // Sprites in front-to-back order with their picture positions.
    struct Sprite {
        int x, y;
        unsigned tile;
    };

    std::vector<Sprite> sprites, hud;
    for (int i = 0; i < 64; ++i) {
        const auto& s = d.sprites[std::size_t(i)];
        const unsigned table_y = d.sprites[std::size_t(i)].y;
        const int table_x = d.sprites[std::size_t(i)].x;
        const unsigned tile = d.sprites[std::size_t(i)].tile & 0xFEu;
        // Water enhancement: the splash and wave sprites and the rain drops
        // are replaced (the rain by drops on the lens, water.hpp); a drop
        // no longer ends the table early (below).
        if (water && rain_entry(g, s.entry)) {
            continue;
        }
        if (table_y == 0xD0) {
            // The end of the table: the VDP draws no further sprites. During
            // play only a rain drop 48 pixels above the camera ends it early.
            break;
        }
        if (water && s.placed && replaced_by_water(g, s.owner)) {
            continue;
        }
        if (s.placed) {
            if (s.full_y + 1 >= y0 + height || s.full_y + 1 + sprite_height <= y0) {
                continue;
            }
            if (!s.repeat) {
                sprites.push_back({s.full_x - shift, s.full_y + 1, tile});
                continue;
            }
            // Rain falls everywhere: drops are repeated every 256 pixels
            // outside the area the game moves them in (-64..255, 6942).
            for (int k = -2; k <= 2; ++k) {
                const int x = s.full_x + k * 256;
                if (k == 0 || x < -64 || x >= 256) {
                    sprites.push_back({x - shift, s.full_y + 1, tile});
                }
            }
            continue;
        }
        int y = int(table_y) + 1;
        if (y > 0xF0) {
            y -= 256;
        }
        if (y >= 192) {
            continue; // Below the picture: hidden.
        }
        int x = table_x - shift;
        if (hud_entry(s.entry)) {
            // The HUD keeps its distance from the view's corner and is drawn
            // over everything: there, priority tiles would hide it.
            x -= g.wide_left;
            y += y < lcd_top + 72 ? -g.wide_top : g.wide_bottom;
            hud.push_back({x, y, tile});
            continue;
        }
        sprites.push_back({x, y, tile});
    }

    const auto columns = std::size_t(width);
    // The HUD's pixels, laid over the picture last.
    std::vector<gg::Color> overlay(columns * std::size_t(height));
    std::vector<bool> overlaid(overlay.size());
    std::vector<std::uint8_t> bg(columns), sp(columns);
    std::vector<bool> bg_priority(columns);
    // Water enhancement, per pixel of the line: inside the water dither,
    // the colour under it and whether the tile has priority over sprites.
    std::vector<std::uint8_t> under(columns);
    std::vector<bool> in_water(columns), tile_front(columns), over_sprites(columns);
    std::size_t next_change = 0;
    for (int v = 0; v < height; ++v) {
        const int line = y0 + v;
        while (next_change < changes.size() && changes[next_change].line <= line) {
            colors = gg::decode_palette(changes[next_change++].colors);
        }
        const auto& palette = split_above && line < surface_line ? above_colors : colors;
        if (water) {
            layers.palettes[std::size_t(v)] = palette;
        }
        const gg::Color backdrop = palette[16];
        if (!d.shown) {
            for (int u = 0; u < width; ++u) {
                image.put(u, v, backdrop);
            }
            continue;
        }

        // Background from the map.
        const int wy = camera_y + 17 + line;
        unsigned entry = 0;
        int cell = -1;
        for (int u = 0; u < width; ++u) {
            const int wx = camera_x + 1 + x0 + u;
            if ((wx >> 3) != cell) {
                cell = wx >> 3;
                entry = map_entry(g, wx, wy);
            }
            int tx = wx & 7, ty = wy & 7;
            if (entry & 0x200) {
                tx = 7 - tx;
            }
            if (entry & 0x400) {
                ty = 7 - ty;
            }
            const auto row_bytes = d.tile_row(entry & 0x1FF, unsigned(ty), level);
            const std::uint8_t* row = row_bytes.data();
            const auto index = row_pixel(row, tx);
            const auto half = (entry & 0x800) ? 16u : 0u;
            bg[std::size_t(u)] = std::uint8_t(index | half);
            bg_priority[std::size_t(u)] = (entry & 0x1000) && index;
            if (art) {
                // The dither's colour-4 pixels hide what the other half
                // shows: the neighbour in the row stands in for it.
                const bool wet = art->water_row(entry, ty);
                const auto shown = wet && index == 4 ? row_pixel(row, tx ^ 1) : index;
                in_water[std::size_t(u)] = wet;
                under[std::size_t(u)] = std::uint8_t(shown | half);
                tile_front[std::size_t(u)] = entry & 0x1000;
                over_sprites[std::size_t(u)] = (entry & 0x1000) && shown;
                layers.flags[std::size_t(v) * columns + std::size_t(u)] =
                    wet ? WaterLayers::water_pixel : 0;
            }
        }

        // Sprites, earlier entries in front, without the per-line limit.
        auto draw_sprites = [&](const std::vector<Sprite>& list) {
            std::fill(sp.begin(), sp.end(), 0);
            for (const auto& s : list) {
                if (line < s.y || line >= s.y + sprite_height) {
                    continue;
                }
                const unsigned line_in = unsigned(line - s.y);
                const auto row_bytes = d.tile_row(s.tile + line_in / 8, line_in % 8, level);
                const std::uint8_t* row = row_bytes.data();
                for (int p = 0; p < 8; ++p) {
                    const int u = s.x + p - x0;
                    if (u < 0 || u >= width || sp[std::size_t(u)]) {
                        continue;
                    }
                    if (const auto index = row_pixel(row, p)) {
                        sp[std::size_t(u)] = std::uint8_t(16 + index);
                    }
                }
            }
        };
        draw_sprites(sprites);
        for (int u = 0; u < width; ++u) {
            const auto i = std::size_t(u);
            if (art && in_water[i]) {
                // Water enhancement: the water's colour over what is under
                // it at half strength (the dither's average) instead of
                // the checkerboard. Sprites in front of the tile stay so.
                if (sp[i] && !tile_front[i]) {
                    image.put(u, v, palette[sp[i]]);
                    continue;
                }
                const bool sprite = sp[i] && !over_sprites[i];
                const auto shown = sprite ? palette[sp[i]] : palette[under[i]];
                const auto wet = palette[(bg[i] & 16) | 4];
                image.put(u, v,
                          {std::uint8_t((shown.r + wet.r + 1) / 2),
                           std::uint8_t((shown.g + wet.g + 1) / 2),
                           std::uint8_t((shown.b + wet.b + 1) / 2), 255});
                continue;
            }
            image.put(u, v, palette[sp[i] && !bg_priority[i] ? sp[i] : bg[i]]);
        }
        if (!hud.empty()) {
            draw_sprites(hud);
            for (int u = 0; u < width; ++u) {
                if (sp[std::size_t(u)]) {
                    const auto i = std::size_t(v) * columns + std::size_t(u);
                    overlay[i] = palette[sp[std::size_t(u)]];
                    overlaid[i] = true;
                }
            }
        }
    }
    if (water) {
        draw_water(image, g, layers, *water);
    }
    if (barrier) {
        // Object coordinates: sprites are drawn one line below their Y.
        barrier->draw(image, int(r.camera.x) + x0, int(r.camera.y) + y0 - 1);
    }
    if (water) {
        // Rain on the lens: in front of everything in the world, behind
        // the HUD.
        water->lens().draw(image);
    }
    for (std::size_t i = 0; i < overlay.size(); ++i) {
        if (overlaid[i]) {
            image.pixels[i] = overlay[i];
        }
    }
    return image;
}

} // namespace sonic2_gg
