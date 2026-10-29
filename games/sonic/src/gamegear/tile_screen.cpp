// Software rendering of a Game Gear "mode 4" screen; see tile_screen.hpp.

#include "gamegear/tile_screen.hpp"
#include <algorithm>

namespace gg {

Screen TileScreen::render() const {
    Screen image(160, 144);
    if (!enabled) {
        return image; // Display off: black.
    }
    const auto colors = decode_palette(palette);
    for (int y = 0; y < 144; ++y) {
        // The LCD shows rows 24..167 and columns 48..207 of the 256x192 picture.
        const int screen_y = y + 24;

        // Like the hardware, take the first eight 8x16 sprites on this line.
        // A sprite is shown one line below its stored Y.
        std::array<const TileSprite*, 8> line{};
        unsigned count = 0;
        for (const auto& sprite : sprites) {
            if (screen_y >= sprite.y + 1 && screen_y < sprite.y + 17) {
                line[count++] = &sprite;
                if (count == 8) {
                    break;
                }
            }
        }

        for (int x = 0; x < 160; ++x) {
            const int screen_x = x + 48;
            // Name table entry: tile number (9 bits), flips, palette and
            // priority bits. The 28-row table wraps vertically at 224 lines.
            const int row = (screen_y + scroll_y) % 224;
            const int offset = ((row / 8) * 32 + screen_x / 8) * 2;
            const unsigned entry = tiles.at(offset) | (tiles.at(offset + 1) << 8);
            int tx = screen_x & 7;
            int ty = row & 7;
            if (entry & 0x200) {
                tx = 7 - tx; // Horizontal flip.
            }
            if (entry & 0x400) {
                ty = 7 - ty; // Vertical flip.
            }
            const unsigned background = tile_pixel(patterns, entry & 511, tx, ty);
            auto color = colors[((entry & 0x800) ? 16 : 0) + background];

            // Sprites cover the background unless the tile has priority (bit
            // 12) and a non-zero colour. The first sprite in the list wins.
            if (!(entry & 0x1000) || !background) {
                for (unsigned i = 0; i < count; ++i) {
                    const auto& sprite = *line[i];
                    const int px = screen_x - sprite.x;
                    const int py = screen_y - sprite.y - 1;
                    if (px < 0 || px >= 8) {
                        continue;
                    }
                    // 8x16 sprites use an even tile and the one after it,
                    // from the upper 256 tiles.
                    const auto pixel =
                        tile_pixel(patterns, 256 + (sprite.tile & 254) + py / 8, px, py & 7);
                    if (pixel) {
                        color = colors[16 + pixel];
                        break;
                    }
                }
            }
            image.put(x, y, color);
        }
    }
    return image;
}

} // namespace gg
