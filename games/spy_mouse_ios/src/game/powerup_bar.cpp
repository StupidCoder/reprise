#include "game/powerup_bar.hpp"

namespace doc::game {

int PowerupBar::rowAt(Vec2 screen, float barHeight) const {
    if (!slide_.finished() || slide_.progress() < 1.0f) return -1;
    const float top = kListTop + offsetY(barHeight);
    if (screen.x < kListLeft || screen.x > kListLeft + kListWidth || screen.y < top || screen.y > top + kListHeight)
        return -1;
    const int row = int((screen.y - top + scroll_) / kRowHeight);
    return row >= 0 && row < kPowerupCount ? row : -1;
}

}  // namespace doc::game
