#include "ui/draw.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace ui {
namespace {
struct Glyph {
    char code;
    std::array<const char*, glyph_rows> rows;
};

// An original proportional alphabet (from BlueStreak's shell). Rows may be omitted
// at the bottom; the first row's length defines the advance width.
constexpr Glyph glyphs[] = {
    {' ', {"..."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"####", "#...", "#...", "###.", "#...", "#...", "####"}},
    {'F', {"####", "#...", "#...", "###.", "#...", "#...", "#..."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'J', {"...#", "...#", "...#", "...#", "...#", "#..#", ".##."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#...", "#...", "#...", "#...", "#...", "#...", "####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'a', {"....", "....", ".##.", "...#", ".###", "#..#", ".###"}},
    {'b', {"#...", "#...", "###.", "#..#", "#..#", "#..#", "###."}},
    {'c', {"....", "....", ".###", "#...", "#...", "#...", ".###"}},
    {'d', {"...#", "...#", ".###", "#..#", "#..#", "#..#", ".###"}},
    {'e', {"....", "....", ".##.", "#..#", "####", "#...", ".###"}},
    {'f', {".##", "#..", "###", "#..", "#..", "#..", "#.."}},
    {'g', {"....", "....", ".###", "#..#", "#..#", "#..#", ".###", "...#", ".##."}},
    {'h', {"#...", "#...", "###.", "#..#", "#..#", "#..#", "#..#"}},
    {'i', {"#", ".", "#", "#", "#", "#", "#"}},
    {'j', {".#", "..", ".#", ".#", ".#", ".#", ".#", ".#", "#."}},
    {'k', {"#...", "#...", "#..#", "#.#.", "##..", "#.#.", "#..#"}},
    {'l', {"#.", "#.", "#.", "#.", "#.", "#.", ".#"}},
    {'m', {".....", ".....", "####.", "#.#.#", "#.#.#", "#.#.#", "#.#.#"}},
    {'n', {"....", "....", "###.", "#..#", "#..#", "#..#", "#..#"}},
    {'o', {"....", "....", ".##.", "#..#", "#..#", "#..#", ".##."}},
    {'p', {"....", "....", "###.", "#..#", "#..#", "#..#", "###.", "#...", "#..."}},
    {'q', {"....", "....", ".###", "#..#", "#..#", "#..#", ".###", "...#", "...#"}},
    {'r', {"....", "....", "#.##", "##..", "#...", "#...", "#..."}},
    {'s', {"....", "....", ".###", "#...", ".##.", "...#", "###."}},
    {'t', {".#.", ".#.", "###", ".#.", ".#.", ".#.", "..#"}},
    {'u', {"....", "....", "#..#", "#..#", "#..#", "#..#", ".###"}},
    {'v', {".....", ".....", "#...#", "#...#", ".#.#.", ".#.#.", "..#.."}},
    {'w', {".....", ".....", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'x', {"....", "....", "#..#", "#..#", ".##.", "#..#", "#..#"}},
    {'y', {"....", "....", "#..#", "#..#", "#..#", "#..#", ".###", "...#", ".##."}},
    {'z', {"....", "....", "####", "...#", ".##.", "#...", "####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###"}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {'.', {".", ".", ".", ".", ".", ".", "#"}},
    {',', {"..", "..", "..", "..", "..", "..", ".#", "#."}},
    {':', {".", ".", "#", ".", ".", ".", "#"}},
    {';', {"..", "..", ".#", "..", "..", "..", ".#", "#."}},
    {'!', {"#", "#", "#", "#", "#", ".", "#"}},
    {'?', {".##.", "#..#", "...#", "..#.", ".#..", "....", ".#.."}},
    {'-', {"...", "...", "...", "###"}},
    {'+', {"...", "...", ".#.", "###", ".#."}},
    {'=', {"....", "....", "####", "....", "####"}},
    {'/', {"..#", "..#", ".#.", ".#.", ".#.", "#..", "#.."}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
    {'\'', {"#", "#"}},
    {'"', {"#.#", "#.#"}},
    {'(', {".#", "#.", "#.", "#.", "#.", "#.", ".#"}},
    {')', {"#.", ".#", ".#", ".#", ".#", ".#", "#."}},
    {'%', {"##..#", "##.#.", "...#.", "..#..", ".#...", ".#.##", "#..##"}},
    {'<', {"...", "..#", ".#.", "#..", ".#.", "..#"}},
    {'>', {"...", "#..", ".#.", "..#", ".#.", "#.."}},
    {'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
    {'_', {"....", "....", "....", "....", "....", "....", "....", "####"}},
    {icon_left, {".....", "..#..", ".#...", "#####", ".#...", "..#.."}},
    {icon_right, {".....", "..#..", "...#.", "#####", "...#.", "..#.."}},
    {icon_up, {"..#..", ".###.", "#.#.#", "..#..", "..#..", "..#.."}},
    {icon_down, {"..#..", "..#..", "..#..", "#.#.#", ".###.", "..#.."}},
    {icon_note, {"..###", "..#.#", "..#.#", "..#.#", "###.#", "###..", "###.."}},
    {icon_play, {"#...", "##..", "###.", "####", "###.", "##..", "#..."}},
    {icon_lock, {".###.", "#...#", "#...#", "#####", "##.##", "##.##", "#####"}},
};

const Glyph* find(char c) {
    for (auto& g : glyphs) {
        if (g.code == c) {
            return &g;
        }
    }
    return nullptr;
}

int advance(char c) {
    auto g = find(c);
    return g ? int(std::char_traits<char>::length(g->rows[0])) + 1 : 4;
}

void glyph(Image& im, const Glyph& g, int x, int y, int scale, Color c) {
    for (int row = 0; row < glyph_rows; ++row) {
        if (!g.rows[row]) {
            break;
        }
        for (int col = 0; g.rows[row][col]; ++col) {
            if (g.rows[row][col] == '#') {
                rect(im, x + col * scale, y + row * scale, scale, scale, c);
            }
        }
    }
}

void raw_text(Image& im, std::string_view value, int x, int y, int scale, Color c) {
    for (char ch : value) {
        if (auto g = find(ch)) {
            glyph(im, *g, x, y, scale, c);
        }
        x += advance(ch) * scale;
    }
}
} // namespace

int text_width(std::string_view value, int scale) {
    int w = 0;
    for (char ch : value) {
        w += advance(ch);
    }
    return std::max(0, w - 1) * scale;
}

int text(Image& im, std::string_view value, int x, int y, int scale, Color color, Ink ink,
         Color edge) {
    if (ink == Ink::shadow) {
        raw_text(im, value, x + scale, y + scale, scale, edge);
    } else if (ink == Ink::outline) {
        // A one-pixel (scaled) outline keeps light text legible on patterns.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx || dy) {
                    raw_text(im, value, x + dx * scale, y + dy * scale, scale, edge);
                }
            }
        }
        raw_text(im, value, x + scale, y + 2 * scale, scale, edge);
    }
    raw_text(im, value, x, y, scale, color);
    return x + text_width(value, scale);
}

int text_right(Image& im, std::string_view value, int right, int y, int scale, Color color, Ink ink,
               Color edge) {
    return text(im, value, right - text_width(value, scale), y, scale, color, ink, edge);
}

int wrapped(Image& im, std::string_view value, int x, int y, int width, int max_lines, int scale,
            Color color) {
    int lines = 0;
    while (!value.empty() && lines < max_lines) {
        std::size_t take = value.size();
        while (take > 0 && text_width(value.substr(0, take), scale) > width) {
            auto split = value.rfind(' ', take - 1);
            take = split == std::string_view::npos || split == 0 ? take - 1 : split;
        }
        take = std::max<std::size_t>(take, 1);
        text(im, value.substr(0, take), x, y + lines * line_rows * scale, scale, color);
        value.remove_prefix(take);
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        ++lines;
    }
    return lines;
}

Color mix(Color a, Color b, int t) {
    t = std::clamp(t, 0, 255);
    auto m = [t](int p, int q) {
        return std::uint8_t(p + (q - p) * t / 255);
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

void fill(Image& im, Color c) {
    std::fill(im.pixels.begin(), im.pixels.end(), c);
}

void rect(Image& im, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0), x1 = std::min(x + w, im.width);
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        std::fill_n(im.pixels.begin() + std::size_t(yy) * im.width + x0, std::max(0, x1 - x0), c);
    }
}

void blend_rect(Image& im, int x, int y, int w, int h, Color c, int alpha) {
    // Source-over compositing, so translucent strips also work on overlay layers.
    const int a = std::clamp(alpha, 0, 255);
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        for (int xx = std::max(x, 0); xx < std::min(x + w, im.width); ++xx) {
            auto& p = im.pixels[std::size_t(yy) * im.width + xx];
            const int keep = p.a * (255 - a) / 255, out = a + keep;
            if (!out) {
                continue;
            }
            auto ch = [&](int src, int dst) {
                return std::uint8_t((src * a + dst * keep) / out);
            };
            p = {ch(c.r, p.r), ch(c.g, p.g), ch(c.b, p.b), std::uint8_t(out)};
        }
    }
}

namespace {
// Horizontal inset of a pixel-stepped rounded corner at row distance d.
int corner_inset(int radius, int d) {
    if (d >= radius) {
        return 0;
    }
    const double dy = radius - d - 0.5;
    return radius - int(std::lround(std::sqrt(double(radius * radius) - dy * dy)));
}
} // namespace

void rounded(Image& im, int x, int y, int w, int h, int radius, Color c) {
    radius = std::min({radius, w / 2, h / 2});
    for (int row = 0; row < h; ++row) {
        int inset = corner_inset(radius, std::min(row, h - 1 - row));
        rect(im, x + inset, y + row, w - 2 * inset, 1, c);
    }
}

void slanted(Image& im, int x, int y, int w, int h, int slant, Color c) {
    for (int row = 0; row < h; ++row) {
        int shift = h > 1 ? slant * (h - 1 - row) / (h - 1) : 0;
        rect(im, x + shift, y + row, w, 1, c);
    }
}

void checker(Image& im, int x, int y, int w, int h, int cell, Color a, Color b, int offset) {
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        for (int xx = std::max(x, 0); xx < std::min(x + w, im.width); ++xx) {
            im.pixels[std::size_t(yy) * im.width + xx] =
                (((xx + offset) / cell + yy / cell) & 1) ? b : a;
        }
    }
}

void stripes(Image& im, int x, int y, int w, int h, int period, int thickness, Color c,
             int offset) {
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        for (int xx = std::max(x, 0); xx < std::min(x + w, im.width); ++xx) {
            int phase = ((xx + yy / 2 + offset) % period + period) % period;
            if (phase < thickness) {
                im.pixels[std::size_t(yy) * im.width + xx] = c;
            }
        }
    }
}

void dots(Image& im, int x, int y, int w, int h, int period, Color c) {
    for (int yy = std::max(y, 0); yy < std::min(y + h, im.height); ++yy) {
        for (int xx = std::max(x, 0); xx < std::min(x + w, im.width); ++xx) {
            if (xx % period < 3 && yy % period < 3) {
                im.pixels[std::size_t(yy) * im.width + xx] = c;
            }
        }
    }
}

void ring(Image& im, int cx, int cy, int radius, int thickness, Color c, int x0, int y0, int x1,
          int y1) {
    const int outer = radius * radius, inner = std::max(0, radius - thickness);
    for (int yy = std::max({cy - radius, y0, 0}); yy < std::min({cy + radius, y1, im.height});
         ++yy) {
        for (int xx = std::max({cx - radius, x0, 0}); xx < std::min({cx + radius, x1, im.width});
             ++xx) {
            int d = (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy);
            if (d < outer && d >= inner * inner) {
                im.pixels[std::size_t(yy) * im.width + xx] = c;
            }
        }
    }
}

void picture(Image& im, const Image& source, int x, int y, int w, int h) {
    double scale = std::min(double(w) / source.width, double(h) / source.height);
    if (scale >= 1) {
        scale = std::floor(scale);
    }
    const int dw = std::max(1, int(source.width * scale)), dh = std::max(1, int(source.height * scale));
    const int left = x + (w - dw) / 2, top = y + (h - dh) / 2;
    for (int yy = 0; yy < dh; ++yy) {
        const int sy = std::min(source.height - 1, int(yy / scale));
        for (int xx = 0; xx < dw; ++xx) {
            const int sx = std::min(source.width - 1, int(xx / scale));
            im.put(left + xx, top + yy, source.pixels[std::size_t(sy) * source.width + sx]);
        }
    }
}

int key_hint(Image& im, std::string_view key, std::string_view action, int x, int y) {
    const int w = text_width(key, 2) + 12;
    rounded(im, x, y, w, 22, 4, {236, 240, 250});
    rect(im, x + 2, y + 20, w - 4, 2, {150, 160, 186});
    text(im, key, x + 6, y + 4, 2, {20, 28, 60});
    return text(im, action, x + w + 8, y + 4, 2, {226, 232, 246}) + 22;
}

double ease_out(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

double ease_back(double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double c = 1.7, u = t - 1;
    return 1 + (c + 1) * u * u * u + c * u * u;
}
} // namespace ui
