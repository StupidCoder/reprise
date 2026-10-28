#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include <cstdint>
#include <vector>

// A level while it is played: its cells (which the rules change) and the
// settings from the level file's header that the ball needs.
namespace rocknroll::game {

// Positions and speeds are in tiles as 16.16 fixed point, as the original
// keeps them: one tile ($10000) is 16 pixels, so $1000 is one pixel.
using Fixed = std::int32_t;
constexpr Fixed tile_size = 0x10000;

struct LevelMap {
    int columns{}, rows{};
    std::vector<std::uint8_t> cells; // row by row

    // From the header (formats.md "Level files"); set-up $686E2.
    Fixed camera_limit_x{}, camera_limit_y{}; // +$3D0, +$3D4 ($71A50/$71A54)
    Fixed camera_start_x{}, camera_start_y{}; // +$3D8, +$3DC ($71A30/$71A34)
    Fixed view_start_x{}, view_start_y{};     // +$3E0, +$3E4 ($71A38/$71A3C)

    static LevelMap from(const data::Level&);

    // The cell at a linear index (row * columns + column), as the original
    // addresses them; indices outside the map read as 0.
    std::uint8_t cell(long index) const {
        return index >= 0 && index < long(cells.size()) ? cells[std::size_t(index)] : 0;
    }
};

} // namespace rocknroll::game
