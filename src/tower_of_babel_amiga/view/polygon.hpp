#pragma once
#include "tower_of_babel_amiga/view/canvas.hpp"
#include <vector>

// The game's polygon drawing, as its rules decide which pixels a face
// covers: clipping to the view window ($147E0) and the scan-line filler
// ($13E3C), plus the rectangle fill for faces that face the camera ($11540).
namespace babel::view {

struct Point {
    int x{}, y{};
    bool operator==(const Point&) const = default;
};

// Clips the polygon to the window and fills it. Points go clockwise on the
// screen (the filler walks forwards down the right-hand side and backwards
// down the left); fewer than three points left after clipping draw nothing.
void fill_polygon(Canvas&, const Window&, std::vector<Point> points, const Pen&);

// Fills the rectangle (inclusive corners), clipped to the window.
void fill_rectangle(Canvas&, const Window&, int left, int top, int right, int bottom, const Pen&);

} // namespace babel::view
