#include "tower_of_babel_amiga/view/polygon.hpp"
#include <cstdint>

namespace babel::view {

Pen read_pen(std::span<const std::uint8_t, 16> bytes) {
    Pen pen;
    for (int row = 0; row < 2; ++row) {
        for (int x = 0; x < 16; ++x) {
            std::uint8_t colour = 0;
            for (int plane = 0; plane < 4; ++plane) {
                const std::size_t at = std::size_t(row * 8 + plane * 2);
                const unsigned word = unsigned(bytes[at]) << 8 | bytes[at + 1];
                colour |= std::uint8_t((word >> (15 - x) & 1) << plane);
            }
            pen.rows[std::size_t(row)][std::size_t(x)] = colour;
        }
    }
    return pen;
}

namespace {

// The clipper's intersections ($149C6 / $149F4): 14-bit fixed point, the
// ratio truncated by a 16-bit signed division, the product shifted
// arithmetically (rounding towards minus infinity).
int cross(int at, int a0, int a1, int b0, int b1) {
    if (a1 < a0) { std::swap(a0, a1); std::swap(b0, b1); }
    const auto ratio = std::int16_t(std::int32_t(std::int16_t(at - a0)) * 16384 / std::int16_t(a1 - a0));
    const std::int32_t product = std::int32_t(ratio) * std::int16_t(b1 - b0);
    return b0 + (product >> 14);
}

enum class Side { left, right, top, bottom };

bool outside(const Point& p, Side side, const Window& w) {
    switch (side) {
    case Side::left: return p.x < w.left;
    case Side::right: return p.x > w.right;
    case Side::top: return p.y < w.top;
    default: return p.y > w.bottom;
    }
}

Point crossing(const Point& a, const Point& b, Side side, const Window& w) {
    switch (side) {
    case Side::left: return {w.left, cross(w.left, a.x, b.x, a.y, b.y)};
    case Side::right: return {w.right, cross(w.right, a.x, b.x, a.y, b.y)};
    case Side::top: return {cross(w.top, a.y, b.y, a.x, b.x), w.top};
    default: return {cross(w.bottom, a.y, b.y, a.x, b.x), w.bottom};
    }
}

// One pass of $147E0 against one side. The original walks the list in place
// with a copy of the first point after the last as the closing point; that
// copy is taken before the pass and not updated by it.
void clip_side(std::vector<Point>& p, Side side, const Window& w) {
    if (p.empty()) return;
    p.push_back(p.front()); // the closing point
    std::size_t i = 0;
    while (i + 1 < p.size()) {
        const bool here_out = outside(p[i], side, w), next_out = outside(p[i + 1], side, w);
        if (here_out && next_out) {
            p.erase(p.begin() + std::ptrdiff_t(i)); // dropped; the same slot is tested again
            if (p.size() <= 1) { p.clear(); return; }
            continue;
        }
        if (here_out) {
            p[i] = crossing(p[i], p[i + 1], side, w);
        } else if (next_out) {
            p.insert(p.begin() + std::ptrdiff_t(i) + 1, crossing(p[i], p[i + 1], side, w));
            ++i; // step over the new point
        }
        ++i;
    }
    p.pop_back();
}

// Final pass: a point equal to the next one (the closing copy included) goes.
void drop_repeats(std::vector<Point>& p) {
    if (p.empty()) return;
    p.push_back(p.front());
    std::size_t i = 0;
    while (i + 1 < p.size()) {
        if (p[i] == p[i + 1]) {
            p.erase(p.begin() + std::ptrdiff_t(i));
            if (p.size() <= 1) { p.clear(); return; }
            continue;
        }
        ++i;
    }
    p.pop_back();
}

void span(Canvas& canvas, int y, int x0, int x1, const Pen& pen) {
    if (y < 0 || y >= canvas.height) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= canvas.width) x1 = canvas.width - 1;
    for (int x = x0; x <= x1; ++x) canvas.set(x, y, pen.colour(x, y));
}

// An edge's step per line ($13EB0): (dx * 256 / dy) as a 16-bit signed
// quotient, as 16.16 fixed point. When the quotient does not fit, the
// 68000's DIVS leaves the dividend, and its low word is used instead.
std::int32_t slope(int dx, int dy) {
    const std::int32_t dividend = std::int32_t(std::int16_t(dx)) * 256;
    std::int32_t quotient = dividend / dy;
    if (quotient < -32768 || quotient > 32767) quotient = std::int16_t(dividend & 0xFFFF);
    return std::int32_t(std::int16_t(quotient)) * 256;
}

// An x position as the filler keeps it: the integer in the low word, the
// fraction in the high word of one register. Setting a vertex x replaces
// only the integer; the fraction stays.
struct Edge {
    std::int32_t fixed{}; // integer << 16 | fraction
    int x() const { return std::int16_t(std::uint32_t(fixed) >> 16); }
    void set_x(int x) { fixed = std::int32_t((std::uint32_t(std::uint16_t(x)) << 16) | (std::uint32_t(fixed) & 0xFFFF)); }
    void step(std::int32_t delta) { fixed = std::int32_t(std::uint32_t(fixed) + std::uint32_t(delta)); }
};

void scan(Canvas& canvas, const std::vector<Point>& p, const Pen& pen) {
    const std::size_t n = p.size();
    const auto next = [&](std::size_t i) { return i + 1 == n ? 0 : i + 1; };
    const auto prev = [&](std::size_t i) { return i == 0 ? n - 1 : i - 1; };

    // The top vertex: least y, then least x.
    std::size_t top = 0;
    int ty = 0x7000, tx = 0x7000;
    for (std::size_t i = 0; i < n; ++i) {
        if (p[i].y < ty || (p[i].y == ty && p[i].x < tx)) { ty = p[i].y; tx = p[i].x; top = i; }
    }
    int y = ty;
    Edge left, right;
    left.set_x(tx);
    right.set_x(tx);
    std::size_t l = top, r = top;
    std::int32_t left_step = 0, right_step = 0;
    int left_lines = 0, right_lines = 0;

    const auto finish = [&]() {
        if (left.x() < right.x()) span(canvas, y, left.x(), right.x(), pen);
    };

    // The right-hand side's first edge; flat edges along the top extend it.
    for (;;) {
        r = next(r);
        right_lines = p[r].y - y;
        if (right_lines < 0) return;
        if (right_lines > 0) break;
        if (right.x() >= p[r].x) { finish(); return; }
        right.set_x(p[r].x);
    }
    right_step = slope(p[r].x - right.x(), right_lines);
    // The left-hand side's first edge.
    l = prev(l);
    left_lines = p[l].y - y;
    if (left_lines < 0) return;
    if (left_lines == 0) { finish(); return; }
    left_step = slope(p[l].x - left.x(), left_lines);

    for (;;) {
        if (left.x() <= right.x()) span(canvas, y, left.x(), right.x(), pen);
        ++y;
        left.step(left_step);
        right.step(right_step);
        if (--left_lines == 0) {
            left.set_x(p[l].x);
            l = prev(l);
            left_lines = p[l].y - y;
            if (left_lines <= 0) { right.set_x(p[r].x); finish(); return; }
            left_step = slope(p[l].x - left.x(), left_lines);
        }
        if (--right_lines == 0) {
            right.set_x(p[r].x);
            // Further points: a flat edge to the right is passed over
            // without taking its x ($1403C); one to the left ends the face.
            for (;;) {
                r = next(r);
                right_lines = p[r].y - y;
                if (right_lines < 0) { left.set_x(p[l].x); finish(); return; }
                if (right_lines > 0) break;
                if (right.x() >= p[r].x) { left.set_x(p[l].x); finish(); return; }
            }
            right_step = slope(p[r].x - right.x(), right_lines);
        }
    }
}

} // namespace

void fill_polygon(Canvas& canvas, const Window& window, std::vector<Point> points, const Pen& pen) {
    for (Side side : {Side::left, Side::right, Side::top, Side::bottom}) clip_side(points, side, window);
    drop_repeats(points);
    if (points.size() < 3) return;
    scan(canvas, points, pen);
}

void fill_rectangle(Canvas& canvas, const Window& w, int left, int top, int right, int bottom, const Pen& pen) {
    if (left > w.right || top > w.bottom || right < w.left || bottom < w.top) return;
    if (left < w.left) left = w.left;
    if (right > w.right) right = w.right;
    if (top < w.top) top = w.top;
    if (bottom > w.bottom) bottom = w.bottom;
    for (int y = top; y <= bottom; ++y) span(canvas, y, left, right, pen);
}

} // namespace babel::view
