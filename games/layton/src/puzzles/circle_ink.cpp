// The ink drawing service (overlay 2, 02071470 / 020714dc). See circle_ink.hpp.
#include "puzzles/circle_ink.hpp"
#include "graphics/text.hpp"
#include <algorithm>
#include <cstdlib>

namespace picarat {

void CircleInk::clear() {
    std::fill(surface.pixels.begin(), surface.pixels.end(), 0);
}

void CircleInk::apply(const CircleInkCommand& c, const Font& font, std::uint16_t color) {
    if (c.clear) {
        clear();
    }
    CircleInk::stamp(surface, c, font, color);
}

void CircleInk::stamp(Surface& layer, const CircleInkCommand& c, const Font& font,
                      std::uint16_t color) {
    // The brush glyph is drawn 4 pixels up and left of the point.
    auto stamp = [&](int x, int y) {
        textGlyph(layer, font, 0x60, x - 4, y - 4, color | 0x8000);
    };
    if (c.kind == CircleInkCommand::Kind::None) {
        return;
    }
    // Ink is kept to x in 4..250; a dot left of x = 4 draws nothing.
    if (c.kind == CircleInkCommand::Kind::Dot) {
        if (c.x0 >= 4) {
            stamp(std::min(c.x0, 250), c.y0);
        }
        return;
    }
    // A Bresenham line, one stamp per pixel. Quirk kept: the deltas and
    // directions are taken before the x coordinates are clamped.
    const int dx = std::abs(c.x1 - c.x0);
    const int dy = std::abs(c.y1 - c.y0);
    const int sx = c.x1 < c.x0 ? -1 : 1;
    const int sy = c.y1 < c.y0 ? -1 : 1;
    int x = std::clamp(c.x0, 4, 250);
    int y = c.y0;
    const int targetX = std::clamp(c.x1, 4, 250);
    stamp(x, y);
    if (dy < dx) {
        int error = -dx;
        while (x != targetX) {
            error += 2 * dy;
            if (error >= 0) {
                error -= 2 * dx;
                y += sy;
            }
            x += sx;
            stamp(x, y);
        }
    } else {
        int error = -dy;
        while (y != c.y1) {
            error += 2 * dx;
            if (error >= 0) {
                error -= 2 * dy;
                x += sx;
            }
            y += sy;
            stamp(x, y);
        }
    }
}

} // namespace picarat
