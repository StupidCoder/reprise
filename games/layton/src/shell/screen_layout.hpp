#pragma once
// Where the two game screens go in the game area of the window: the DS's
// two 256x192 screens, or the 3DS's 400x240 upper and 320x240 lower ones.
#include <algorithm>

namespace picarat::shell {
struct ScreenRect {
    double x, y, width, height;
};

// The screens' sizes in game pixels.
struct ScreenGeometry {
    int topWidth = 256, topHeight = 192, bottomWidth = 256, bottomHeight = 192;

    static constexpr ScreenGeometry ds() { return {}; }
    static constexpr ScreenGeometry ctr() { return {400, 240, 320, 240}; }
    int frameWidth() const { return std::max(topWidth, bottomWidth); }
    int frameHeight() const { return topHeight + bottomHeight; }
    bool operator==(const ScreenGeometry&) const = default;
};

struct ScreenLayout {
    ScreenRect top, bottom;
    bool horizontal;
    double scale;
};

// Choose the arrangement with the larger usable screen scale, including
// fractional enlargement. Preserve the screens' shapes and center the pair
// (stacked: each screen centred on the other).
inline ScreenLayout screenLayout(double width, double height, const ScreenGeometry& g = {}) {
    const double tw = g.topWidth, th = g.topHeight, bw = g.bottomWidth, bh = g.bottomHeight;
    double vertical = std::min(width / std::max(tw, bw), height / (th + bh));
    double horizontal = std::min(width / (tw + bw), height / std::max(th, bh));
    bool sideBySide = horizontal > vertical;
    double scale = std::max(0.001, sideBySide ? horizontal : vertical);
    if (sideBySide) {
        const double x = (width - (tw + bw) * scale) / 2;
        const double y = (height - std::max(th, bh) * scale) / 2;
        return {{x, y + (std::max(th, bh) - th) * scale / 2, tw * scale, th * scale},
                {x + tw * scale, y + (std::max(th, bh) - bh) * scale / 2, bw * scale, bh * scale},
                true,
                scale};
    }
    const double y = (height - (th + bh) * scale) / 2;
    return {{(width - tw * scale) / 2, y, tw * scale, th * scale},
            {(width - bw * scale) / 2, y + th * scale, bw * scale, bh * scale},
            false,
            scale};
}
} // namespace picarat::shell
