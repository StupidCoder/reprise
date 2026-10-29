#pragma once
#include "gg_engine/enhancements/widescreen.hpp"
#include <optional>

// Widescreen enhancement. None of this is cartridge behaviour: it adapts the
// reconstructed rules to a presented view larger than the 160x144 LCD. With
// the original view size every function here reproduces the LCD crop.
//
// The game side is in Game (update_wide_view, wide_screen_limits,
// camera_area, the wide_* margins used by off-screen tests and the full
// sprite positions it keeps for the renderer); the view size and its aspect
// limits are shared with Sonic 1 and 2 (gamegear/wide_view.hpp), the view
// geometry and the arena barrier with Sonic 2
// (gg_engine/enhancements/widescreen.hpp).
namespace triple_trouble_gg {

class Game;

using gg_engine::ArenaBarrier;
using gg_engine::lcd_left;
using gg_engine::lcd_top;
using gg_engine::lift;
using gg_engine::ViewRect;

// The view for the camera the picture's scroll was set for, clamped to the
// act's camera area (gg_engine::wide_view_rect).
ViewRect wide_view_rect(const Game&, int camera_x, int camera_y, int width, int height);

// The arena barrier's view of the frame (gg_engine::ArenaBarrier), or none
// outside widescreen play or without the enhancement.
std::optional<gg_engine::ArenaView> arena_view(const Game&, bool enabled);

} // namespace triple_trouble_gg
