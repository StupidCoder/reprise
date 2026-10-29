#pragma once
#include "platform/image.hpp"
#include <cstdint>
#include <string_view>
#include <vector>

// The 320 x 200 picture a Mind Walker stage draws into, as pen numbers (the
// screen's colour indices: 5 bits on the stages' 32-colour screens, 6 bits on
// the title's hold-and-modify screen), with the drawing operations the stages
// use from graphics.library: pens, fills, lines and text.
namespace mindwalker::screen {

using Rgb4 = std::uint16_t;

enum class TextFont { topaz8, topaz9 };
enum TextStyle : unsigned { plain = 0, bold = 2, italic = 4 };

class Canvas {
public:
    static constexpr int width = 320, height = 200;

    Canvas() : pens_(std::size_t(width * height)) {}

    std::uint8_t pen(int x, int y) const { return pens_[std::size_t(y * width + x)]; }
    void plot(int x, int y, int pen) {
        if (x >= 0 && y >= 0 && x < width && y < height) pens_[std::size_t(y * width + x)] = std::uint8_t(pen);
    }
    void clear(int pen = 0) { std::fill(pens_.begin(), pens_.end(), std::uint8_t(pen)); }
    // RectFill: both corners included.
    void fill(int x0, int y0, int x1, int y1, int pen);
    // Draw from (x0, y0) to (x1, y1), both ends included.
    void line(int x0, int y0, int x1, int y1, int pen);
    // Text with the pen at (x, baseline): JAM1 (only the glyphs' pixels) or
    // JAM2 (the cells' other pixels in `back`). Styles are graphics.library's
    // algorithmic ones: bold ORs each row with itself one pixel to the right,
    // italic shifts each row right by (baseline - row) >> 1; characters
    // advance by the font's width either way.
    void text(int x, int baseline, std::string_view text, int pen, TextFont = TextFont::topaz8,
              unsigned style = plain, bool jam2 = false, int back = 0);
    static int text_width(std::string_view text, TextFont = TextFont::topaz8);

    // The picture on a 32-colour screen.
    platform::Image picture(const std::vector<Rgb4>& palette) const;
    // The picture on the hold-and-modify screen (6-bit pens; `palette` holds
    // the 16 base colours).
    platform::Image ham_picture(const std::vector<Rgb4>& palette) const;

private:
    std::vector<std::uint8_t> pens_;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(pens_); }
};

platform::Color to_color(Rgb4);

} // namespace mindwalker::screen
