#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include <array>
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

    // Teleports: $22 cells jump between the points of a pair (+$380, four
    // pairs of camera x, y), $23 cells to one point (+$3C0, +$3C4).
    std::array<Fixed, 8> pair_x{}, pair_y{};
    Fixed beam_x{}, beam_y{};

    // The level's two puzzle countdowns, from the header at set-up ($6C754):
    // $6D80E (+$1BE; cells $26, repair sets on $14, small balls) ends in the
    // secret warp, $6D87E (+$1C8; cells $24) in the level's reward.
    std::uint16_t warp_count_start{}, reward_count_start{};
    std::uint16_t warp_count{}, reward_count{};

    // The value a collected item leaves in its cell ($6EC00): from header
    // +$1D5 at the start, refilled from +$1D4 after each use ($6EC02).
    std::uint8_t left_behind{};
    std::uint8_t refill{};

    static LevelMap from(const data::Level&);

    // $6EC02: the cell takes the value left behind, which is refilled.
    void clear(long index) {
        if (index >= 0 && index < long(cells.size())) cells[std::size_t(index)] = left_behind;
        left_behind = refill;
    }

    // The cell at a linear index (row * columns + column), as the original
    // addresses them; indices outside the map read as 0.
    std::uint8_t cell(long index) const {
        return index >= 0 && index < long(cells.size()) ? cells[std::size_t(index)] : 0;
    }
};

} // namespace rocknroll::game
