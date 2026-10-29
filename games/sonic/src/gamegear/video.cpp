#include "gamegear/video.hpp"
#include <stdexcept>

namespace gg {

void Screen::put_index(int x, int y, int index, const Palette& base) {
    // Find the last palette change at or above this row.
    const Palette* palette = &base;
    for (const auto& change : palette_changes) {
        if (y < change.first) {
            break;
        }
        palette = &change.second;
    }
    put(x, y, (*palette)[index]);
}

Palette decode_palette(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 64) {
        throw std::runtime_error("Game Gear palette needs 64 bytes");
    }
    Palette colors;
    for (unsigned i = 0; i < 32; ++i) {
        const std::uint8_t low = bytes[i * 2];      // GGGGRRRR
        const std::uint8_t high = bytes[i * 2 + 1]; // 0000BBBB
        colors[i] = {std::uint8_t((low & 15) * 17), std::uint8_t((low >> 4) * 17),
                     std::uint8_t((high & 15) * 17), 255};
    }
    return colors;
}

std::uint8_t tile_pixel(std::span<const std::uint8_t> patterns, unsigned tile, int x, int y) {
    // 32 bytes per tile, 4 bytes (one per bit plane) per row.
    const std::size_t row = std::size_t(tile) * 32 + std::size_t(y) * 4;
    if (row + 4 > patterns.size()) {
        throw std::runtime_error("Tile index outside pattern data");
    }
    unsigned color = 0;
    for (unsigned plane = 0; plane < 4; ++plane) {
        color |= ((patterns[row + plane] >> (7 - x)) & 1) << plane;
    }
    return static_cast<std::uint8_t>(color);
}

void draw_tile(Screen& screen, std::span<const std::uint8_t> patterns, unsigned tile, int x, int y,
               const Palette& palette, int palette_base, bool transparent) {
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            const auto index = tile_pixel(patterns, tile, px, py);
            if (!transparent || index != 0) {
                screen.put_index(x + px, y + py, palette_base + index, palette);
            }
        }
    }
}

} // namespace gg
