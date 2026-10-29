#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/game/font.hpp"
#include <array>
#include <cstdint>

// The eye's overview map ($6B67E): the view cleared, "CREATING MAP WITH
// DEPTH: nn", then one pixel per cell ($6B77A) and the ball's place
// ($6B6FA). A cell's colour comes from value ranges ($77E7D: first value,
// count; $FF ends) whose colours ($77ECA) the magic-map pieces reveal one
// by one ($6EB22: piece n copies $77EF2[i] for range i = $77F1A[n − 1]).
// The depth is printed but does not limit the drawing (the compare at
// $6B7DA is never acted on).
namespace rocknroll::game {

// The ranges and their colours with `depth` ($77E62).
class OverviewColours {
public:
    OverviewColours() = default;
    OverviewColours(const data::Program&, int depth);
    // A cell's colour in map column `column`. $6B7BE scans every range for
    // the cell value; at the first match it plots the range's colour, but
    // the plotting reuses D0 (the value) for the pixel's bit number,
    // 7 - column % 8, and the scan goes on through the later ranges with
    // that number: a later range holding it adds its colour too (bset).
    // In practice only cells of the first range (1-4) pick up the second
    // range's colour (5-8) in columns 0-2 of every eight.
    std::uint8_t colour(std::uint8_t value, int column) const;

private:
    struct Range {
        int first, count;
        std::uint8_t colour; // 0 until revealed
    };
    std::vector<Range> ranges_;
};

// Cells the original draws per frame: every cell scans all 37 ranges, so
// the time per cell hardly varies (measured in level 1: about nine rows
// of 70 a second, 12.6 cells a frame).
constexpr double overview_cells_per_frame = 12.6;

// "CREATING MAP WITH DEPTH:" and the depth's two digits (row 0).
std::vector<ScreenText> overview_texts(const data::Program&, int depth);

// Where the map's top-left cell lies in the view's buffer ($6B884: centred
// in 240 x 112 from buffer x 32 to whole bytes, eight lines down).
struct OverviewOrigin {
    int x, y;
};
OverviewOrigin overview_origin(int columns, int rows);

} // namespace rocknroll::game
