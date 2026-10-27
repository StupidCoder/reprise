#include "mindwalker_amiga/landscape/zap_bolt.hpp"
#include <algorithm>
#include <cstdlib>

namespace mindwalker::landscape {
namespace {

constexpr int view_top = 10; // $204D74: bolts stay below the bar ($206406)
constexpr int corner_x[4] = {0, 319, 319, 0};    // h27 ($206440)
constexpr int corner_y[4] = {11, 11, 199, 199};  // $206450

int clamp_row(int y) { return std::clamp(y, view_top, 199); }

// graphics.library's Draw: a line with both ends set.
void line(std::vector<std::uint8_t>& layer, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < 320 && y0 >= 0 && y0 < 200) layer[std::size_t(y0 * 320 + x0)] = 1;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

} // namespace

void ZapBolt::segment(std::vector<std::uint8_t>& layer, int x0, int y0, int x1, int y1, int spread) {
    const int mx = (x0 + x1) >> 1;
    if (std::abs(mx - x0) < 9) {
        line(layer, x0, y0, x1, y1);
        return;
    }
    // The magnitude from one step, the sign from bit 6 of the next (a
    // spread of 0 gives 0, as A's division helper returns for /0).
    const int magnitude = spread > 0 ? random_.step() % spread : (random_.step(), 0);
    const int offset = (random_.step() & 0x40) ? magnitude : -magnitude;
    const int my = clamp_row(((y0 + y1) >> 1) + offset);
    segment(layer, x0, y0, mx, my, spread >> 1);
    segment(layer, mx, my, x1, y1, spread >> 1);
}

// Three quarters of the way from the corner to the target, then once or
// twice more halfway on, by chance.
int ZapBolt::toward(int corner, int target) {
    int v = (target + ((target + corner) >> 1)) >> 1;
    ++halvings_;
    if (random_.step() & 0x30) {
        v = (target + v) >> 1;
        ++halvings_;
    }
    if (random_.step() & 3) {
        v = (target + v) >> 1;
        ++halvings_;
    }
    return v;
}

int ZapBolt::draw(std::vector<std::uint8_t>& layer, int x0, int y0, int x1, int y1) {
    y0 = clamp_row(view_top + y0);
    y1 = clamp_row(view_top + y1);
    segment(layer, x0, y0, x1, y1, 0x1F);
    // Branches every 16th frame, or when the mixer says so.
    if ((++frames_ & 0xF) != 0 && (random_.step() & 0x142) != 0) return -1;
    halvings_ = 0; // $20648A
    for (int k = 0; k < 4; ++k) {
        const int bx = toward(corner_x[k], x1);
        const int by = toward(corner_y[k], y1);
        segment(layer, x1, y1, bx, by, 0xF);
    }
    return halvings_;
}

} // namespace mindwalker::landscape
