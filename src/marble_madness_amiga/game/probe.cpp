#include "marble_madness_amiga/game/probe.hpp"

namespace marble_madness::game {
namespace {

std::int16_t s16(int value) { return std::int16_t(value); }

} // namespace

void Probe::place(std::int32_t x, std::int32_t y) {
    x_ = std::int16_t(x >> 16);
    y_ = std::int16_t(y >> 16);
    fx_ = x_ & 7;
    fy_ = y_ & 7;
    cell_x_ = std::int16_t(x_ >> 3);
    cell_y_ = std::int16_t(y_ >> 3);
}

void Probe::locate(const Terrain& terrain, std::int32_t x, std::int32_t y, std::int32_t z) {
    const auto old_x = cell_x_, old_y = cell_y_;
    z_ = std::int16_t(z >> 16);
    place(x, y);
    if (old_x != cell_x_ || old_y != cell_y_) {
        reload(terrain);
    }
}

void Probe::reload(const Terrain& terrain) {
    v_[0] = terrain.vertex(cell_x_, cell_y_ + 1);
    v_[1] = terrain.vertex(cell_x_ + 1, cell_y_ + 1);
    v_[2] = terrain.vertex(cell_x_ + 1, cell_y_);
    v_[3] = terrain.vertex(cell_x_, cell_y_);
}

std::int32_t Probe::ground_height() {
    // The cell's own heights at its corners (0,0), (8,0), (0,8), (8,8).
    const std::int16_t h00 = v_[3].height[Vertex::plus_x_plus_y];
    const std::int16_t h80 = v_[2].height[Vertex::minus_x_plus_y];
    const std::int16_t h08 = v_[0].height[Vertex::plus_x_minus_y];
    const std::int16_t h88 = v_[1].height[Vertex::minus_x_minus_y];
    if (fx_ <= fy_) { // the triangle (0,0) (0,8) (8,8)
        slope_x = s16(h88 - h08);
        slope_y = s16(h08 - h00);
    } else {          // the triangle (0,0) (8,0) (8,8)
        slope_x = s16(h80 - h00);
        slope_y = s16(h88 - h80);
    }
    return h00 * 0x10000 + (slope_y * fy_ + slope_x * fx_) * 0x2000;
}

void Probe::analyse_sides() {
    const auto& a = v_[0].height; // vertex (cx, cy + 1)
    const auto& b = v_[1].height; // (cx + 1, cy + 1)
    const auto& c = v_[2].height; // (cx + 1, cy)
    const auto& d = v_[3].height; // (cx, cy)
    side_kind = {};
    corner_bits = 0;
    // One side: `inner` are the heights this cell has at the side's two
    // ends, `outer` the neighbour's. A wall when the neighbour is higher at
    // either end (kind 1), a drop otherwise (kind 3); one more when the
    // neighbour's two ends are level. The height starts at `*_base` and moves
    // by (*_a - *_b) * along / 8 across the side ($10F390 as written).
    auto side = [&](int n, std::int16_t inner0, std::int16_t outer0, std::int16_t inner1, std::int16_t outer1,
                    std::int16_t up_base, std::int16_t up_a, std::int16_t up_b, std::int16_t down_base,
                    std::int16_t down_a, std::int16_t down_b, int along) {
        if (outer0 == inner0 && outer1 == inner1) {
            return;
        }
        auto& kind = side_kind[std::size_t(n)];
        auto& height = side_height[std::size_t(n)];
        const bool wall = inner0 < outer0 || inner1 < outer1;
        kind = wall ? 1 : 3;
        height = wall ? up_base : down_base;
        if (outer0 == outer1) {
            ++kind;
        }
        if (kind == 1) {
            height = s16(height + (s16((up_a - up_b) * along + 4) >> 3));
        } else if (kind == 3) {
            height = s16(height + (s16((down_a - down_b) * along + 4) >> 3));
        }
    };
    // $1006BC .. $1006BF.
    side(0, a[2], a[3], d[1], d[0], d[0], a[3], d[0], d[1], a[2], d[1], fy_);
    side(1, a[2], a[1], b[3], b[0], a[1], b[0], a[1], a[2], b[3], a[2], fx_);
    side(2, c[0], c[1], b[3], b[2], b[2], b[2], c[1], b[3], b[3], c[0], fy_);
    side(3, c[0], c[3], d[1], d[2], c[3], c[3], d[2], c[0], c[0], d[1], fx_);
    // Corners: one vertex's value above its three others.
    auto peak = [&](int bit, int slot, const std::array<std::int16_t, 4>& h, int top) {
        const std::int16_t t = h[std::size_t(top)];
        for (int k = 0; k < 4; ++k) {
            if (k != top && !(h[std::size_t(k)] < t)) {
                return;
            }
        }
        corner_bits = std::uint8_t(corner_bits | bit);
        corner_height[std::size_t(slot)] = t;
    };
    peak(0x01, 0, a, 0);
    peak(0x02, 1, b, 1);
    peak(0x04, 2, c, 2);
    peak(0x08, 3, d, 3);
    peak(0x10, 0, a, 2);
    peak(0x20, 1, b, 3);
    peak(0x40, 2, c, 0);
    peak(0x80, 3, d, 1);
    // Relative to the point. The game subtracts from every height, also
    // those it did not set this time (kept from earlier calls).
    for (auto& h : side_height) {
        h = s16(h - z_);
    }
    for (auto& h : corner_height) {
        h = s16(h - z_);
    }
}

} // namespace marble_madness::game
