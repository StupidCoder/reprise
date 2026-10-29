#pragma once
#include "gamegear/video.hpp"
#include "gg_engine/enhancements/widescreen.hpp"

// Widescreen enhancement, presentation: the VDP picture extended beyond the
// LCD crop. Within the crop it is the same picture the VDP draws.
namespace sonic2_gg {

class Game;
using gg_engine::ArenaBarrier;
class WaterEffects;

// Sets the view the game adapts its rules to (160x144 = the LCD, inert).
void set_game_view(Game&, int width, int height);

// The current frame at the given view size: during play, the map, sprites,
// the arena force field (if given) and the HUD drawn for the view;
// otherwise (or at 160x144) the LCD picture. With `water` (the water
// enhancement), its acts are drawn from the map at any size, the water
// enhanced (enhancements/water.hpp).
gg::Screen render_frame(const Game&, int width, int height, const ArenaBarrier* barrier = nullptr,
                        const WaterEffects* water = nullptr);

// The name table entry the map gives a world (map) position, as 56E0/57A5
// fill the name table.
unsigned map_entry(const Game&, int x, int y);

// True while an act is played (also while Sonic dies or the act ends):
// the states in which a widescreen view applies. Attract demos replay
// original controller bytes, so they keep the LCD rules.
bool wide_play(const Game&);

// True when render_frame draws the expanded view for the game's state.
bool wide_view_active(const Game&);

} // namespace sonic2_gg
