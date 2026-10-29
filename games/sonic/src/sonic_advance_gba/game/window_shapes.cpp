#include "sonic_advance_gba/game/window_shapes.hpp"
#include "sonic_advance_gba/game/video.hpp"
#include <cstdlib>

namespace sonic_advance_gba {

namespace {

// Byte access to the table being filled. Like the original's tables, which
// lie one after the other in memory, a write past the end of the first
// table goes into the second; past the second it reaches the sprite colour
// buffer that follows them.
class LineBytes {
public:
    LineBytes(VideoState& v, u8 which) : v_(v) {
        v.line_effect_on = true;
        if (which < 2) {
            v.line_bytes = 2;
            v.line_target =
                (which & 1) ? VideoState::LineTarget::window1 : VideoState::LineTarget::window0;
        } else {
            v.line_bytes = 4;
            v.line_target = VideoState::LineTarget::windows;
            if (which & 1) {
                start_ = 2;
            }
        }
    }

    unsigned stride() const { return v_.line_bytes; }

    unsigned line(unsigned y) const { return start_ + y * stride(); }

    void put(unsigned at, u8 value) {
        const unsigned table = v_.line_table;
        unsigned o = table * 0x280 + at;
        if (o < 0x280) {
            v_.line_tables[0].bytes[o] = value;
        } else if (o < 0x500) {
            v_.line_tables[1].bytes[o - 0x280] = value;
        } else if (o < 0x700) {
            // 03001920: the sprite colours (not marked for upload).
            const unsigned c = (o - 0x500) / 2;
            u16& colour = v_.sprite_colors[c];
            colour =
                (o & 1) ? u16((colour & 0x00FF) | (value << 8)) : u16((colour & 0xFF00) | value);
        } else {
            throw Unrecovered("window edge written far beyond the line tables");
        }
    }

private:
    VideoState& v_;
    unsigned start_{};
};

// The shared Bresenham steps. Each line's first byte is the window's right
// side, the second its left side; `edge_right` draws the edge as the right
// side (with the other side fixed at `other`), else as the left side.
void edge_line(VideoState& v, u8 which, u8 x0, u8 y0, u8 x1, u8 y1, u8 other, bool edge_right) {
    LineBytes t(v, which);
    u8 x = x0;
    const s16 dx = s16(x1 - x0), dy = s16(y1 - y0);
    const int adx = std::abs(dx), ady = std::abs(dy);
    const s16 adx2 = s16(u16(adx << 1)), ady2 = s16(u16(ady << 1));
    const auto right = [&] {
        return edge_right ? x : other;
    };
    const auto left = [&] {
        return edge_right ? other : x;
    };
    unsigned at = t.line(y0);
    if (adx > ady) {
        // More columns than lines: a line is written when the edge moves down.
        s16 err = s16(-dx);
        if (dx > 0) {
            t.put(at + 1, left());
            for (s16 i = 0; i < adx; ++i) {
                ++x;
                err = s16(err + ady2);
                if (err >= 0) {
                    err = s16(err - adx2);
                    t.put(at, right());
                    at += t.stride();
                    t.put(at + 1, left());
                }
            }
            t.put(at, right());
        } else {
            t.put(at, right());
            unsigned p = at + 1;
            for (s16 i = 0; i < adx; ++i) {
                --x;
                err = s16(err + ady2);
                if (err >= 0) {
                    err = s16(err - adx2);
                    t.put(p, left());
                    p = p + t.stride() - 1;
                    t.put(p, right());
                    ++p;
                }
            }
            t.put(p, left());
        }
        return;
    }
    // More lines than columns: every line is written; a right edge ends one
    // pixel after the edge.
    s16 err = s16(-dy);
    for (s16 i = 0; i < ady; ++i) {
        t.put(at, edge_right ? u8(x + 1) : other);
        t.put(at + 1, left());
        at += t.stride();
        err = s16(err + adx2);
        if (err >= 0) {
            x = dx > 0 ? u8(x + 1) : u8(x - 1);
            err = s16(err - ady2);
        }
    }
}

} // namespace

void window_right_edge(VideoState& v, u8 which, u8 x0, u8 y0, u8 x1, u8 y1, u8 left) {
    edge_line(v, which, x0, y0, x1, y1, left, true);
}

void window_left_edge(VideoState& v, u8 which, u8 x0, u8 y0, u8 x1, u8 y1, u8 right) {
    edge_line(v, which, x0, y0, x1, y1, right, false);
}

void window_wedge(VideoState& v, u8 which, const std::array<u8, 8>& p) {
    LineBytes t(v, which);
    const u8 ya = p[1], yb = p[5];
    unsigned line = ya < yb ? ya : yb;
    const unsigned first_end = ya < yb ? yb : ya;
    unsigned at = t.line(line);
    // Edge A (left): p0,p1 -> p2,p3; edge B (right): p4,p5 -> p6,p7.
    const s16 dxa = s16(p[2] - p[0]), dya = s16(p[3] - p[1]);
    const s16 dxb = s16(p[6] - p[4]), dyb = s16(p[7] - p[5]);
    const u16 adxa2 = u16(std::abs(dxa) << 1), adya2 = u16(std::abs(dya) << 1);
    const u16 adxb2 = u16(std::abs(dxb) << 1), adyb2 = u16(std::abs(dyb) << 1);
    u8 xa = p[0], xb = p[4];
    u16 erra = u16(-dya), errb = u16(-dyb);
    // The steps along each edge after a line (the right edge's +1 makes it
    // exclusive).
    const auto step_a = [&] {
        while (s16(erra) >= 0) {
            if (dxa > 0) {
                ++xa;
                erra = u16(erra - adya2);
            } else {
                --xa;
                erra = u16(erra - adya2);
                if (s16(erra) >= 0) {
                    t.put(at + 1, xa);
                }
            }
        }
    };
    const auto step_b = [&] {
        while (s16(errb) >= 0) {
            if (dxb > 0) {
                ++xb;
                errb = u16(errb - adyb2);
                if (s16(errb) >= 0) {
                    t.put(at, u8(xb + 1));
                }
            } else {
                --xb;
                errb = u16(errb - adyb2);
            }
        }
    };
    if (line != first_end) {
        if (ya < yb) {
            // Only the left edge: the window reaches the right side.
            for (; line < first_end; line = (line + 1) & 0xFF) {
                t.put(at, 0xF0);
                t.put(at + 1, xa);
                erra = u16(erra + adxa2);
                step_a();
                at += t.stride();
            }
        } else {
            // Only the right edge: the window starts at the left side.
            for (; line < first_end; line = (line + 1) & 0xFF) {
                t.put(at, xb);
                t.put(at + 1, 0);
                errb = u16(errb + adxb2);
                step_b();
                at += t.stride();
            }
        }
    }
    const unsigned end = p[7] < p[3] ? p[7] : p[3];
    for (; line < end; line = (line + 1) & 0xFF) {
        t.put(at, xb);
        t.put(at + 1, xa);
        erra = u16(erra + adxa2);
        errb = u16(errb + adxb2);
        step_a();
        step_b();
        at += t.stride();
    }
}

void window_band(VideoState& v, u8 which, const std::array<u8, 8>& p, s32 limit) {
    LineBytes t(v, which);
    const u8 ya = p[1], yb = p[5];
    unsigned line = ya < yb ? ya : yb;
    const unsigned first_end = ya < yb ? yb : ya;
    unsigned at = t.line(line);
    // Edge A (left): p0,p1 -> p2,p3; edge B (right): p4,p5 -> p6,p7.
    const s16 dxa = s16(p[2] - p[0]), dya = s16(p[3] - p[1]);
    const s16 dxb = s16(p[6] - p[4]), dyb = s16(p[7] - p[5]);
    const u16 adxa2 = u16(std::abs(dxa) << 1), adya2 = u16(std::abs(dya) << 1);
    const u16 adxb2 = u16(std::abs(dxb) << 1), adyb2 = u16(std::abs(dyb) << 1);
    u8 xa = p[0], xb = p[4];
    u16 erra = u16(-dya), errb = u16(-dyb);
    // The steps along each edge after a line, within 0..limit (the right
    // edge's +1 makes it exclusive).
    const auto step_a = [&] {
        while (s16(erra) >= 0) {
            if (dxa > 0) {
                if (xa < limit) {
                    ++xa;
                }
                erra = u16(erra - adya2);
            } else {
                if (xa != 0) {
                    --xa;
                }
                erra = u16(erra - adya2);
                if (s16(erra) >= 0) {
                    t.put(at + 1, xa);
                }
            }
        }
    };
    const auto step_b = [&] {
        while (s16(errb) >= 0) {
            if (dxb > 0) {
                if (xb < limit) {
                    ++xb;
                }
                errb = u16(errb - adyb2);
                if (s16(errb) >= 0) {
                    t.put(at, u8(xb + 1));
                }
            } else {
                if (xb != 0) {
                    --xb;
                }
                errb = u16(errb - adyb2);
            }
        }
    };
    const auto only_a = [&](unsigned end) {
        for (; line < end; line = (line + 1) & 0xFF) {
            t.put(at, u8(limit));
            t.put(at + 1, xa);
            erra = u16(erra + adxa2);
            step_a();
            at += t.stride();
        }
    };
    const auto only_b = [&](unsigned end) {
        for (; line < end; line = (line + 1) & 0xFF) {
            t.put(at, xb);
            t.put(at + 1, 0);
            errb = u16(errb + adxb2);
            step_b();
            at += t.stride();
        }
    };
    if (line != first_end) {
        if (ya < yb) {
            only_a(first_end);
        } else {
            only_b(first_end);
        }
    }
    const unsigned end = p[7] < p[3] ? p[7] : p[3];
    for (; line < end; line = (line + 1) & 0xFF) {
        t.put(at, xb);
        t.put(at + 1, xa);
        erra = u16(erra + adxa2);
        errb = u16(errb + adxb2);
        step_a();
        step_b();
        at += t.stride();
    }
    // Below the shorter edge, the longer one alone.
    if (p[7] < p[3]) {
        only_a(p[3]);
    } else {
        only_b(p[7]);
    }
}

} // namespace sonic_advance_gba
