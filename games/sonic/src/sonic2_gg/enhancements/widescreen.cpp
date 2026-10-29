// Widescreen enhancement, game side: how the reconstructed rules adapt to a
// presented view larger than the 160x144 LCD. None of this is cartridge
// behaviour, and every rule is gated on such a view (the wide_* margins are
// zero on the LCD), so the original format is unaffected.
//
// The view follows Sonic 1's rules (sonic1_gg/enhancements/widescreen.cpp):
// it is centred on the LCD crop and clamped to what the LCD could show
// anywhere in the act. The rules bent for it are marked "Widescreen" where
// they are applied; the main ones:
//   - 603C: objects count as on screen inside the presented view, so they
//     are drawn there and run the parts of their handlers that the original
//     only runs on screen (most objects idle while off screen).
//   - bank 1E, 8000: placed objects also appear once they come within the
//     view (the original's reach covers it at most aspect ratios already).
//   - 3BF5: Sonic is kept inside the view rather than the LCD, but only on
//     sides where the camera can still scroll; elsewhere the LCD's edge
//     stays a wall and ArenaBarrier shows it.
//   - 34F8: after the end sign Sonic runs until he has left the view.
//   - Removal and entry points relative to the camera (fireballs, boulders,
//     bubbles, falling blocks, the Under Ground boss, the capsule) move
//     beyond the view.
// The renderer (render/wide_view.cpp) draws the map, the sprites at their
// full positions and the HUD at the view's corners.

#include "sonic2_gg/enhancements/widescreen.hpp"
#include "sonic2_gg/game/game.hpp"
#include "sonic2_gg/render/wide_view.hpp"
#include <algorithm>

namespace sonic2_gg {

Game::Area Game::camera_area() const {
    // 53C4: the map header's camera limits (bytes 12-19, table 546A). Later
    // limits (arenas, scripts) only narrow them, except where a boss widens
    // the right limit beyond the header; the union covers both.
    const auto h = act_entry(rom::map_headers);
    Area a{int(rom.word(h, 12)), int(rom.word(h, 14)), int(rom.word(h, 16)), int(rom.word(h, 18))};
    a.left = std::min(a.left, int(camera.min_x));
    a.top = std::min(a.top, int(camera.min_y));
    a.right = std::max(a.right, int(camera.max_x));
    a.bottom = std::max(a.bottom, int(camera.max_y));
    return a;
}

ViewRect wide_view_rect(const Game& g, int camera_x, int camera_y, int width, int height) {
    return gg_engine::wide_view_rect(g.camera_area(), camera_x, camera_y, width, height);
}

// Run once the frame interrupt has moved the camera, before the main program.
// The margins are fitted to the picture being shown, which lags the camera:
// the interrupt sets the scroll registers from D172/D173 (computed by 4AE1
// from the camera before it moves) and only then moves the camera to its
// target (0512); while the water line is in use some interrupts upload
// colours only and leave the registers older still. Fitted to the moved
// camera, a view held at the act's edge would shift by the difference
// every frame. The registers include any screen shake (04E4), which then
// moves the whole view as it moves the LCD's background.
void Game::update_wide_view() {
    wide_left = wide_right = wide_top = wide_bottom = 0;
    if ((view_width <= 160 && view_height <= 144) || flow.zone >= 7) {
        return;
    }
    const int shown_x = lift(-int(display.scroll_x) - 1, 256, int(camera.x));
    const int shown_y = lift(int(display.scroll_y) - 17, 224, int(camera.y));
    const auto v = wide_view_rect(*this, shown_x, shown_y, view_width, view_height);
    wide_left = lcd_left - v.x;
    wide_right = v.x + v.width - (lcd_left + 160);
    wide_top = lcd_top - v.y;
    wide_bottom = v.y + v.height - (lcd_top + 144);
}

Game::ScreenLimits Game::wide_screen_limits() const {
    // 4A5F cancels targets outside camera_min_x .. camera_max_x - 1.
    const bool held = camera.mode & camera_mode::scripted; // 4A21: a scripted target
    const bool left_free = !held && camera.x > camera.min_x;
    const bool right_free = !held && camera.x + 1u < camera.max_x;
    return {0x38 - (left_free ? wide_left : 0), 0xC7 + (right_free ? wide_right : 0)};
}

bool Game::in_wide_view(unsigned x, unsigned y, int slack) const {
    if (!widescreen()) {
        return false;
    }
    const int dx = int(std::int16_t(std::uint16_t(x - camera.x)));
    const int dy = int(std::int16_t(std::uint16_t(y - camera.y)));
    return dx >= -wide_left - slack && dx < 256 + wide_right + slack && dy >= -wide_top - slack &&
           dy < 256 + wide_bottom + slack;
}

std::optional<gg_engine::ArenaView> arena_view(const Game& g, bool enabled) {
    if (!enabled || !g.widescreen() || !wide_play(g)) {
        return std::nullopt;
    }
    // Where the camera cannot scroll on, 3BF5 keeps Sonic inside the LCD
    // (Game::wide_screen_limits). After the end sign Sonic runs off (34F8,
    // state 20) through the wall.
    const Object& sonic = g.objects[0];
    const auto limits = g.wide_screen_limits();
    gg_engine::ArenaView v{
        .act = g.flow.zone * 3 + g.flow.act,
        .camera_x = int(g.camera.x),
        .left_wall = limits.left == 0x38,
        .right_wall = limits.right == 0xC7,
        .wide_left = g.wide_left,
        .wide_right = g.wide_right,
        .finished = sonic.state == 0x20 ||
                    (g.flow.game_flags & (game_flag::act_complete | game_flag::zone_complete)),
        .alive = !(g.flow.game_flags & game_flag::life_lost),
        .player_x = int(sonic.x),
        .player_y = int(sonic.y),
        .pressing_left = (g.input.held[0] & button::left) != 0,
        .pressing_right = (g.input.held[0] & button::right) != 0,
        .bosses = {},
    };
    for (int i = 1; i < 20; ++i) {
        const Object& o = g.objects[i];
        switch (o.type) {
        case 0x42:
        case 0x44:
        case 0x48:
        case 0x4A:
        case 0x4B:
        case 0x4E:
        case 0x55:
            v.bosses.push_back({int(o.x), int(o.y), o.width, o.height});
            break;
        default:
            break;
        }
    }
    return v;
}

} // namespace sonic2_gg
