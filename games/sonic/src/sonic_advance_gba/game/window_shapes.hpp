#pragma once
#include "sonic_advance_gba/game/types.hpp"
#include <array>

// Window shapes drawn line by line: the edges of a window are written into
// the per-line table (video.hpp) with Bresenham's line algorithm, so that a
// window can be a slanted wedge (the intro's searchlights).
//
// `which` selects the table layout and window as the original's routines
// do: 0/1 window 0/1 alone (two bytes per line), 2/3 window 0/1 in a table
// holding both windows (four bytes per line).
namespace sonic_advance_gba {

struct VideoState;

// 08005170: a right edge from (x0, y0) to (x1, y1), the left edge at `left`.
void window_right_edge(VideoState& v, u8 which, u8 x0, u8 y0, u8 x1, u8 y1, u8 left);
// 080053F0: a left edge from (x0, y0) to (x1, y1), the right edge at `right`.
void window_left_edge(VideoState& v, u8 which, u8 x0, u8 y0, u8 x1, u8 y1, u8 right);
// 08006194: a left edge (points 0, 1) and a right edge (points 2, 3), each
// from its first point down to its second; above the lower start only one
// edge exists (the other at the screen side).
void window_wedge(VideoState& v, u8 which, const std::array<u8, 8>& points);
// 08005CFC: a band between a left edge (points 0, 1 to 2, 3) and a right
// edge (points 4, 5 to 6, 7), each from its first point down to its
// second; where only one edge exists the other is at the screen's left or
// at `limit` (the right side). Edges stay within 0..limit.
void window_band(VideoState& v, u8 which, const std::array<u8, 8>& points, s32 limit);

} // namespace sonic_advance_gba
