#pragma once
#include "gamegear/video.hpp"
#include "sonic1_gg/game/game.hpp"

// Rendering of a gameplay frame.
//
// The original draws through the Game Gear VDP: a scrolling background of 8x8
// tiles (some with a priority bit that puts them in front of sprites) and up to
// 64 8x16 sprites. Here the same tile data is drawn in software into a
// gg::Screen, in an order that reproduces the VDP's layering. Hardware sprite
// limits (8 per line) are deliberately not emulated.
namespace sonic1_gg {
class WaterEffects;
class RingEffects;
class BasinWater;
class ArenaBarrier;
class ObjectFades;
// A complete frame: 160x144 (the LCD) or, `expanded`, the widescreen view sized
// for the canvas. The optional pointers enable the visual enhancements.
gg::Screen render_game_view(const Game&, bool expanded, bool shield_transparency = false,
                            bool enhanced_water = false, const WaterEffects* effects = nullptr,
                            int canvas_width = 0, int canvas_height = 0,
                            const RingEffects* rings = nullptr, const BasinWater* basins = nullptr,
                            const ArenaBarrier* barrier = nullptr,
                            const ObjectFades* fades = nullptr);
void configure_water_palette(gg::Screen&, const Game&, int camera_y);
void draw_level(gg::Screen& image, const sonic1_gg::Level& level, int camera_x, int camera_y,
                const BasinWater* basins = nullptr);
void draw_foreground(gg::Screen& image, const sonic1_gg::Level& level, int camera_x, int camera_y,
                     const BasinWater* basins = nullptr);
void draw_player(gg::Screen& image, const sonic1_gg::Level& level, int frame, bool left, int x,
                 int y);
void draw_shield(gg::Screen&, const Game&, int x, int y, bool translucent);
void draw_player_effects(gg::Screen&, const Game&, int camera_x, int camera_y);
void draw_objects(gg::Screen& image, const sonic1_gg::Game& game, int camera_x, int camera_y,
                  bool enhanced_water = false, bool enhanced_rings = false,
                  const ObjectFades* fades = nullptr);
void draw_flowers(gg::Screen& image, const sonic1_gg::Game& game, int camera_x, int camera_y);
void draw_hud(gg::Screen& image, const sonic1_gg::Game& game);
} // namespace sonic1_gg
