#include "rock_n_roll_amiga/data/preview.hpp"
#include <algorithm>

namespace rocknroll::data {
namespace {

constexpr platform::Color backdrop{40, 40, 56, 255};
constexpr platform::Color missing{255, 0, 255, 255};
constexpr int tile = 16;
constexpr int sheet_columns = 20;

platform::Color colour_of(const Palette& palette, int index) {
    return std::size_t(index) < palette.size() ? to_color(palette[std::size_t(index)]) : missing;
}

// Top-left corner in the bank of the 16 x 16 picture the drawer uses for a
// cell (tile table $72CBE as $70954 builds it, abyss entries as $6C972 sets
// them with the camera on a tile boundary).
struct Corner {
    int x, y;
};

Corner tile_corner(std::uint8_t cell) {
    if (is_abyss(cell)) return {0, abyss_row};
    const int index = int(cell) - 1;
    return {index % sheet_columns * tile, index / sheet_columns * tile};
}

} // namespace

platform::Color to_color(amiga::Rgb4 colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17),
            255};
}

platform::Image picture_image(const amiga::IlbmPicture& picture) {
    platform::Image image(picture.width, picture.height);
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            image.put(x, y, colour_of(picture.palette, picture.pixels[std::size_t(y * picture.width + x)]));
        }
    }
    return image;
}

platform::Image bank_image(const GraphicsBank& bank, const Palette& palette) {
    platform::Image image(GraphicsBank::width, bank.rows);
    for (int y = 0; y < bank.rows; ++y) {
        for (int x = 0; x < GraphicsBank::width; ++x) {
            image.put(x, y, colour_of(palette, bank.at(x, y)));
        }
    }
    return image;
}

platform::Image level_image(const Level& level, const World& world, int cell_size) {
    cell_size = std::clamp(cell_size, 1, tile);
    platform::Image image(level.columns * cell_size, level.rows * cell_size);
    const auto& palette = world.tiles.palette;
    for (int row = 0; row < level.rows; ++row) {
        for (int column = 0; column < level.columns; ++column) {
            const auto cell = level.at(column, row);
            if (cell == 0 || cell > 0xF0) {
                for (int y = 0; y < cell_size; ++y)
                    for (int x = 0; x < cell_size; ++x) image.put(column * cell_size + x, row * cell_size + y, missing);
                continue;
            }
            const auto corner = tile_corner(cell);
            for (int y = 0; y < cell_size; ++y) {
                for (int x = 0; x < cell_size; ++x) {
                    // Average the block of tile pixels this output pixel covers.
                    int r = 0, g = 0, b = 0, n = 0;
                    for (int ty = y * tile / cell_size; ty < (y + 1) * tile / cell_size; ++ty) {
                        for (int tx = x * tile / cell_size; tx < (x + 1) * tile / cell_size; ++tx) {
                            const auto c = colour_of(palette, world.bank.at(corner.x + tx, corner.y + ty));
                            r += c.r; g += c.g; b += c.b; ++n;
                        }
                    }
                    image.put(column * cell_size + x, row * cell_size + y,
                              {std::uint8_t(r / n), std::uint8_t(g / n), std::uint8_t(b / n), 255});
                }
            }
        }
    }
    return image;
}

platform::Image palette_image(const Palette& palette) {
    constexpr int size = 12, per_row = 16;
    const int rows = std::max(1, (int(palette.size()) + per_row - 1) / per_row);
    platform::Image image(per_row * size, rows * size);
    std::fill(image.pixels.begin(), image.pixels.end(), backdrop);
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const int x0 = int(i % per_row) * size, y0 = int(i / per_row) * size;
        for (int y = 1; y < size - 1; ++y)
            for (int x = 1; x < size - 1; ++x) image.put(x0 + x, y0 + y, to_color(palette[i]));
    }
    return image;
}

} // namespace rocknroll::data
