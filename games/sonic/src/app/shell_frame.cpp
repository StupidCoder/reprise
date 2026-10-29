#include "app/shell_frame.hpp"
#include "platform/type.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace shell {

const Theme& theme(int generation) {
    return generations[std::size_t(generation)].theme;
}

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

int lerp(int a, int b, double t) {
    return int(std::lround(a + (b - a) * t));
}

// Blue/red split with scrolling speed lines: BlueStreak's own frame.
void frame_background(Image& im, double time) {
    backdrop(im, theme(0), 0, 0, im.width, im.height);
    // A slow travelling dash on a fixed rail gives the home screen life.
    rect(im, 36, 126, im.width - 72, 1, {44, 61, 82});
    const int dash = reduce_motion ? 0 : int(std::fmod(time * 30, im.width - 100));
    rect(im, 36 + dash, 126, 28, 1, frame_gold);
}

namespace {
void wing(Image& im, int x, int y, int height) {
    // Two forward-swept ribbons form a B in negative space. Rendered from
    // normalized geometry so the mark stays exact at every shell size.
    const int width = height * 3 / 2;
    for (int yy = 0; yy < height; ++yy) {
        for (int xx = 0; xx < width; ++xx) {
            const double u = double(xx) / width, v = double(yy) / height;
            const double band = v < 0.5 ? v * 2 : (1 - v) * 2;
            const double outer = 0.82 + 0.18 * std::sin(band * 3.141592653589793);
            const double inner = band < 0.55 ? band * 1.30 : (1 - band) * 1.58;
            if (u >= inner && u < outer) {
                im.put(x + xx, y + yy, frame_blue);
            }
        }
    }
}
} // namespace

void logo(Image& im, int x, int y, LogoSize size) {
    const int h = size == LogoSize::title ? 48 : 30;
    wing(im, x, y, h);
    const int scale = size == LogoSize::title ? 5 : 3;
    Image word(300, 70);
    fill(word, {0, 0, 0, 0});
    const int advance = text(word, "BlueStreak", 0, 8, scale, white);
    // An authored oblique wordmark, independent of OS-installed fonts.
    for (int yy = 0; yy < word.height; ++yy) {
        for (int xx = 0; xx < std::min(advance + 4, word.width); ++xx) {
            const auto p = word.pixels[std::size_t(yy) * word.width + xx];
            if (!p.a) {
                continue;
            }
            const int px = x + h * 3 / 2 + 8 + xx + (h - yy) / 5, py = y + yy - 6;
            if (px < 0 || py < 0 || px >= im.width || py >= im.height) {
                continue;
            }
            auto& d = im.pixels[std::size_t(py) * im.width + px];
            const int keep = d.a * (255 - p.a) / 255, out = p.a + keep;
            auto ch = [&](int a, int b) {
                return std::uint8_t((a * p.a + b * keep) / out);
            };
            d = {ch(p.r, d.r), ch(p.g, d.g), ch(p.b, d.b), std::uint8_t(out)};
        }
    }
    slanted(im, x + h * 3 / 2 + advance - 34, y + h - 2, 48, 3, 6, frame_blue);
}

void hint_footer(Image& im,
                 std::initializer_list<std::pair<std::string_view, std::string_view>> keys) {
    blend_rect(im, 0, 604, 960, 36, hint_bar, 200);
    int x = 24;
    for (auto& [key, action] : keys) {
        x = key_hint(im, key, action, x, 611);
    }
}

// Header strip for screens inside a generation.
void zone_header(Image& im, int generation, std::string_view title, std::string_view right) {
    const auto& t = theme(generation);
    rect(im, 0, 0, 960, 92, {12, 22, 39});
    rect(im, 24, 91, 912, 1, {44, 61, 82});
    logo(im, 24, 9, LogoSize::header);
    int x = 28;
    badge(im, t, generations[std::size_t(generation)].bits, x, 54, 2);
    x += text_width(generations[std::size_t(generation)].bits, 2) + 20;
    text(im, generations[std::size_t(generation)].systems, x, 58, 2, white, Ink::shadow, navy);
    if (!title.empty()) {
        text_right(im, title, 932, 22, 3, white, Ink::outline, navy);
    }
    if (!right.empty()) {
        text_right(im, right, 932, 58, 2, t.accent, Ink::shadow, navy);
    }
}

void button(Image& im, const Theme& t, int x, int y, int w, int h, std::string_view label,
            bool active, int scale) {
    if (active) {
        panel(im, t, x, y, w, h, t.accent, mix(t.accent, black, 90));
    } else {
        panel(im, t, x, y, w, h, t.card, mix(t.card, t.bg, 70));
    }
    const int tw = text_width(label, scale);
    text(im, label, x + (w - tw) / 2, y + (h - 7 * scale) / 2, scale, active ? t.ink : t.card_ink);
}

void menu_row(Image& im, const Theme& t, int y, std::string_view label, bool active, bool dim,
              double time, std::string_view key) {
    if (active) {
        panel(im, t, 272, y, 416, 38, t.accent, t.accent);
        rect(im, 272, y + 9, 3, 20, frame_blue);
        const int bob = 0;
        (void)time;
        text(im, std::string(1, icon_play), 284 + bob, y + 9, 3, t.ink);
    } else {
        rect(im, 272, y, 416, 38, mix(t.card, t.bg, 18));
    }
    const Color ink = active ? t.ink : dim ? t.muted : t.card_ink;
    text(im, label, 312, y + 9, 3, ink);
    if (!key.empty()) {
        text_right(im, key, 672, y + 9, 3, ink);
    }
}

} // namespace shell
