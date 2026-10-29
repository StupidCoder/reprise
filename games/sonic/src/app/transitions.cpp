// Title cards and screen transitions.

#include "app/shell_frame.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace shell {

// --- Title card and transitions -----------------------------------------
platform::Image title_card(int w, int h, std::string_view title, std::string_view subtitle,
                           int generation, double enter, double leave) {
    Image im(w, h);
    fill(im, {0, 0, 0, 0});
    const auto& gen = generations[std::size_t(generation)];
    const double in = ease_out(enter),
                 out = reduce_motion ? (leave > 0 ? 1.0 : 0.0) : clamp01(leave) * clamp01(leave);
    const int blue_shift = int(-w * (1 - in) - w * out);
    const int gold_shift = int(w * (1 - in) + w * out);
    const int red_shift = int(w * (1 - ease_out(enter * 1.15 - 0.15)) + w * out * 1.2);
    // Gold sits behind the blue band; the bands overlap so together they cover.
    for (int y = 0; y < h; ++y) {
        const int blue_edge = w * 62 / 100 - (y * w / 10) / h;
        const int gold_edge = w * 50 / 100 - (y * w / 10) / h;
        rect(im, gold_shift + gold_edge, y, w - gold_edge + 8, 1, frame_red);
        rect(im, blue_shift - 8, y, blue_edge + 8, 1, navy);
        rect(im, blue_shift + blue_edge - 12, y, 6, 1, mix(frame_blue, white, 70));
    }
    ring(im, gold_shift + w * 80 / 100, h * 40 / 100, h * 22 / 100, std::max(8, h / 26), frame_blue,
         0, 0, w, h);
    const int red_h = h * 18 / 100;
    for (int y = h - red_h; y < h; ++y) {
        rect(im, red_shift + (h - y) / 2, y, w, 1, frame_blue);
    }
    int scale = std::max(2, h * 9 / 640);
    while (scale > 2 && text_width(title, scale) > w * 84 / 100) {
        --scale;
    }
    const int x = blue_shift + w * 7 / 100;
    const int ty = h * 34 / 100;
    text(im, title, x, ty, scale, white, Ink::outline, navy);
    text(im, subtitle, x + scale, ty + 11 * scale, std::max(2, scale / 2), frame_gold, Ink::outline,
         navy);
    std::string tag = std::string(gen.bits) + "  " + gen.systems;
    text(im, tag, red_shift + w * 7 / 100 + red_h / 2, h - red_h / 2 - std::max(2, scale / 2) * 4,
         std::max(2, scale / 2), white, Ink::shadow, navy);
    return im;
}

void wipe(platform::Image& to, const platform::Image& from, double progress, int direction) {
    if (reduce_motion) {
        return;
    }
    if (from.width != to.width || from.height != to.height) {
        return;
    }
    const double p = ease_out(progress);
    const int span = to.width + to.height / 2 + 40;
    for (int y = 0; y < to.height; ++y) {
        // Leading edge travels diagonally; a gold and a blue band ride on it.
        int edge = int(p * span) - y / 2;
        for (int x = 0; x < to.width; ++x) {
            const int d = direction >= 0 ? edge - x : edge - (to.width - 1 - x);
            auto& dst = to.pixels[std::size_t(y) * to.width + x];
            if (d < 0) {
                dst = from.pixels[std::size_t(y) * to.width + x];
            } else if (d < 10) {
                dst = frame_gold;
            } else if (d < 22) {
                dst = frame_blue;
            }
        }
    }
}

void composite(platform::Image& base, const platform::Image& layer) {
    if (base.width != layer.width || base.height != layer.height) {
        return;
    }
    for (std::size_t n = 0; n < base.pixels.size(); ++n) {
        if (layer.pixels[n].a) {
            base.pixels[n] = layer.pixels[n];
        }
    }
}

} // namespace shell
