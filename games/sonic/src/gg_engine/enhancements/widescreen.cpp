// The widescreen view and the arena barrier (widescreen.hpp).

#include "gg_engine/enhancements/widescreen.hpp"
#include <algorithm>
#include <cstdint>

namespace gg_engine {

ViewRect wide_view_rect(const CameraArea& area, int camera_x, int camera_y, int width,
                        int height) {
    if (width <= 160 && height <= 144) {
        return {lcd_left, lcd_top, 160, 144};
    }
    width = std::max(width, 160);
    height = std::max(height, 144);
    const int lcd_x = camera_x + lcd_left + 1, lcd_y = camera_y + lcd_top + 17;
    auto fit = [](int lcd, int lcd_size, int size, int low, int high) {
        low = std::min(low, lcd);
        high = std::max(high, lcd + lcd_size);
        if (high - low <= size) {
            return low - (size - (high - low)) / 2;
        }
        return std::clamp(lcd - (size - lcd_size) / 2, low, high - size);
    };
    const int x = fit(lcd_x, 160, width, area.left + lcd_left + 1, area.right + lcd_left + 1 + 160);
    const int y =
        fit(lcd_y, 144, height, area.top + lcd_top + 17, area.bottom + lcd_top + 17 + 144);
    return {lcd_left + (x - lcd_x), lcd_top + (y - lcd_y), width, height};
}

int lift(int value, int period, int near) {
    int d = (value - near) % period;
    if (d < 0) {
        d += period;
    }
    if (d >= period / 2) {
        d -= period;
    }
    return near + d;
}

void ArenaBarrier::reset() {
    field_.clear();
    act_ = -1;
}

void ArenaBarrier::tick(const std::optional<ArenaView>& view) {
    field_.advance();
    if (!view) {
        field_.clear();
        return;
    }
    const ArenaView& v = *view;
    if (v.act != act_) {
        field_.clear();
        act_ = v.act;
    }
    // Within the view only: at the act's ends the view is clamped and the
    // wall is its edge, which draws nothing.
    const int screen = int(std::int16_t(std::uint16_t(v.player_x - v.camera_x)));
    for (bool right : {false, true}) {
        // The player's position is kept within 38-C7, the sprite within the
        // LCD (picture columns 48-207).
        const int x = v.camera_x + lcd_left + (right ? 160 : 0);
        const bool lock = !v.finished && (right ? v.right_wall && v.wide_right > 8
                                                : v.left_wall && v.wide_left > 8);
        auto* wall = field_.side(right, lock, x);
        if (!wall) {
            continue;
        }
        const bool pushing =
            right ? screen >= 0xC7 && v.pressing_right : screen <= 0x38 && v.pressing_left;
        if (pushing && v.alive) {
            gg::ForceField::touch(*wall, v.player_y - 12);
        }
        for (const auto& b : v.bosses) {
            if (b.x - b.half_width < x && b.x + b.half_width > x) {
                gg::ForceField::touch(*wall, b.y - b.half_height);
            }
        }
    }
}

void ArenaBarrier::draw(gg::Screen& image, int view_x, int view_y) const {
    field_.draw(image, view_x, view_y);
}

} // namespace gg_engine
