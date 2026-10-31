#include "stunt_car_racer_amiga/render/front_end_screen.hpp"
#include "stunt_car_racer_amiga/data/planar.hpp"
#include <algorithm>

namespace stuntcar::render {
namespace {

constexpr int width = data::screen_width, height = data::screen_height;

// The panel ($64C6E): grey (colour 3) from row 65 to 191; its first two
// rows leave the logo's lower edge alone (x 64-255), the rest span x
// 32-255.
constexpr std::uint8_t panel_grey = 3;
constexpr int panel_top = 65, panel_bottom = 192, panel_left = 32, panel_right = 256, panel_notch_left = 64;
// Bars: copied from the strips of the menu picture, 224 pixels wide at x 32;
// a bar on text row r starts at screen row 8r - 9.
constexpr int bar_x = 32, bar_width = 224, bar_rows = 17, tall_bar_rows = 25, extra_line_rows = 16;
// Text: a cell is 7 pixels wide (the name font 6) and 8 high; row r of the
// menus sits 8 pixels up (the menus' display starts 8 rows lower).
constexpr int cell_width = 7, name_cell_width = 6, cell_height = 8, menu_shift = 8;
// Portraits: 80 x 55, with the name strip ($5893C).
constexpr int portrait_width = 80, portrait_height = 55;

} // namespace

FrontEndCanvas::FrontEndCanvas(const data::FrontEndArt& art) : art_(art), palette_(art.menu.palette) {}

void FrontEndCanvas::picture(const data::Picture& p) {
    std::copy(p.pixels.begin(), p.pixels.end(), pixels_.begin());
    palette_ = p.palette;
}

void FrontEndCanvas::copy(const data::Picture& p, int sx, int sy, int w, int h, int dx, int dy) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int tx = dx + x, ty = dy + y;
            if (tx < 0 || ty < 0 || tx >= width || ty >= height) continue;
            pixels_[std::size_t(ty * width + tx)] = p.pixels[std::size_t((sy + y) * width + sx + x)];
        }
}

void FrontEndCanvas::fill(int x, int y, int w, int h, std::uint8_t colour) {
    for (int ty = std::max(0, y); ty < std::min(height, y + h); ++ty)
        for (int tx = std::max(0, x); tx < std::min(width, x + w); ++tx) pixels_[std::size_t(ty * width + tx)] = colour;
}

void FrontEndCanvas::overlay(const std::uint8_t* pixels, int w, int h, int x, int y, std::uint8_t clear) {
    for (int sy = 0; sy < h; ++sy)
        for (int sx = 0; sx < w; ++sx) {
            const auto c = pixels[sy * w + sx];
            const int tx = x + sx, ty = y + sy;
            if (c == clear || tx < 0 || ty < 0 || tx >= width || ty >= height) continue;
            pixels_[std::size_t(ty * width + tx)] = c;
        }
}

void FrontEndCanvas::menu_panel() {
    picture(art_.menu);
    fill(panel_notch_left, panel_top, panel_right - panel_notch_left, 2, panel_grey);
    fill(panel_left, panel_top + 2, panel_right - panel_left, panel_bottom - panel_top - 2, panel_grey);
}

void FrontEndCanvas::bar(int row, Strip strip, int extra_lines) {
    const int source = art_.strip_rows[std::size_t(strip)], top = 8 * row - 9;
    copy(art_.menu, bar_x, source, bar_width, strip == Strip::tall ? tall_bar_rows : bar_rows, bar_x, top);
    for (int k = 1; k <= extra_lines; ++k)
        copy(art_.menu, bar_x, source + 1, bar_width, extra_line_rows, bar_x, top + 1 + 8 * k);
}

void FrontEndCanvas::portrait(int driver, data::Point at) {
    const auto from = art_.portrait_in_sheet[art_.portrait_of_driver[std::size_t(driver)]];
    copy(art_.portraits, from.x, from.y, portrait_width, portrait_height, at.x, at.y);
}

void FrontEndCanvas::trimmed_portrait(int driver, data::Point at, int shift) {
    constexpr int trim = 2;
    const auto from = art_.portrait_in_sheet[art_.portrait_of_driver[std::size_t(driver)]];
    copy(art_.portraits, from.x + trim, from.y, portrait_width - 2 * trim, portrait_height, at.x + shift + trim, at.y);
}

void FrontEndCanvas::print(std::string_view s) {
    for (const char c : s) {
        if (pending_ == 2) {
            column_ = std::uint8_t(c), --pending_;
        } else if (pending_ == 1) {
            row_ = std::uint8_t(c), --pending_;
        } else if (c == data::move_cursor) {
            pending_ = 2;
        } else if (std::uint8_t(c) == 0x7F) {
            // Rub out the last character: a space with its paper.
            --column_;
            const auto keep = paper_;
            if (!paper_) paper_ = panel_grey;
            glyph(' ');
            paper_ = keep;
            --column_;
        } else {
            glyph(c);
        }
    }
}

void FrontEndCanvas::print_number(int value) {
    const auto digits = std::to_string(value);
    print(digits);
}

void FrontEndCanvas::glyph(char c) {
    const int index = std::clamp(int(std::uint8_t(c)) - ' ', 0, 95);
    const auto& rows = name_font_ ? art_.name_font[std::size_t(index)] : art_.font[std::size_t(index)];
    const int cell = name_font_ ? name_cell_width : cell_width;
    const int x0 = column_ * cell + dx_, y0 = row_ * cell_height + dy_ - (menu_rows_ ? menu_shift : 0);
    for (int y = 0; y < cell_height; ++y)
        for (int x = 0; x < cell; ++x) {
            const int tx = x0 + x, ty = y0 + y;
            if (tx < 0 || ty < 0 || tx >= width || ty >= height) continue;
            const bool on = rows[std::size_t(y)] & (0x80 >> x);
            if (on) pixels_[std::size_t(ty * width + tx)] = ink_;
            else if (paper_) pixels_[std::size_t(ty * width + tx)] = *paper_;
        }
    ++column_;
}

platform::Image FrontEndCanvas::image() const {
    platform::Image image(width, height);
    for (std::size_t i = 0; i < pixels_.size(); ++i) {
        const auto& c = palette_[pixels_[i] & 15];
        image.pixels[i] = {c.r, c.g, c.b, 255};
    }
    return image;
}

} // namespace stuntcar::render
