#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/landscape/camera.hpp"
#include "mindwalker_amiga/landscape/view_cells.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"

// Drawing the landscape's view (A $201492 / $201758): the floor grid, then the
// 4 x 4 cells from the back row to the front, each cell's body (pillar,
// pedestal or box), top face, side faces and markers, in the original's pens.
//
// Close to the original, not pixel-exact: polygons are filled by our own
// scanline fill rather than graphics.library's AreaEnd (owner's decision,
// 2026-10-27: the landscape need not be pixel perfect). Not drawn yet:
// creatures, finds, ramps, landmark decorations, objects ($20105C), the
// walker and the occlusion mask.
namespace mindwalker::landscape {

class ViewPainter {
public:
    explicit ViewPainter(const amiga::LoadedImage& landscape);

    // Draws the view facing `direction` (0-3, the compass) into the canvas,
    // whose row 0 is the drawing bitmap's (the screen shows it from row
    // `top` on). Floor, ceiling levels come from the world.
    void paint(screen::Canvas&, const ViewCells&, int direction, int floor_level, int ceiling_level,
               int top = 0) const;

private:
    struct Cell {
        std::uint16_t terrain{}, objects{}, shape{};
        int lattice_column{}, lattice_row{}, shade{};
    };
    using Turned = std::array<std::array<Cell, 4>, 4>; // [screen column][row]
    Turned turn(const ViewCells&, int direction) const;
    void paint_grid(screen::Canvas&, int top) const;

    const amiga::LoadedImage& a_;
    FixedCamera camera_;
};

} // namespace mindwalker::landscape
