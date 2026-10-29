// The picture from the display state (display.hpp).

#include "gg_engine/video/display.hpp"
#include <algorithm>
#include <stdexcept>

namespace gg_engine {

namespace {

// Where the parts the tiles 448-511 show lie, as byte positions counted from
// the start of tile 0 (the cartridge's video memory addresses).
constexpr unsigned map_start = 0x3800, sprite_y_start = 0x3F00, sprite_gap = 0x3F40,
                   sprite_xt_start = 0x3F80, memory_end = 0x4000;

// One pixel of a tile row: four bit planes, bit 7 is the leftmost pixel.
unsigned row_pixel(const Display& d, unsigned tile, unsigned row, int x,
                   const Display::LevelBackground& level) {
    const int bit = 7 - x;
    unsigned index = 0;
    for (unsigned plane = 0; plane < 4; ++plane) {
        index |= ((d.tile_byte(tile, row * 4 + plane, level) >> bit) & 1u) << plane;
    }
    return index;
}

} // namespace

// Art written past tile 447 lands in the background map and the sprite
// table, which share the chip's memory (Triple Trouble's title does this).
void Display::put_byte(unsigned at, u8 value) {
    at %= memory_end;
    if (at < map_start) {
        tiles[at / 32][at % 32] = value;
    } else if (at < sprite_y_start) {
        auto& e = map[(at - map_start) / 2];
        e = (at & 1) ? std::uint16_t((e & 0x00FF) | (value << 8))
                     : std::uint16_t((e & 0xFF00) | value);
    } else if (at < sprite_gap) {
        sprites[at - sprite_y_start].y = value;
    } else if (at < sprite_xt_start) {
        gap[at - sprite_gap] = value;
    } else {
        auto& sp = sprites[(at - sprite_xt_start) / 2];
        ((at & 1) ? sp.tile : sp.x) = value;
    }
}

void Display::put_art(unsigned offset, std::span<const u8> bytes) {
    for (const auto b : bytes) {
        put_byte(offset++, b);
    }
}

void Display::fill_art(unsigned offset, std::size_t count, u8 value) {
    for (std::size_t i = 0; i < count; ++i) {
        put_byte(unsigned(offset + i), value);
    }
}

u8 Display::tile_byte(unsigned tile, unsigned offset, const LevelBackground& level) const {
    const unsigned at = (tile * 32 + offset) % memory_end;
    if (at < map_start) {
        return tiles[at / 32][at % 32];
    }
    if (at < sprite_y_start) {
        const unsigned index = (at - map_start) / 2;
        const auto entry = cell(int(index % map_columns), int(index / map_columns), level);
        return u8((at & 1) ? entry >> 8 : entry);
    }
    if (at < sprite_gap) {
        return sprites[at - sprite_y_start].y;
    }
    if (at < sprite_xt_start) {
        return gap[at - sprite_gap];
    }
    const auto& s = sprites[(at - sprite_xt_start) / 2];
    return (at & 1) ? s.tile : s.x;
}

// The VDP's mode 4 with the games' settings: lines 24-167 and columns
// 48-207 of the 256x192 picture are the LCD; the scroll applies to the whole
// background; sprites are 8x16, earlier entries in front, all of them drawn
// (the hardware's limit of eight per line is not reproduced).
gg::Screen Display::render(const LevelBackground& level) const {
    gg::Screen image(160, 144);
    auto colors = gg::decode_palette(this->colors);
    std::vector<ColorChange> changes = this->changes;
    std::stable_sort(changes.begin(), changes.end(),
                     [](const ColorChange& a, const ColorChange& b) { return a.line < b.line; });
    std::size_t next_change = 0;
    std::size_t next_scroll = 0;
    int scroll_x = this->scroll_x;
    constexpr int sprite_height = 16;

    for (int line = 0; line < 168; ++line) {
        while (next_change < changes.size() && changes[next_change].line <= line) {
            colors = gg::decode_palette(changes[next_change++].colors);
        }
        while (next_scroll < scroll_changes.size() && scroll_changes[next_scroll].line <= line) {
            scroll_x = scroll_changes[next_scroll++].scroll_x;
        }
        if (line < 24) {
            continue; // Above the LCD.
        }
        const int y = line - 24;
        const platform::Color backdrop = colors[16];
        if (!shown) {
            for (int x = 0; x < 160; ++x) {
                image.put(x, y, backdrop);
            }
            continue;
        }

        // Background.
        std::array<u8, 256> bg{};
        std::array<bool, 256> bg_priority{};
        const int py = (line + scroll_y) % 224;
        for (int x = 48; x < 208; ++x) {
            const int px = (x - scroll_x) & 255;
            const unsigned entry = cell(px >> 3, py >> 3, level);
            int tx = px & 7, ty = py & 7;
            if (entry & 0x200) {
                tx = 7 - tx;
            }
            if (entry & 0x400) {
                ty = 7 - ty;
            }
            const auto index = row_pixel(*this, entry & 0x1FF, unsigned(ty), tx, level);
            bg[std::size_t(x)] = u8(index | ((entry & 0x800) ? 16 : 0));
            bg_priority[std::size_t(x)] = (entry & 0x1000) && index;
        }

        // Sprites.
        std::array<u8, 256> sp{};
        for (const auto& s : sprites) {
            if (s.y == 0xD0) {
                break;
            }
            int sy = s.y + 1;
            if (sy > 0xF0) {
                sy -= 256;
            }
            if (line < sy || line >= sy + sprite_height) {
                continue;
            }
            const unsigned row = unsigned(line - sy);
            const unsigned tile = (s.tile & 0xFEu) + row / 8;
            for (int p = 0; p < 8; ++p) {
                const int x = s.x + p;
                if (x > 255 || sp[std::size_t(x)]) {
                    continue;
                }
                if (const auto index = row_pixel(*this, tile, row % 8, p, level)) {
                    sp[std::size_t(x)] = u8(16 + index);
                }
            }
        }

        for (int x = 0; x < 160; ++x) {
            const auto sx = std::size_t(x + 48);
            image.put(x, y, colors[sp[sx] && !bg_priority[sx] ? sp[sx] : bg[sx]]);
        }
    }
    return image;
}

} // namespace gg_engine
