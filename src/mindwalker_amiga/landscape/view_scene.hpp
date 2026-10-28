#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/landscape/camera.hpp"
#include "mindwalker_amiga/landscape/view_cells.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include <array>
#include <string>
#include <vector>

// Drawing the landscape's view (A $201492 / $201758): the floor grid, then the
// 4 x 4 cells from the back row to the front, each cell's body (pillar,
// pedestal or box), top face, side faces and markers, in the original's pens.
//
// Polygons are filled as graphics.library's AreaEnd does (the blitter's
// one-dot-per-row lines and inclusive fill), matching the original's pens. The depth masks
// and the walker's mask hide the sprites behind the scenery. Not drawn yet:
// finds, landmark decorations, objects ($20105C).
namespace mindwalker::landscape {

class ViewPainter {
public:
    explicit ViewPainter(const amiga::LoadedImage& landscape);

    // Stage state the drawing shows: whether the way on (the marked block's
    // ramp) is still closed ($2099B6: 1 in a new game, 0 once a beacon has
    // marked the block) and which finds are held ($2099BC/$2099BE: their
    // signs beside the score).
    struct Status {
        bool ramp_closed{true};
        std::array<bool, 2> found{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(ramp_closed, found); }
    };
    void set_status(const Status& status) { status_ = status; }

    // Draws the view facing `direction` (0-3, the compass) into the canvas,
    // whose row 0 is the drawing bitmap's (the screen shows it from row
    // `top` on). Floor, ceiling levels come from the world.
    // `score` is printed first, as the grid pass does ($205218 / $20655A).
    void paint(screen::Canvas&, const ViewCells&, int direction, int floor_level, int ceiling_level,
               int top = 0, std::uint32_t score = 0) const;
    // The score as $20655A prints it: decimal without leading zeros, and
    // nothing at all for 0.
    static std::string score_text(std::uint32_t score);
    // The grid and score alone ($205218), as h8 draws them on the display.
    void draw_grid(screen::Canvas& canvas, int top, std::uint32_t score) const { paint_grid(canvas, top, score); }

    // The depth masks of the second pass ($201492: $201758(0, ...)): for
    // rows 0-2, which pixels of the drawing (row 0 = its top) that row's
    // cells cover. A sprite at depth d is visible where rows 0..d-1 cover
    // nothing (the masking minterms at A $2039A0).
    using DepthMask = std::vector<std::uint8_t>; // 320 x 200, 1 = covered
    std::array<DepthMask, 3> depth_masks(const ViewCells&, int direction, int floor_level, int ceiling_level) const;

    // h11's mask for the walker's sprite ($20309C with $2035D2): which pixels
    // of the 16 x (rows + 1) box at (x, top) (drawing rows) show. The box
    // loses the marked block's ramp pole in the walker's cell when the
    // walker is behind it, and at the view's edge columns the side wall of
    // a neighbour more than 8 higher; then the rows in front of the
    // walker's own row cover it as they cover the creatures.
    struct WalkerPlace {
        int x{}, top{}, rows{}, feet{};  // sprite box and its feet (drawing rows)
        int view_row{}, view_column{};   // $20B318 / $20B31C: its place facing the view
        int depth{};                     // $20B310: its depth + 24
        std::uint16_t terrain{};         // its cell's terrain word ($20B320)
        bool ramp_closed{true};          // $2099B6: the way on still closed

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(x, top, rows, feet, view_row, view_column, depth, terrain, ramp_closed);
        }
    };
    std::vector<std::uint16_t> walker_mask(const ViewCells&, int direction, int floor_level, const WalkerPlace&,
                                           const std::array<DepthMask, 3>& masks) const;

private:
    struct Cell {
        std::uint16_t terrain{}, objects{}, shape{};
        int lattice_column{}, lattice_row{}, shade{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(terrain, objects, shape, lattice_column, lattice_row, shade);
        }
    };
    using Turned = std::array<std::array<Cell, 4>, 4>; // [screen column][row]
    Turned turn(const ViewCells&, int direction) const;
    void paint_grid(screen::Canvas&, int top, std::uint32_t score) const;
    // `only_row` >= 0: the mask pass (that row's cells only; no grid, line
    // patterns or visited markers).
    void paint_cells(screen::Canvas&, const ViewCells&, int direction, int floor_level, int ceiling_level, int top,
                     int only_row) const;

    const amiga::LoadedImage& a_;
    FixedCamera camera_;
    Status status_;

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(status_); }
};

} // namespace mindwalker::landscape
