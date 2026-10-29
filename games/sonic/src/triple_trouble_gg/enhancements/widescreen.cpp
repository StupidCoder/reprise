// Widescreen enhancement, game side: how the reconstructed rules adapt to a
// presented view larger than the 160x144 LCD. None of this is cartridge
// behaviour, and every rule is gated on such a view (the wide_* margins are
// zero on the LCD), so the original format is unaffected.
//
// The view follows Sonic 1's and Sonic 2's rules: it is centred on the LCD
// crop and clamped to what the LCD could show anywhere in the act. The
// rules bent for it are marked "Widescreen" where they are applied; the
// main ones:
//   - 5456: objects count as on screen inside the presented view, so they
//     are drawn there and run the parts of their routines that the
//     original only runs on screen; they are removed only well beyond it.
//   - bank 1C, 8000: placed objects also appear in a ring just outside the
//     view (the original's ring lies 64 pixels outside the picture, which a
//     wide view can show).
//   - B2A7, B42C: the player is kept inside the view rather than the LCD,
//     but only on sides where the camera can still scroll; elsewhere the
//     LCD's edge stays a wall and ArenaBarrier shows it.
//     51FF: a solid object pushes him out only within the same limits.
//   - 84FF: after the sign the player runs until he has left the view.
//   - Entry and removal points relative to the camera (the falling sign,
//     the bouncers entering at the screen's edges, the train leaving, the
//     last machine drifting off, Robotnik Winter's boss dropped in from
//     beyond the screen, crumbling blocks left of it, the jet board's top
//     and bottom) move beyond the view; the water's glints (A357) spread
//     across it.
// Not widened (the original's): falling 208 lines below the camera kills
// (B03B, A1B9; the view is clamped to the act, so this is out of sight
// except where the camera itself is still falling behind), and Tails'
// flight tests against the screen's line A0.
// No widescreen (the LCD view): special stages, flights, zone 9, and
// Sunset Park's train ride (see wide_play).
// The renderer (render/wide_view.cpp) draws the map, the sprites at their
// full positions and the HUD at the view's corner.

#include "triple_trouble_gg/enhancements/widescreen.hpp"
#include "triple_trouble_gg/game/game.hpp"
#include <algorithm>

namespace triple_trouble_gg {

ViewRect wide_view_rect(const Game& g, int camera_x, int camera_y, int width, int height) {
    return gg_engine::wide_view_rect(g.camera_area(), camera_x, camera_y, width, height);
}

bool Game::wide_play() const {
    // An act of zones 0-5 (mode 5, not a demo) whose background is drawn
    // from the act's map: not the special stages, flights, title cards,
    // tallies or the ending.
    // Nor Sunset Park's train ride (its director, type 13, in the
    // partner's slot): the scenery moves by the scroll and by rewriting
    // the chip's 256-pixel map, with the camera still, so there is no
    // wider world to show.
    return flow.mode == 0x05 && flow.zone < 6 && !(flow.demo & 0x80) && display.level_shown &&
           objects[1].type != 0x13;
}

// Run once the frame interrupt has set the scroll, before the main program.
// The margins are fitted to the picture being shown (the camera the scroll
// was set for, any shake included), which lags the camera's latest move.
void Game::update_wide_view() {
    wide_left = wide_right = wide_top = wide_bottom = 0;
    if ((view_width <= 160 && view_height <= 144) || !wide_play()) {
        return;
    }
    const auto v =
        wide_view_rect(*this, picture_x() - 1, picture_y() - 17, view_width, view_height);
    wide_left = lcd_left - v.x;
    wide_right = v.x + v.width - (lcd_left + 160);
    wide_top = lcd_top - v.y;
    wide_bottom = v.y + v.height - (lcd_top + 144);
}

Game::ScreenLimits Game::wide_screen_limits() const {
    // The camera stops (4CCE) or follows a script (4C54) in boss arenas and
    // at the act's end; otherwise it is held between min_x and max_x.
    const bool held =
        (camera.mode & camera_mode::scripted) || !(camera.map_flags & map_flag::enabled);
    const bool left_free = !held && camera.x > camera.min_x;
    const bool right_free = !held && camera.x < camera.max_x;
    return {0x38 - (left_free ? wide_left : 0), 0xC7 + (right_free ? wide_right : 0)};
}

bool Game::in_wide_view(unsigned x, unsigned y, int slack) const {
    if (!widescreen()) {
        return false;
    }
    const int dx = int(std::int16_t(std::uint16_t(x - camera.x)));
    const int dy = int(std::int16_t(std::uint16_t(y - camera.y)));
    return dx >= -wide_left - slack && dx < 256 + wide_right + slack &&
           dy >= 16 - wide_top - slack && dy < 240 + wide_bottom + slack;
}

std::optional<gg_engine::ArenaView> arena_view(const Game& g, bool enabled) {
    if (!enabled || !g.widescreen() || !g.wide_play()) {
        return std::nullopt;
    }
    // Where the camera cannot scroll on, B2A7 keeps the player inside the
    // LCD (Game::wide_screen_limits). After the sign the player runs off
    // (84FF, states 20 and 49) through the wall. Before the player starts
    // (state 0, while the act fades in) the camera does not follow yet: no
    // arena either.
    const Object& p = g.objects[0];
    const auto limits = g.wide_screen_limits();
    gg_engine::ArenaView v{
        .act = g.flow.zone * 3 + g.flow.act,
        .camera_x = int(g.camera.x),
        .left_wall = limits.left == 0x38,
        .right_wall = limits.right == 0xC7,
        .wide_left = g.wide_left,
        .wide_right = g.wide_right,
        .finished = !p.state || p.state == 0x20 || p.state == 0x49 || (g.flow.frame_work & 0x10),
        .alive = !(g.flow.frame_work & 0x04),
        .player_x = int(p.x),
        .player_y = int(p.y),
        .pressing_left = (g.input.held & button::left) != 0,
        .pressing_right = (g.input.held & button::right) != 0,
        .bosses = {},
    };
    for (unsigned i = 1; i < g.objects.size(); ++i) {
        const Object& o = g.objects[i];
        switch (o.type) {
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x66:
            v.bosses.push_back({int(o.x), int(o.y), o.frame_width, o.frame_height});
            break;
        default:
            break;
        }
    }
    return v;
}

} // namespace triple_trouble_gg
