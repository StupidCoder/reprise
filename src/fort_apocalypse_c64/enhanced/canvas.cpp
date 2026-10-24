#include "fort_apocalypse_c64/enhanced/canvas.hpp"
#include <algorithm>
#include <cmath>

namespace fort_apocalypse::enhanced {

void draw_character(Bitmap& out, int x, int y, const c64::Glyph& glyph, u8 cell_colour, u8 shared1,
                    u8 shared2) {
    const bool wide = out.pixel_width == 2;
    for (int row = 0; row < 8; ++row) {
        const u8 bits = glyph[std::size_t(row)];
        for (int px = 0; px < 8; px += wide ? 2 : 1) {
            u8 colour = Bitmap::clear;
            if (cell_colour & 8) {
                const int pair = bits >> (6 - (px & 6)) & 3;
                const u8 by_pair[] = {Bitmap::clear, shared1, shared2, u8(cell_colour & 7)};
                colour = by_pair[pair];
            } else if (bits >> (7 - px) & 1) {
                colour = cell_colour & 15;
            }
            if (colour != Bitmap::clear) {
                out.put(x + (wide ? px / 2 : px), y + row, colour);
            }
        }
    }
}

Bitmap scale2x(const Bitmap& in) {
    Bitmap out(in.width * 2, in.height * 2, in.pixel_width);
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            const u8 p = in.at(x, y), a = in.at(x, y - 1), b = in.at(x + 1, y),
                     c = in.at(x - 1, y), d = in.at(x, y + 1);
            u8 e0 = p, e1 = p, e2 = p, e3 = p;
            if (c == a && c != d && a != b) e0 = a;
            if (a == b && a != c && b != d) e1 = b;
            if (d == c && d != b && c != a) e2 = c;
            if (b == d && b != a && d != c) e3 = d;
            out.put(2 * x, 2 * y, e0);
            out.put(2 * x + 1, 2 * y, e1);
            out.put(2 * x, 2 * y + 1, e2);
            out.put(2 * x + 1, 2 * y + 1, e3);
        }
    }
    return out;
}

Viewport::Viewport(const c64::Layers& layers, int window_column, int window_row,
                   int window_first_row, const c64::TextFrame& frame)
    : right_(frame.width()) {
    // The playfield: lines showing character rows from the window's first.
    for (int y = 0; y < c64::Picture::height; ++y) {
        const int source = layers.source_line[std::size_t(y)];
        const bool playfield = source >= window_first_row * 8;
        if (playfield && first_line_ < 0) {
            first_line_ = y;
            const int world_y = (window_row + source / 8 - window_first_row) * 8 + source % 8;
            origin_y_ = y - world_y;
            const int xscroll = layers.xscroll[std::size_t(y)];
            origin_x_ = xscroll - window_column * 8;
            parity_ = xscroll & 1;
            if (frame.line(y).narrow) {
                left_ = 7; // the 38-column window
                right_ = frame.width() - 9;
            }
        }
        if (playfield) {
            last_line_ = y + 1;
        }
    }
}

void plot(c64::Picture& picture, int x, int y, u8 colour, const Clip& clip) {
    if (y < clip.first_line || y >= clip.last_line) {
        return;
    }
    for (int k = 0; k < 2; ++k) {
        const int px = x + k;
        if (px < clip.left || px >= clip.right) {
            continue;
        }
        const std::size_t at = std::size_t(y * picture.width + px);
        if (clip.behind && clip.behind->foreground[at]) {
            continue;
        }
        picture.pixels[at] = colour;
    }
}

void draw_rotated(c64::Picture& picture, const Bitmap& bitmap, double pivot_x, double pivot_y,
                  double x, double y, double radians, int parity, const Clip& clip) {
    // Unturned, the art is copied pixel for pixel.
    const bool turned = std::abs(radians) > 1e-4;
    const int enlarge = turned ? 8 : 1;
    const Bitmap big = turned ? scale2x(scale2x(scale2x(bitmap))) : bitmap;
    // Hires pixels to the enlarged art's pixels.
    const double per_x = double(enlarge) / bitmap.pixel_width, per_y = enlarge;
    const double c = turned ? std::cos(radians) : 1, s = turned ? std::sin(radians) : 0;
    const double reach =
        std::hypot(double(bitmap.width * bitmap.pixel_width), double(bitmap.height)) + 2;
    const int top = std::max(clip.first_line, int(std::floor(y - reach)));
    const int bottom = std::min(clip.last_line, int(std::ceil(y + reach)));
    // Grid cells (double-wide) from the left of the reach, on the parity.
    int left = int(std::floor(x - reach));
    left -= ((left - parity) % 2 + 2) % 2;
    const int right = int(std::ceil(x + reach));
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; px += 2) {
            // The cell's centre, turned back into the art.
            const double dx = px + 1.0 - x, dy = py + 0.5 - y;
            const double sx = c * dx + s * dy + pivot_x, sy = -s * dx + c * dy + pivot_y;
            const u8 colour = big.at(int(std::floor(sx * per_x)), int(std::floor(sy * per_y)));
            if (colour != Bitmap::clear) {
                plot(picture, px, py, colour, clip);
            }
        }
    }
}

} // namespace fort_apocalypse::enhanced
