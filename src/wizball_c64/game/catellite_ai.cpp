// The automatic catellite (an option, not the original game): a computer
// team mate holding the second stick, as a second player does in the team
// modes ($697D, control 2). It goes for the falling droplets worth
// catching ($88B5: paint, whatever its colour, and white for a life),
// keeps out of the aliens' way unless it cannot be hurt, and shoots an
// alien level with it; with nothing to do it lets go of the stick, so the
// catellite falls back to trailing the wiz. It only reads the game's state.

#include "wizball_c64/game/play.hpp"
#include <cstdlib>

namespace wizball::game {
namespace {
constexpr std::uint8_t up = 1, down = 2, left = 4, right = 8, fire = 16;
// Droplet kinds worth catching ($88B5): paint 2/5/6, white (a life) 1.
bool wanted(std::uint8_t kind) { return kind == 1 || kind == 2 || kind == 5 || kind == 6; }
} // namespace

Stick Play::catellite_ai_stick() const {
    std::uint8_t pressed = 0;
    // Its catching point, as $88B5 tests it (x in the objects' units).
    const int cx = catellite_.x(), cy = catellite_.y() + 0x0B;
    // The nearest droplet worth having, still falling.
    int best = -1, best_distance = 1 << 30;
    for (int s = 0; s < int(objects_.size()); ++s) {
        const auto& o = objects_[std::size_t(s)];
        if (!o.active() || o.state != 2 || !wanted(o.kind) || o.y >= 0xC4) continue;
        const int dx = (o.box_left + o.box_right) / 2 - cx, dy = (o.box_top + o.box_bottom) / 2 - cy;
        const int distance = 4 * dx * dx + dy * dy; // (an x unit is two pixels)
        if (distance < best_distance) {
            best_distance = distance;
            best = s;
        }
    }
    if (best >= 0) {
        const auto& o = objects_[std::size_t(best)];
        // A little below its middle: it falls a line a pass.
        const int dx = (o.box_left + o.box_right) / 2 - cx, dy = (o.box_top + o.box_bottom) / 2 + 3 - cy;
        if (dx > 1) pressed |= right;
        if (dx < -1) pressed |= left;
        if (dy > 2) pressed |= down;
        if (dy < -2) pressed |= up;
    }
    // Aliens: out of their way (when they can hurt it), and a shot at one
    // level with it when there is nothing to fetch.
    const bool safe = catellite_protected_ || catellite_.shield() != 0;
    for (const auto& o : objects_) {
        if (!o.active() || !(o.state < 2 || o.state == 0xFF)) continue;
        const int dx = (o.box_left + o.box_right) / 2 - cx, dy = (o.box_top + o.box_bottom) / 2 - cy;
        if (!safe && std::abs(dx) < 12 && std::abs(dy) < 20) {
            pressed &= std::uint8_t(~(left | right | up | down));
            pressed |= dx > 0 ? left : right;
            pressed |= dy > 0 ? up : down;
            break;
        }
        if (best < 0 && std::abs(dy) < 6 && std::abs(dx) < 70) {
            pressed |= fire | (dx > 0 ? right : left);
            break;
        }
    }
    return Stick{std::uint8_t(0xFF & ~pressed)};
}

} // namespace wizball::game
