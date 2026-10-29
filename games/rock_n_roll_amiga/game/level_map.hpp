#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/tile_animation.hpp"
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

    // From the header (game.md "Level files"); set-up $686E2.
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

    // Time limit in seconds (+$1C6; $FFFF = none, the top bar then says
    // "NO"), set at level start ($6C780).
    std::uint16_t time_limit{0xFFFF};

    // Switch lists ($29-$2E use lists 0-5 at +$218, +$254, …, 60 bytes
    // each): cell changes (x, y, value), ended by x = $FFFF.
    struct CellChange {
        int x{}, y{};
        std::uint8_t value{};
    };
    std::array<std::vector<CellChange>, 6> switch_lists;
    static std::vector<CellChange> read_changes(const data::Level&, std::size_t at);

    // Crumbling floor: every crumble into the abyss counts this down
    // (+$1D8; switches $2D/$2E reset it from +$1DA); at 0 a cell-change list
    // (+$1DC, or +$1FA after those switches) is played ($6ECB0).
    std::uint16_t crumble_count{};
    std::vector<CellChange> crumble_list, crumble_list_switched;
    std::uint16_t crumble_count_switched{};
    bool crumble_switched{};

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

    void set(long index, std::uint8_t value) {
        if (index >= 0 && index < long(cells.size())) cells[std::size_t(index)] = value;
    }

    // Which picture each cell value shows ($72CBE, rebuilt at set-up) and
    // the animations that change it. The animations' counters run on from
    // level to level: whoever starts a level carries them over.
    TileTable tiles;
    TileAnimator animation;

    // The pushable block ($6D984, $6B372): one slide at a time. Its state
    // is the program's, kept in fixed places the set-up never resets.
    struct BlockSlide {
        int count{0xFF};             // $6DAF4: 0-$36 in steps of 2 while sliding, $FF idle
        int direction{};             // $6DAF8: 1 up, 2 right, 3 down, 4 left; 0 none
        Fixed last_vx{}, last_vy{};  // $6D978/$6D980: the speeds at the last push test
        long from{}, to{};           // $6DAEC/$6DAF0: the block's cell and the one it enters
        std::uint8_t value{0xC0};    // $6B1AC: what the entered cell becomes ($C0, or $24 when it sank)
        bool sinking{};              // $6B1AE = $80: the sinking pictures
        bool counts_for_warp{};      // $69F88: sank into a $14 cell
    } block;

    // What a bomb leaves in the 3 x 3 cells around it (+$1CA, $6BF56).
    std::array<std::uint8_t, 9> after_bomb{};

    // The cell at a linear index (row * columns + column), as the original
    // addresses them; indices outside the map read as 0.
    std::uint8_t cell(long index) const {
        return index >= 0 && index < long(cells.size()) ? cells[std::size_t(index)] : 0;
    }
};

} // namespace rocknroll::game
