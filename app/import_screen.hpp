#pragma once
#include "ui/shelf.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace app {
// Only the UI thread draws. Import workers publish plain progress values.
inline platform::Image import_screen(int width, int height, const std::string& phase,
                                     double fraction, double seconds, std::size_t index,
                                     std::size_t count, bool quitting = false) {
    namespace s = ui::shelf;
    platform::Image image(width, height);
    ui::fill(image, s::paper);
    const int w = std::min(500, width - 48), x = (width - w) / 2, y = (height - 240) / 2;
    ui::rounded(image, x, y + 4, w, 240, 12, s::line);
    ui::rounded(image, x, y, w, 240, 12, s::card);
    s::text(image, "Adding to your library", x + 24, y + 24, 25, s::ink, true);
    const std::string detail = count > 1 ? "Image " + std::to_string(index + 1) + " of " + std::to_string(count)
                                          : "Preparing your game";
    s::text(image, detail, x + 24, y + 64, 16, s::muted);
    s::clipped(image, phase, x + 24, y + 106, w - 48, 17);
    const int bar = w - 48;
    ui::rounded(image, x + 24, y + 143, bar, 10, 5, s::pale);
    if (fraction >= 0 && std::isfinite(fraction)) {
        const double value = std::clamp(fraction, 0.0, 1.0);
        const int filled = int(bar * value);
        if (filled > 0) ui::rounded(image, x + 24, y + 143, filled, 10, std::min(5, filled / 2), s::cobalt);
        s::right(image, std::to_string(int(value * 100)) + "%", x + w - 24, y + 166, 16, s::ink);
    } else {
        const int at = int((bar - 72) * (.5 + .5 * std::sin(seconds * 3)));
        ui::rounded(image, x + 24 + at, y + 143, 72, 10, 5, s::cobalt);
    }
    s::wrap(image, quitting ? "Finishing safely before closing..." : "Large games can take a little longer.",
            x + 24, y + 199, w - 48, 2, 14, s::muted);
    return image;
}
} // namespace app
