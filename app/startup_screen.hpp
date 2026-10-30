#pragma once
#include "ui/shelf.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace app {
// Lightweight, artwork-independent startup UI. Draw only on the UI thread.
inline platform::Image startup_screen(int width, int height, std::string_view phase, double seconds) {
    namespace s = ui::shelf;
    platform::Image image(width, height);
    ui::fill(image, s::paper);
    const int w = std::min(400, width - 64), x = (width - w) / 2, y = height / 2 - 72;
    s::reprise(image, x, y);
    s::text(image, "Reprise", x + 56, y + 4, 30, s::ink, true);
    s::text(image, "Welcome back", x, y + 64, 26, s::ink, true);
    s::text(image, phase, x, y + 106, 17, s::muted);
    ui::rounded(image, x, y + 148, w, 6, 3, s::line);
    const int at = int((w - 64) * (.5 + .5 * std::sin(seconds * 2.5)));
    ui::rounded(image, x + at, y + 148, 64, 6, 3, s::cobalt);
    return image;
}
} // namespace app
