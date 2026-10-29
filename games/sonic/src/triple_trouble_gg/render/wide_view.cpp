// Widescreen enhancement, presentation (see wide_view.hpp).
//
// The Game Gear's picture is 256x192 around a 32x28-cell background, so it
// cannot show more than the area around the LCD crop. The expanded view is
// drawn the same way over a larger area: the background from the act's map
// (Game::level_entry), the tile art and colours of the display, and the
// sprites at the full positions the game placed them at (Display::Sprite);
// the HUD is moved to the view's corner and drawn over everything, after
// the arena force field.
//
// Within the LCD crop the result is the original view, apart from the moved
// HUD. Beyond it:
//   - Lines whose horizontal scroll differs from the picture's (Sunset Park
//     act 3's bands) show the background map as the chip does, repeating
//     every 256 pixels; lines above and below the picture continue the
//     scroll of its first and last line.
//   - In acts with water the original only splits the colours at the
//     surface inside the picture (05A7); outside the LCD lines the view
//     uses the normal colours above the surface and the underwater ones
//     below it.

#include "triple_trouble_gg/render/wide_view.hpp"
#include "triple_trouble_gg/enhancements/widescreen.hpp"
#include "triple_trouble_gg/game/game.hpp"
#include <algorithm>

namespace triple_trouble_gg {

namespace {

// One pixel of a 4-plane tile row: four bytes, bit 7 is the leftmost pixel.
inline unsigned row_pixel(const std::array<u8, 4>& row, int x) {
    const int bit = 7 - x;
    return ((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1) | (((row[2] >> bit) & 1) << 2) |
           (((row[3] >> bit) & 1) << 3);
}

// The HUD: entries 51-63 (1B:BD12), unless DBC3 gave them to objects.
bool hud_entry(const Game& g, unsigned entry) {
    return entry >= 51 && !g.sprites.hud_small;
}

} // namespace

void set_game_view(Game& g, int width, int height) {
    g.view_width = width;
    g.view_height = height;
}

bool wide_view_active(const Game& g) {
    return g.widescreen() && g.wide_play();
}

gg::Screen render_frame(const Game& g, int width, int height, const ArenaBarrier* barrier) {
    if (!wide_view_active(g) || (width <= 160 && height <= 144)) {
        return g.picture();
    }
    const auto& d = g.display;
    const auto level = g.level_background();
    // The view in picture coordinates, as the game's margins were set.
    const int x0 = lcd_left - g.wide_left, y0 = lcd_top - g.wide_top;
    width = g.wide_left + 160 + g.wide_right;
    height = g.wide_top + 144 + g.wide_bottom;
    gg::Screen image(width, height);
    // The world position of the picture's top left corner.
    const int world_x = g.picture_x(), world_y = g.picture_y();

    // Colours: CRAM, then the changes made during the picture (the water
    // line), which hold for the lines below them.
    std::vector<Display::ColorChange> changes = d.changes;
    std::stable_sort(changes.begin(), changes.end(),
                     [](const auto& a, const auto& b) { return a.line < b.line; });
    const bool water = g.act.water_level && !(g.flow.zone == 4 && g.flow.act == 2);
    const int surface_line = int(g.act.water_level) - world_y + 18; // 05A7: the line after
    const auto dry_colors = gg::decode_palette(g.palette.colors);
    const auto wet_colors = gg::decode_palette(g.palette.water_colors);

    constexpr int sprite_height = 16;

    struct Sprite {
        int x, y;
        unsigned tile;
    };

    std::vector<Sprite> sprites, hud;
    for (const auto& s : d.sprites) {
        if (s.y == 0xD0) {
            break; // The end of the table: the chip draws no further sprites.
        }
        const unsigned tile = s.tile & 0xFEu;
        if (s.placed) {
            if (s.full_y + 1 >= y0 + height || s.full_y + 1 + sprite_height <= y0) {
                continue;
            }
            sprites.push_back({s.full_x, s.full_y + 1, tile});
            continue;
        }
        int y = int(s.y) + 1;
        if (y > 0xF0) {
            y -= 256;
        }
        if (y >= 192) {
            continue; // Below the picture: hidden.
        }
        if (hud_entry(g, s.entry)) {
            // The HUD keeps its distance from the view's corner and is
            // drawn over everything: there, priority tiles would hide it.
            hud.push_back({int(s.x) - g.wide_left, y - g.wide_top, tile});
            continue;
        }
        sprites.push_back({int(s.x), y, tile});
    }

    const auto columns = std::size_t(width);
    std::vector<gg::Color> overlay(columns * std::size_t(height));
    std::vector<bool> overlaid(overlay.size());
    std::vector<u8> bg(columns), sp(columns);
    std::vector<bool> bg_priority(columns);
    auto colors = gg::decode_palette(d.colors);
    std::size_t next_change = 0, next_scroll = 0;
    int scroll_x = d.scroll_x;
    for (int v = 0; v < height; ++v) {
        const int line = y0 + v;
        while (next_change < changes.size() && changes[next_change].line <= line) {
            colors = gg::decode_palette(changes[next_change++].colors);
        }
        while (next_scroll < d.scroll_changes.size() &&
               d.scroll_changes[next_scroll].line <= line) {
            scroll_x = d.scroll_changes[next_scroll++].scroll_x;
        }
        const bool lcd_line = line >= lcd_top && line < lcd_top + 144;
        const auto& palette =
            water && !lcd_line ? (line < surface_line ? dry_colors : wet_colors) : colors;
        if (!d.shown) {
            for (int u = 0; u < width; ++u) {
                image.put(u, v, palette[16]);
            }
            continue;
        }

        // Background: from the map, or on band lines as the chip shows its
        // 256-pixel map.
        const bool band = scroll_x != d.scroll_x;
        const int wy = world_y + line;
        const int py = ((line + int(d.scroll_y)) % 224 + 224) % 224;
        unsigned entry = 0;
        int cell = -1;
        for (int u = 0; u < width; ++u) {
            const int column = x0 + u;
            int tx, ty;
            if (band) {
                const int px = (column - scroll_x) & 255;
                if ((px >> 3) != cell) {
                    cell = px >> 3;
                    entry = d.cell(px >> 3, py >> 3, level);
                }
                tx = px & 7;
                ty = py & 7;
            } else {
                const int wx = world_x + column;
                if ((wx >> 3) != cell) {
                    cell = wx >> 3;
                    entry = g.level_entry(wx, wy);
                }
                tx = wx & 7;
                ty = wy & 7;
            }
            if (entry & 0x200) {
                tx = 7 - tx;
            }
            if (entry & 0x400) {
                ty = 7 - ty;
            }
            const auto index = row_pixel(d.tile_row(entry & 0x1FF, unsigned(ty), level), tx);
            bg[std::size_t(u)] = u8(index | ((entry & 0x800) ? 16 : 0));
            bg_priority[std::size_t(u)] = (entry & 0x1000) && index;
        }

        // Sprites, earlier entries in front, without the per-line limit.
        auto draw_sprites = [&](const std::vector<Sprite>& list) {
            std::fill(sp.begin(), sp.end(), 0);
            for (const auto& s : list) {
                if (line < s.y || line >= s.y + sprite_height) {
                    continue;
                }
                const unsigned row = unsigned(line - s.y);
                const auto bytes = d.tile_row(s.tile + row / 8, row % 8, level);
                for (int p = 0; p < 8; ++p) {
                    const int u = s.x + p - x0;
                    if (u < 0 || u >= width || sp[std::size_t(u)]) {
                        continue;
                    }
                    if (const auto index = row_pixel(bytes, p)) {
                        sp[std::size_t(u)] = u8(16 + index);
                    }
                }
            }
        };
        draw_sprites(sprites);
        for (int u = 0; u < width; ++u) {
            const auto i = std::size_t(u);
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
    if (barrier) {
        // Object coordinates: sprites are drawn one line below their Y.
        barrier->draw(image, int(g.camera.x) + x0, int(g.camera.y) + y0 - 1);
    }
    for (std::size_t i = 0; i < overlay.size(); ++i) {
        if (overlaid[i]) {
            image.pixels[i] = overlay[i];
        }
    }
    return image;
}

} // namespace triple_trouble_gg
