#include "fort_apocalypse_c64/enhanced/widescreen.hpp"
#include <algorithm>
#include <cmath>

namespace fort_apocalypse::enhanced {

using game::Game;

WideSize wide_size(int canvas_width, int canvas_height, double pixel_aspect) {
    constexpr int standard = c64::Picture::standard_width, height = c64::Picture::height;
    // The original screen with its PAL border (32 and 36 pixels around).
    const double narrowest = (standard + 64) * pixel_aspect / (height + 72);
    constexpr double widest = 21.0 / 9.0;
    if (canvas_width <= 0 || canvas_height <= 0) {
        return {1, standard, 0};
    }
    const double aspect = std::clamp(double(canvas_width) / canvas_height, narrowest, widest);
    WideSize size;
    size.width = std::max(standard, int(std::lround(aspect * height / pixel_aspect)));
    if (size.width == standard) {
        // Narrower than the display window: border above and below instead.
        size.margin_y = std::max(0, int(std::lround((standard * pixel_aspect / aspect - height) / 2)));
    }
    size.columns =
        std::min(Game::wide_columns, int(std::ceil((size.width - standard) / 16.0)) + 1);
    return size;
}

c64::TextFrame wide_frame(const Game& game, int columns) {
    const auto& shown = game.shown_frame();
    if (columns <= 0) {
        return shown;
    }
    constexpr int standard = c64::TextFrame::standard_columns;
    c64::TextFrame frame(standard + 2 * columns);
    frame.set_idle_pattern(shown.idle_pattern());
    for (int y = 0; y < c64::Picture::height; ++y) {
        auto line = shown.line(y);
        line.narrow = false; // the 38-column window's border would cut the view
        if (line.background_from_x > 0) {
            line.background_from_x += columns * 8;
        }
        frame.set_lines(y, y + 1, line);
    }
    for (int row = 0; row < c64::TextFrame::rows; ++row) {
        for (int c = -columns; c < standard + columns; ++c) {
            if (c >= 0 && c < standard) {
                const auto& cell = shown.cell(c, row);
                frame.set(c + columns, row, &cell.glyph, cell.colour);
            } else if (row >= Game::window_first_row && game.shown_playfield()) {
                // The row's colour (the playfield rows have one colour each).
                const u8 colour = shown.cell(c < 0 ? 0 : standard - 1, row).colour;
                frame.set(c + columns, row, &game.playfield_glyph(game.shown_wide_code(c, row)),
                          colour);
            } else {
                frame.set(c + columns, row, nullptr, 0); // beside the HUD
            }
        }
    }
    for (int i = 0; i < 8; ++i) {
        // The game keeps sprite x in 9 bits: past the window's left edge a
        // position wraps to 448-511, which the wider view shows on the left.
        auto s = shown.sprite(i);
        if (s.x >= 448) {
            s.x -= 512;
        }
        frame.set_sprite(i, s);
    }
    // The enemy where the game hides it, but the wider view shows it.
    const auto enemy = game.shown_wide_enemy();
    if (enemy.placed && game.enemy_hidden()) {
        auto s = shown.sprite(1);
        s.x = enemy.sprite_x;
        s.y = enemy.sprite_y;
        frame.set_sprite(1, s);
    }
    return frame;
}

ScreenCodes screen_codes(const Game& game, int columns) {
    constexpr int standard = c64::TextFrame::standard_columns;
    columns = std::max(0, columns);
    ScreenCodes codes;
    codes.columns = standard + 2 * columns;
    codes.extension = columns;
    codes.codes.assign(std::size_t(codes.columns * c64::TextFrame::rows), 0);
    const auto& screen = game.shown_screen();
    for (int row = 0; row < c64::TextFrame::rows; ++row) {
        for (int c = -columns; c < standard + columns; ++c) {
            u8 code = 0;
            if (c >= 0 && c < standard) {
                code = screen[std::size_t(row * standard + c)];
            } else if (row >= Game::window_first_row && game.shown_playfield()) {
                code = game.shown_wide_code(c, row);
            }
            codes.codes[std::size_t(row * codes.columns + c + columns)] = code;
        }
    }
    return codes;
}

} // namespace fort_apocalypse::enhanced
