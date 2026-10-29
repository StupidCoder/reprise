#include "sonic_advance_gba/game/searchlight.hpp"
#include "sonic_advance_gba/game/game.hpp"
#include "sonic_advance_gba/game/math.hpp"
#include "sonic_advance_gba/game/window_shapes.hpp"
#include <array>

namespace sonic_advance_gba {

namespace {

// The slope (8.8) of a direction: sin / cos with the table's values shifted
// down to 10 bits.
s16 slope(const Assets& a, unsigned angle) {
    angle &= 0x3FF;
    return s16(divide((sine(a, angle) >> 6) << 8, sine(a, angle + 0x100) >> 6));
}

// The direction measured from the screen side the beam leaves through.
unsigned side_angle(unsigned angle) {
    return (angle < 0x100 ? 0x100 - angle : angle - 0x300) & 0x3FF;
}

} // namespace

void Searchlight::on_destroy() {
    video_.line_effect_on = false;
}

void Searchlight::update(Game& g) {
    const Assets& a = g.assets();
    // 08011E08
    swing = angle < 0x200 ? s16(swing - turn_speed) : s16(turn_speed + swing);
    angle = u16(((s16(swing) >> 8) + angle) & 0x3FF);
    const unsigned first = (angle + width) & 0x3FF;
    if (sine(a, first + 0x100) == 0) {
        return;
    }
    // The first side: where it meets the top of the screen or a screen side.
    s16 x0, y0;
    {
        const s16 s = slope(a, first);
        const int v = int(x) - ((int(s) * y) >> 8);
        x0 = s16(v);
        if (s16(v) < 1) {
            x0 = 0;
            const s16 t = slope(a, side_angle(first));
            y0 = s16(y - s16(u32(int(t) * int(x)) >> 8));
        } else if (s16(v) < 0xF0) {
            y0 = 0;
        } else {
            x0 = 0xF0;
            const s16 t = slope(a, side_angle(first));
            y0 = s16(y - s16(u32(int(t) * (0xF0 - int(x))) >> 8));
        }
    }
    const u8 x1 = u8(u8(x) - u8(u32((y - 0xA0) * int(slope(a, first))) >> 8));
    // The second side, mirrored.
    const unsigned second = (0x400 - (angle - width)) & 0x3FF;
    s16 x2, y2;
    {
        const s16 s = slope(a, second);
        const int v = ((int(y) * s) >> 8) + x;
        x2 = s16(v);
        if (s16(v) < 1) {
            x2 = 0;
            const s16 t = slope(a, side_angle(second));
            y2 = s16(y - s16(u32(int(t) * int(x)) >> 8));
        } else if (s16(v) < 0xF0) {
            y2 = 0;
        } else {
            x2 = 0xF0;
            const s16 t = slope(a, side_angle(second));
            y2 = s16(y - s16(u32(int(t) * (0xF0 - int(x))) >> 8));
        }
    }
    const u8 x3 = u8(u8(u32((y - 0xA0) * int(slope(a, second))) >> 8) + u8(x));
    const std::array<u8, 8> points{u8(x0), u8(y0), x1, 0xA0, u8(x2), u8(y2), x3, 0xA0};
    auto& v = g.video();
    const auto set_top = [&](u8 top) {
        auto& w = v.settings.windows[window & 1];
        w.top = top;
        w.bottom = 0xA0;
    };
    if (angle < 0x200) {
        if (y2 < 0xA0) {
            set_top(u8(y2));
            if (y0 < 0xA0) {
                window_wedge(v, window, points);
            } else {
                window_right_edge(v, window, u8(x2), u8(y2), x3, 0xA0, 0);
            }
        }
    } else if (y0 < 0xA0) {
        set_top(u8(y0));
        if (y2 < 0xA0) {
            window_wedge(v, window, points);
        } else {
            window_left_edge(v, window, u8(x0), u8(y0), x1, 0xA0, 0);
        }
    }
}

} // namespace sonic_advance_gba
