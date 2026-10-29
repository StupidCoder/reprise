#pragma once
// The third adventure's special puzzle engines: the common puzzle screen
// with its own script commands (1280..1293) and a step every frame. Each is
// a separate overlay (36..42) whose scene class derives from the common one
// (lt3CLoopNazoCommon); the puzzle's script drives it through the commands.
//
//  - CUT (type 9, overlay 36): lines drawn along the edges of a grid of
//    points, judged against the expected edges;
//  - LINE (type 10, overlay 37): straight lines between anchor points;
//  - LTGRAM (types 12, 18, 22, overlay 39): pieces moved, turned and
//    mirrored on a grid until they cover the target regions;
//  - SLIDE (type 11, overlay 38): blocks slid on a board of cells until the
//    named ones cover their goal cells;
//  - BANANA (type 14, overlay 40): a walker led through a grid with ice by
//    arrow buttons;
//  - SCALE (type 19, overlay 41): a balance weighing what is put on its
//    dishes;
//  - GRID (type 21, overlay 42): pieces moved and turned on a grid, kept on
//    the board.
#include "graphics/a3_assets.hpp"
#include "system/a3_lscr_scene.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace picarat::adventure3 {

// The engine of a puzzle type, or null for the common engine (and for the
// special ones not reconstructed yet).
std::unique_ptr<lscr::SceneEngine> makeEngine(Assets& assets, int type);

// The script sections an engine has its own objects run (for the static
// support check).
std::vector<std::string> engineSections(Assets& assets, int type);

// The engines' drawing, shared with the minigames: a line `width` wide
// between fixed-point points, ends squared off (020553d4); a filled box
// between fixed-point corners.
void drawThickLine(Surface& s, std::array<int, 2> a, std::array<int, 2> b, int width,
                   std::uint16_t colour);
void drawBox(Surface& s, int left, int top, int right, int bottom, std::uint16_t colour);

} // namespace picarat::adventure3
