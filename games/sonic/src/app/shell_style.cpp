#include "app/shell_style.hpp"
#include "platform/type.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>

namespace shell::style {
namespace {
// Cache complete labels so the 60 Hz shell never rerasterizes static type.
// Private control characters keep the existing navigation icon API.
const platform::TypesetLine& line(std::string_view value, int scale) {
    static std::map<std::pair<std::string, int>, platform::TypesetLine> cache;
    auto key = std::pair{std::string(value), scale};
    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    if (cache.size() > 512) {
        cache.clear();
    }
    std::vector<platform::TypesetLine> runs;
    for (std::size_t i = 0; i < value.size();) {
        const unsigned char c = value[i];
        if (c > 0 && c < 8) {
            // Authored control glyphs: no dependency on a font's symbol coverage.
            static constexpr std::array<std::array<const char*, 7>, 7> icons = {
                {{{"..#..", ".#...", "#####", ".#...", "..#..", ".....", "....."}},
                 {{"..#..", "...#.", "#####", "...#.", "..#..", ".....", "....."}},
                 {{"..#..", ".###.", "#.#.#", "..#..", "..#..", ".....", "....."}},
                 {{"..#..", "..#..", "#.#.#", ".###.", "..#..", ".....", "....."}},
                 {{"..###", "..#.#", "..#.#", "..#.#", "###.#", "###..", "....."}},
                 {{"#....", "##...", "###..", "####.", "###..", "##...", "#...."}},
                 {{".###.", "#...#", "#...#", "#####", "##.##", "##.##", "#####"}}}};
            Image icon(7 * scale, 11 * scale);
            std::fill(icon.pixels.begin(), icon.pixels.end(), Color{0, 0, 0, 0});
            for (int y = 0; y < 7; ++y) {
                for (int x = 0; x < 5; ++x) {
                    if (icons[c - 1][y][x] == '#') {
                        rect(icon, x * scale, (y + 2) * scale, scale, scale, white);
                    }
                }
            }
            runs.push_back({std::move(icon), 7 * scale});
            ++i;
        } else {
            auto end = i + 1;
            while (end < value.size() &&
                   (static_cast<unsigned char>(value[end]) >= 8 || value[end] == 0)) {
                ++end;
            }
            runs.push_back(platform::typeset(value.substr(i, end - i), scale * 9, scale >= 3));
            i = end;
        }
    }
    int width = 0, height = scale * 12;
    for (const auto& run : runs) {
        width += run.advance;
        height = std::max(height, run.coverage.height);
    }
    Image mask(std::max(1, width + 4), height);
    std::fill(mask.pixels.begin(), mask.pixels.end(), Color{0, 0, 0, 0});
    int x = 0;
    for (const auto& run : runs) {
        for (int yy = 0; yy < run.coverage.height; ++yy) {
            for (int xx = 0; xx < run.coverage.width; ++xx) {
                auto p = run.coverage.pixels[std::size_t(yy) * run.coverage.width + xx];
                if (p.a) {
                    mask.put(x + xx, yy, p);
                }
            }
        }
        x += run.advance;
    }
    return cache.emplace(std::move(key), platform::TypesetLine{std::move(mask), width})
        .first->second;
}
} // namespace

int text_width(std::string_view value, int scale) {
    return line(value, scale).advance;
}

int text(Image& im, std::string_view value, int x, int y, int scale, Color color, Ink, Color) {
    const auto& typeset = line(value, scale);
    const auto& mask = typeset.coverage;
    // Align the new cap height to the old API's top edge.
    y -= scale * 2;
    for (int yy = 0; yy < mask.height; ++yy) {
        for (int xx = 0; xx < mask.width; ++xx) {
            const int a = mask.pixels[std::size_t(yy) * mask.width + xx].a;
            if (!a || x + xx < 0 || y + yy < 0 || x + xx >= im.width || y + yy >= im.height) {
                continue;
            }
            auto& p = im.pixels[std::size_t(y + yy) * im.width + x + xx];
            const int keep = p.a * (255 - a) / 255, out = a + keep;
            auto ch = [&](int src, int dst) {
                return std::uint8_t((src * a + dst * keep) / out);
            };
            p = {ch(color.r, p.r), ch(color.g, p.g), ch(color.b, p.b), std::uint8_t(out)};
        }
    }
    return x + typeset.advance;
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

void backdrop(Image& im, const Theme& t, int x, int y, int w, int h, int scroll) {
    rect(im, x, y, w, h, t.bg);
    // Quiet registration lines and an oversized racing arc, away from text.
    for (int yy = y + 32; yy < y + h; yy += 64) {
        rect(im, x, yy, w, 1, t.bg_alt);
    }
    ring(im, x + w - 40, y + h / 2, w / 3, 1, t.bg_alt, x, y, x + w, y + h);
    (void)scroll;
}

void panel(Image& im, const Theme& t, int x, int y, int w, int h, Color fill_color, Color border) {
    rounded(im, x, y, w, h, 10, border);
    rounded(im, x + 1, y + 1, w - 2, h - 2, 9, fill_color);
    (void)t;
}

void badge(Image& im, const Theme& t, std::string_view label, int x, int y, int scale) {
    const int w = text_width(label, scale) + 6 * scale, h = 11 * scale;
    if (t.radius) {
        rounded(im, x, y, w, h, 2 * scale, t.accent);
    } else {
        rect(im, x, y, w, h, t.accent);
    }
    text(im, label, x + 3 * scale, y + 2 * scale, scale, t.ink);
}

int key_hint(Image& im, std::string_view key, std::string_view action, int x, int y) {
    const int w = text_width(key, 2) + 12;
    rounded(im, x, y, w, 22, 4, {236, 240, 250});
    rect(im, x + 2, y + 20, w - 4, 2, {150, 160, 186});
    text(im, key, x + 6, y + 4, 2, {20, 28, 60});
    return text(im, action, x + w + 8, y + 4, 2, {226, 232, 246}) + 22;
}

double ease_out(double t) {
    if (reduce_motion) {
        return 1;
    }
    t = std::clamp(t, 0.0, 1.0);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

double ease_back(double t) {
    if (reduce_motion) {
        return 1;
    }
    t = std::clamp(t, 0.0, 1.0);
    const double c = 1.7, u = t - 1;
    return 1 + (c + 1) * u * u * u + c * u * u;
}
} // namespace shell::style
