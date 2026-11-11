#include "fort_apocalypse_c64/game/game.hpp"
#include "fort_apocalypse_c64/enhanced/canvas.hpp"
#include <cmath>

namespace fort_apocalypse::game {
std::optional<enhanced::WorldScene> Game::world_scene() const {
    if (!shown_playfield()) return std::nullopt;
    enhanced::WorldScene scene;
    scene.level = level_;
    // Play colours ($AE19/$7F); row 9's black screen strip is a viewport
    // artifact, not a black stripe across the world. See data::play_colours.
    scene.colours = {u8(playfield_colour_ & 15), u8(level_colour_ & 15), 1, 5};
    for (int row = 0; row < scene.rows; ++row) {
        for (int col = 0; col < scene.columns; ++col) {
            int source = col;
            if (col < 40) {
                if (map_code(col, row) != map_code(col + scene.columns, row) ||
                    cell_glyph(col, row) != cell_glyph(col + scene.columns, row))
                    ++scene.alias_disagreements;
                // The original copies these only at level construction. Keep
                // the copy nearest the original camera's center; never mutate
                // either alias to make a presentation seam disappear.
                const int middle = int(camera_column_) - 1 + 20;
                if (std::abs(col + scene.columns - middle) < std::abs(col - middle))
                    source += scene.columns;
            }
            auto& cell = scene.cells[std::size_t(row * scene.columns + col)];
            if (map_code(source, row) < 128) cell.glyph = cell_glyph(source, row);
        }
    }

    // Resolve the old displayed sprite coordinates at the game boundary.
    // The source-line map accounts for the raster split and fine scrolling;
    // it is read-only and does not latch any collisions into Game.
    c64::Layers layers;
    shown_frame_.rasterise(nullptr, &layers);
    const enhanced::Viewport view(layers, shown_window_.column, shown_window_.row,
                                  window_first_row, shown_frame_);
    if (!view.valid()) return std::nullopt;
    auto add = [&](const c64::Sprite& sprite, int x, int y) {
        enhanced::WorldScene::Sprite out;
        out.x = x; out.y = y; out.width = sprite.expand_x ? 48 : 24; out.height = 21;
        out.colour = sprite.colour;
        out.mask.resize(std::size_t(out.width * out.height));
        for (int sy = 0; sy < out.height; ++sy) for (int sx = 0; sx < out.width; ++sx) {
            const int bit = sx / (sprite.expand_x ? 2 : 1);
            out.mask[std::size_t(sy * out.width + sx)] =
                (sprite.data[std::size_t(sy * 3 + bit / 8)] >> (7 - bit % 8)) & 1;
        }
        scene.sprites.push_back(std::move(out));
    };
    for (int i = 0; i < 5; ++i) {
        auto sprite = shown_frame_.sprite(i);
        if (i == 1 && enemy_hidden()) continue;
        if (!sprite.shown) continue;
        int sx = sprite.x;
        if (sx >= 448) sx -= 512;
        const int x = int(std::lround(view.world_x(sx - 24)));
        const int y = int(std::lround(view.world_y(sprite.y + 1 - 51)));
        add(sprite, x, y);
        if (i == 0) scene.focus_x = enhanced::wrap_world_x(x + 16) / double(scene.width);
    }
    if (enemy_hidden() && enemy_.state != 1) {
        // The enemy's world location is independent of camera visibility.
        // Algebraically remove camera/fine scroll from enemy_visibility
        // ($A1B0) and the playfield origin (row 9 starts at 69 + fine_y).
        c64::Sprite sprite;
        sprite.expand_x = true;
        sprite.colour = enemy_.colour;
        const auto& shape = assets_.helicopter[assets_.helicopter_tilt[std::min<u8>(enemy_.bank, 17)]];
        for (int y = 0; y < 18; ++y) {
            sprite.data[std::size_t(y * 3)] = u8(shape.rows[std::size_t(y)] >> 8);
            sprite.data[std::size_t(y * 3 + 1)] = u8(shape.rows[std::size_t(y)]);
        }
        add(sprite, (int(enemy_.column) - 1) * 8 - 52 + enemy_.sub_x * 2,
            int(enemy_.row) * 8 - 14 + enemy_.sub_y);
    }
    return scene;
}
} // namespace fort_apocalypse::game
