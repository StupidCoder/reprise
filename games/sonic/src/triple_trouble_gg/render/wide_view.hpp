#pragma once
#include "gamegear/video.hpp"
#include "gg_engine/enhancements/widescreen.hpp"

// Widescreen enhancement, presentation: the picture extended beyond the LCD
// crop. Within the crop it is the same picture the video chip draws, apart
// from the HUD, which moves to the view's corner.
namespace triple_trouble_gg {

class Game;
using gg_engine::ArenaBarrier;

// Sets the view the game adapts its rules to (160x144 = the LCD, inert).
void set_game_view(Game&, int width, int height);

// True when render_frame draws the expanded view for the game's state.
bool wide_view_active(const Game&);

// The current frame at the given view size: during play the act's map,
// the sprites, the arena force field (if given) and the HUD drawn for the
// view; otherwise (or at 160x144) the LCD picture.
gg::Screen render_frame(const Game&, int width, int height, const ArenaBarrier* barrier = nullptr);

} // namespace triple_trouble_gg
