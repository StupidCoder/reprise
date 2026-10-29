#pragma once
#include <array>

// Widescreen view size shared by the Game Gear games. None of this is
// cartridge behaviour: a game that offers the widescreen enhancement presents
// a view larger than the 160x144 LCD, sized here for the host's canvas.
namespace gg {

// The aspect ratio is limited to min/max_view_aspect: from the LCD's own 10:9
// (160x144) up to 21:9. Wider views would wake whole stretches of an act at
// once, and taller ones reveal rooms above and below that the LCD never
// framed; beyond the limits the host letterboxes or pillarboxes.
inline constexpr double min_view_aspect = 10.0 / 9.0, max_view_aspect = 21.0 / 9.0;

// Scene size for a drawable canvas: at least 320x224, widened or heightened
// to follow the canvas aspect, independent of DPI. 0x0 keeps 320x224.
std::array<int, 2> expanded_view_size(int canvas_width, int canvas_height);

} // namespace gg
