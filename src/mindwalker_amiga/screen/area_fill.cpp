#include "mindwalker_amiga/screen/area_fill.hpp"
#include <algorithm>
#include <cstdlib>
#include <utility>

namespace mindwalker::screen {

// graphics.library's AreaEnd, as the blitter does it: each edge is drawn
// into a scratch plane by the line mode with one dot per raster line (SING:
// the first dot of each row as Bresenham steps from the edge's start to its
// end, both ends included), exclusive-or; flat edges give none, and an edge
// that carries on the previous one's way up or down leaves out its first row
// (the vertex keeps one dot, so the row through it fills). The plane is
// then filled row by row from right to left, inclusively (the boundary dots
// are filled too), and the filled pixels take the pen. Then, with
// AREAOUTLINE, the outline in its own pen. (Matches the original's view and
// sprite masks pixel for pixel in every recording so far, and the tunnel's
// rings to within a pixel on shallow edges.)
void fill_area(const std::vector<AreaPoint>& points, const std::function<void(int x, int y)>& plot) {
    int y0 = 1 << 30, y1 = -(1 << 30), x0 = 1 << 30, x1 = -(1 << 30);
    for (const auto& p : points) {
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
    }
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    std::vector<std::uint8_t> dots(std::size_t(w) * std::size_t(h));
    const std::size_t n = points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto a = points[i], b = points[(i + 1) % n];
        if (a.y == b.y) continue;
        // The previous edge (flat ones passed over) going the same way up or down.
        bool through = false;
        for (std::size_t j = 1; j < n; ++j) {
            const auto p = points[(i + n - j) % n], q = points[(i + n - j + 1) % n];
            if (p.y == q.y) continue;
            through = (q.y > p.y) == (b.y > a.y);
            break;
        }
        const int dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
        const int sx = b.x > a.x ? 1 : -1, sy = b.y > a.y ? 1 : -1;
        const bool y_major = dy >= dx;
        const int major = y_major ? dy : dx, minor = y_major ? dx : dy;
        int error = 2 * minor - major, x = a.x, y = a.y, last_row = through ? a.y : 1 << 30;
        for (int k = 0; k <= major; ++k) {
            if (y != last_row) {
                dots[std::size_t((y - y0) * w + (x - x0))] ^= 1;
                last_row = y;
            }
            if (error >= 0) {
                if (y_major) x += sx;
                else y += sy;
                error += 2 * (minor - major);
            } else {
                error += 2 * minor;
            }
            if (y_major) y += sy;
            else x += sx;
        }
    }
    for (int y = y0; y <= y1; ++y) {
        bool inside = false;
        for (int x = x1; x >= x0; --x) {
            const bool dot = dots[std::size_t((y - y0) * w + (x - x0))] != 0;
            if (dot) inside = !inside;
            if (inside || dot) plot(x, y);
        }
    }
}

void fill(Canvas& canvas, const std::vector<AreaPoint>& points, int pen, int outline, int top) {
    fill_area(points, [&](int x, int y) { canvas.plot(x, top + y, pen); });
    if (outline >= 0) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto a = points[i], b = points[(i + 1) % points.size()];
            canvas.line(a.x, top + a.y, b.x, top + b.y, outline);
        }
    }
}

} // namespace mindwalker::screen
