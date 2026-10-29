#include "spindizzy_worlds_amiga/view/camera.hpp"
#include <algorithm>

namespace spindizzy::view {

namespace {

constexpr int target_x = 0x88;  // the explorer's corner this far right of $2570
constexpr int target_y = 0x50;  // and this far below $2572 while following
constexpr int snap_y = 0x58;    // (a level start puts it 8 lower)
constexpr int last_x = 0x2E0;   // $2570 range
constexpr int last_y = 0x1A7;   // $2572 range
constexpr std::uint16_t fixed_across = 0x8000;

// An eighth of the distance (asr #3); a negative eighth is one smaller,
// so that the last seven pixels upward or leftward are never closed.
int eighth(int distance, int limit) {
    int step = std::int16_t(distance) >> 3;
    if (step < 0) {
        ++step;
        if (step >= 0) return 0;
        return std::max(step, -limit);
    }
    return std::min(step, limit);
}

} // namespace

Camera snap_camera(const SpritePlace& explorer, std::uint16_t level_flags) {
    Camera camera;
    int column = std::int16_t(explorer.x - target_x) >> 4;
    column = std::clamp(column, 0, last_x >> 4);
    if (level_flags & fixed_across) column = 0x17;
    camera.scroll_x = column << 4;
    const int y = explorer.y - snap_y;
    camera.scroll_y = y < 0 ? 0 : (y >= last_y + 1 ? last_y : y);
    return camera;
}

void follow(Camera& camera, const SpritePlace& explorer, int z, std::uint16_t level_flags) {
    if (!(level_flags & fixed_across)) {
        const int distance = explorer.x - target_x - camera.scroll_x;
        if (distance != 0) {
            if (const int step = eighth(distance, 8); step != 0 || distance > 0)
                camera.scroll_x = std::clamp(camera.scroll_x + step, 0, last_x);
        }
    }
    const int followed_y = explorer.y + (std::int16_t(z) >> 7);
    const int distance = followed_y - target_y - camera.scroll_y;
    if (distance == 0) return;
    const int step = eighth(distance, 4);
    if (step == 0 && distance < 0) return;
    camera.scroll_y = std::clamp(camera.scroll_y + step, 0, last_y);
}

} // namespace spindizzy::view
