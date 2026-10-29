#include "app/shell_style.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace doc::shell {
namespace {
struct Glyph {
    char code;
    std::array<const char*, glyph_rows> rows;
};

// An original proportional alphabet drawn for this shell. Rows may be omitted
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

}  // namespace doc::shell
