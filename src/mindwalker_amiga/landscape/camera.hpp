#pragma once
#include <array>

// How the landscape's view is seen. The scene places the view's cells in
// world units (X across, Z into the picture, Y = the terrain level, growing
// downwards on screen); a camera turns world points into screen points.
//
// FixedCamera is the original's (A $202E2C): the four cells of a row 80 units
// apart (X = column * 80 - 120), rows 80 apart from Z = 64, and
//   x = X * 256 / (Z + 256) + 160,  y = Y * 256 / (Z + 256)
// with C's truncating division. The original turns the cell tables to face
// one of four directions ($20129A) before projecting; a free camera can
// replace this class later (an enhancement, off by default).
namespace mindwalker::landscape {

struct ScreenPoint {
    int x{}, y{};
};

class FixedCamera {
public:
    static constexpr int column_spacing = 80, row_spacing = 80, first_row_z = 64;

    static int cell_x(int column) { return column * column_spacing - 120; }
    static int cell_z(int row) { return row * row_spacing + first_row_z; }

    ScreenPoint project(int x, int y, int z) const {
        return {x * 256 / (z + 256) + 160, y * 256 / (z + 256)};
    }
};

} // namespace mindwalker::landscape
