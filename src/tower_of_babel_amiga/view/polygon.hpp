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

// How a polygon's spans are written: the plain filler ($13E3C); the
// outlined one ($1414C), which leaves both ends of every span in colour 0;
// the see-through one ($1447C), which skips the pen's clear pixels.
enum class Fill { plain, outlined, see_through };

// A point in view space before projection (x, y, z relative to the camera).
struct Point3 {
    int x{}, y{}, z{};
};

// Clips a polygon in view space to one side of a cell boundary (the
// object clips $14A20-$14B30): keeps x >= plane (keep = 0), x <= plane (1),
// z >= plane (2) or z <= plane (3).
void clip_to_plane(std::vector<Point3>& points, int keep, int plane);

// Clips the polygon to the window and fills it. Points go clockwise on the
// screen (the filler walks forwards down the right-hand side and backwards
// down the left); fewer than three points left after clipping draw nothing.
void fill_polygon(Canvas&, const Window&, std::vector<Point> points, const Pen&, Fill fill = Fill::plain);

// Fills the rectangle (inclusive corners), clipped to the window.
void fill_rectangle(Canvas&, const Window&, int left, int top, int right, int bottom, const Pen&);

} // namespace babel::view
