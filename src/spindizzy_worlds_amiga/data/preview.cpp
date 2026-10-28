#include "spindizzy_worlds_amiga/data/preview.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include "spindizzy_worlds_amiga/view/sprites.hpp"
#include "spindizzy_worlds_amiga/world/level_builder.hpp"
#include "ui/draw.hpp"
#include <algorithm>
#include <string>

namespace spindizzy::data {
namespace {

// Legend colours, not the game's: the game's block colours come from its
// block graphics, which are not decoded yet.
platform::Color height_color(int z, std::uint8_t type) {
    const int level = 70 + z * 23; // 0-7 → dark to bright
    switch (type >> 6) {           // the two high bits (variants; meaning not traced)
    case 1: return {std::uint8_t(level / 2), std::uint8_t(level), std::uint8_t(level)};
    case 2: return {std::uint8_t(level), std::uint8_t(level), std::uint8_t(level / 2)};
    case 3: return {std::uint8_t(level), std::uint8_t(level / 2), std::uint8_t(level)};
    default: return {std::uint8_t(level), std::uint8_t(level), std::uint8_t(level)};
    }
}

} // namespace

platform::Color to_color(Colour colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17)};
}

platform::Image title_image(const Picture& picture) {
    platform::Image image(picture.width, picture.height);
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < picture.width; ++x) {
            image.put(x, y, to_color(picture.palette[picture.pixels[std::size_t(y * picture.width + x)]]));
        }
    }
    return image;
}

platform::Image level_image(const World& world, int level, const Assets& assets) {
    const auto built = world::build_level(world, level, assets.block_attributes);
    const auto map = view::TileMap::build(built.grid, assets.graphics);
    int left = view::map_columns, right = -1, top = view::map_rows, bottom = -1;
    for (int row = 0; row < view::map_rows; ++row) {
        for (int column = 0; column < view::map_columns; ++column) {
            if (map.layers(column, row).empty()) continue;
            left = std::min(left, column); right = std::max(right, column);
            top = std::min(top, row); bottom = std::max(bottom, row);
        }
    }
    if (right < 0) {
        platform::Image empty(160, 24);
        ui::fill(empty, {12, 12, 16});
        ui::text(empty, "Objects only", 8, 8, 1, {200, 200, 200});
        return empty;
    }
    view::Canvas canvas((right - left + 1) * tile_width + 16, (bottom - top + 1) * tile_height + 16);
    const int view_left = left * tile_width - 8, view_top = top * tile_height - 8;
    view::draw_playfield(canvas, map, assets.graphics, view_left, view_top);
    // The explorer at its start cell, standing on the top block there
    // (hypothesis for the height), first spin step.
    const auto header = world.header(level);
    const int sx = header.start_x(), sy = header.start_y();
    const int ground = built.grid.top(sx, sy);
    const view::WorldPosition start{sx * 256 + 0x80, sy * 256 + 0x80, ground < 0 ? 0 : ground * 256};
    view::draw_sprite(canvas, read_sprite_frame(assets.main, view::explorer_frames, 1, 16), view::explorer_place(start),
                      map, assets.graphics, view_left, view_top);
    platform::Image image(canvas.width, canvas.height);
    const auto& palette = assets.graphics.palettes[0];
    for (int y = 0; y < canvas.height; ++y)
        for (int x = 0; x < canvas.width; ++x) image.put(x, y, to_color(palette[canvas.at(x, y)]));
    return image;
}

platform::Image world_image(const World& world, const std::array<std::uint8_t, 256>& block_attributes) {
    constexpr int cell = 4, map = world::level_size * cell, gap = 12, margin = 8, label = 14, columns = 4;
    const int count = std::max<int>(1, int(world.levels.size()));
    const int rows = (count + columns - 1) / columns;
    platform::Image image(margin * 2 + columns * map + (columns - 1) * gap, margin * 2 + rows * (label + map + gap));
    ui::fill(image, {12, 12, 16});
    if (world.levels.empty()) {
        ui::text(image, "This world file holds no levels.", margin, margin, 1, {200, 200, 200});
        return image;
    }
    int n = 0;
    for (const auto& [id, record] : world.levels) {
        const int x0 = margin + (n % columns) * (map + gap);
        const int y0 = margin + (n / columns) * (label + map + gap);
        ++n;
        ui::text(image, "Level " + std::to_string(id), x0, y0, 1, {200, 200, 200});
        const auto level = world::build_level(world, id, block_attributes);
        for (int x = 0; x < world::level_size; ++x) {
            for (int y = 0; y < world::level_size; ++y) {
                const int z = level.grid.top(x, y);
                // x grows to the right, y downwards (the editor's map; the
                // game's view is isometric).
                const auto colour = z < 0 ? platform::Color{24, 24, 32} : height_color(z, level.grid.at(x, y, z));
                ui::rect(image, x0 + x * cell, y0 + label + y * cell, cell, cell, colour);
            }
        }
        for (const auto& object : level.objects) {
            ui::rect(image, x0 + object.x * cell + 1, y0 + label + object.y * cell + 1, cell - 2, cell - 2, {255, 200, 40});
        }
    }
    return image;
}

} // namespace spindizzy::data
