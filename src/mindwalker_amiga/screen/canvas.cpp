#include "mindwalker_amiga/screen/canvas.hpp"
#include "amiga/topaz.hpp"
#include <cstdlib>

namespace mindwalker::screen {
namespace {

struct FontMetrics {
    int width, height, baseline;
};

FontMetrics metrics(TextFont font) {
    return font == TextFont::topaz8 ? FontMetrics{amiga::Topaz8::width, amiga::Topaz8::height, amiga::Topaz8::baseline}
                                    : FontMetrics{amiga::Topaz9::width, amiga::Topaz9::height, amiga::Topaz9::baseline};
}

bool glyph_pixel(TextFont font, unsigned char c, int x, int y) {
    return font == TextFont::topaz8 ? amiga::Topaz8::pixel(c, x, y) : amiga::Topaz9::pixel(c, x, y);
}

} // namespace

platform::Color to_color(Rgb4 colour) {
    return {std::uint8_t((colour >> 8 & 15) * 17), std::uint8_t((colour >> 4 & 15) * 17), std::uint8_t((colour & 15) * 17),
            255};
}

void Canvas::fill(int x0, int y0, int x1, int y1, int pen) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            plot(x, y, pen);
        }
    }
}

void Canvas::line(int x0, int y0, int x1, int y1, int pen) {
    // Bresenham along the longer axis, as the blitter's line mode draws.
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const int sx = x1 >= x0 ? 1 : -1, sy = y1 >= y0 ? 1 : -1;
    const bool steep = dy > dx;
    const int major = steep ? dy : dx, minor = steep ? dx : dy;
    int error = 2 * minor - major;
    int x = x0, y = y0;
    for (int i = 0; i <= major; ++i) {
        plot(x, y, pen);
        if (error >= 0) {
            (steep ? x : y) += steep ? sx : sy;
            error -= 2 * major;
        }
        error += 2 * minor;
        (steep ? y : x) += steep ? sy : sx;
    }
}

void Canvas::text(int x, int baseline, std::string_view text, int pen, TextFont font, unsigned style, bool jam2,
                  int back) {
    const auto m = metrics(font);
    const int top = baseline - m.baseline;
    for (std::size_t n = 0; n < text.size(); ++n) {
        const int cx = x + int(n) * m.width;
        const auto c = static_cast<unsigned char>(text[n]);
        for (int row = 0; row < m.height; ++row) {
            const int shift = (style & italic) ? (m.baseline - row) >> 1 : 0;
            if (jam2) {
                for (int col = 0; col < m.width; ++col) plot(cx + col, top + row, back);
            }
            for (int col = 0; col < m.width; ++col) {
                if (!glyph_pixel(font, c, col, row)) continue;
                plot(cx + col + shift, top + row, pen);
                if (style & bold) plot(cx + col + shift + 1, top + row, pen);
            }
        }
    }
}

int Canvas::text_width(std::string_view text, TextFont font) {
    return int(text.size()) * metrics(font).width;
}

platform::Image Canvas::picture(const std::vector<Rgb4>& palette) const {
    platform::Image image(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto p = pen(x, y);
            image.put(x, y, to_color(p < palette.size() ? palette[p] : 0));
        }
    }
    return image;
}

platform::Image Canvas::ham_picture(const std::vector<Rgb4>& palette) const {
    platform::Image image(width, height);
    for (int y = 0; y < height; ++y) {
        Rgb4 colour = palette[0]; // each line starts from the background colour
        for (int x = 0; x < width; ++x) {
            const int code = pen(x, y), value = code & 15;
            switch (code >> 4 & 3) {
            case 0: colour = palette[std::size_t(value)]; break;
            case 1: colour = Rgb4((colour & 0xFF0) | value); break;
            case 2: colour = Rgb4((colour & 0x0FF) | value << 8); break;
            default: colour = Rgb4((colour & 0xF0F) | value << 4); break;
            }
            image.put(x, y, to_color(colour));
        }
    }
    return image;
}

} // namespace mindwalker::screen
