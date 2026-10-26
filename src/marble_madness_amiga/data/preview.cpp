#include "marble_madness_amiga/data/preview.hpp"
#include <algorithm>

namespace marble_madness::data {
namespace {

constexpr platform::Color backdrop{40, 40, 56, 255};
constexpr std::array<platform::Color, 4> sprite_ramp = {
    platform::Color{0, 0, 0, 0}, platform::Color{90, 90, 90, 255}, platform::Color{170, 170, 170, 255},
    platform::Color{250, 250, 250, 255}};

void fill(platform::Image& image, platform::Color colour) {
    std::fill(image.pixels.begin(), image.pixels.end(), colour);
}

void draw_tile(platform::Image& image, const Tile& tile, const Palette& palette, int x0, int y0) {
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            image.put(x0 + x, y0 + y, to_color(palette[tile[std::size_t(y * 8 + x)]]));
        }
    }
}

} // namespace

platform::Color to_color(Rgb4 colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17),
            255};
}

platform::Image course_image(const Assets& assets, Course course) {
    const auto& map = assets.maps[std::size_t(course)];
    const int extra = map.stored_rows() > map.rows ? 2 : 0;
    platform::Image image(CourseMap::columns * 8, map.stored_rows() * 8 + extra);
    fill(image, platform::Color{255, 0, 0, 255});
    for (int row = 0; row < map.stored_rows(); ++row) {
        const int y = row * 8 + (row >= map.rows ? extra : 0);
        for (int column = 0; column < CourseMap::columns; ++column) {
            draw_tile(image, map.tiles[map.cell(row, column)], map.palette, column * 8, y);
        }
    }
    return image;
}

platform::Image tile_sheet(const Assets& assets, Course course) {
    const auto& map = assets.maps[std::size_t(course)];
    constexpr int per_row = 32;
    const int rows = (int(map.tiles.size()) + per_row - 1) / per_row;
    platform::Image image(per_row * 9 + 1, rows * 9 + 1);
    fill(image, backdrop);
    for (std::size_t n = 0; n < map.tiles.size(); ++n) {
        draw_tile(image, map.tiles[n], map.palette, 1 + int(n % per_row) * 9, 1 + int(n / per_row) * 9);
    }
    return image;
}

platform::Image library_sheet(const ImageLibrary& library, const Palette& palette, int width) {
    // Lay the cells out in rows, left to right, 2 pixels apart.
    struct Place {
        int x, y;
    };
    std::vector<Place> places;
    int x = 2, y = 2, row_height = 0, used_width = 0;
    for (const auto& cell : library.cells) {
        if (x + cell.width + 2 > width && x > 2) {
            x = 2;
            y += row_height + 2;
            row_height = 0;
        }
        places.push_back({x, y});
        x += cell.width + 2;
        used_width = std::max(used_width, x);
        row_height = std::max(row_height, cell.height);
    }
    platform::Image image(std::max(used_width, 4), y + row_height + 2);
    fill(image, backdrop);
    for (std::size_t i = 0; i < library.cells.size(); ++i) {
        const auto& cell = library.cells[i];
        for (int py = 0; py < cell.height; ++py) {
            for (int px = 0; px < cell.width; ++px) {
                const auto index = cell.pixels[std::size_t(py * cell.width + px)];
                if (index == 0) {
                    continue;
                }
                image.put(places[i].x + px, places[i].y + py,
                          cell.type == 1 ? sprite_ramp[index & 3] : to_color(palette[index & 15]));
            }
        }
    }
    return image;
}

platform::Image splash_image(const Assets& assets) {
    const auto& picture = assets.splash;
    platform::Image image(picture.width, picture.height);
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            const auto index = picture.pixels[std::size_t(y * picture.width + x)];
            image.put(x, y, index < picture.palette.size() ? to_color(picture.palette[index]) : platform::Color{});
        }
    }
    return image;
}

} // namespace marble_madness::data
