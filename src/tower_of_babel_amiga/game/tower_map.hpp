#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include <array>
#include <cstdint>

// The tower's cells as the game plays them: four floors of 8 x 8 cells, each
// a set of shape flags (the tower record's 3-bit cell codes translated by the
// unpacker's table $1375A), plus the flags the game derives after unpacking:
// which cells are lit from above ($CCA8) and which use the second square
// colour ($CD90, from the tower's colour pattern).
namespace babel::game {

// Shape flags of a cell (the original keeps them in one byte per cell).
namespace cell {
constexpr std::uint8_t solid = 0x80;       // drawn: a floor slab (or a block)
constexpr std::uint8_t opaque = 0x40;      // hides the faces of neighbours next to it
constexpr std::uint8_t lift = 0x20;        // a lift shaft (platform height from its lift object)
constexpr std::uint8_t lift_open = 0x10;   // light passes down this lift cell ($CCCA)
constexpr std::uint8_t full_height = 0x08; // a supporting block up to the slab above
constexpr std::uint8_t lit = 0x04;         // nothing above it: its top uses the lit pen
constexpr std::uint8_t second = 0x02;      // drawn in the second square colours
} // namespace cell

class TowerMap {
public:
    static constexpr int floors = 4, size = 8;

    // `colour_patterns` is the program's table of eight 4 x 8-row bit patterns
    // ($CCF0, 32 bytes each); the tower's setting picks one.
    TowerMap(const data::Tower&, std::span<const std::uint8_t> colour_patterns);
    // The same with the program's table.
    TowerMap(const data::Tower&, const data::Memory& program);

    // Cells outside the tower (x or z outside 0-7, floors above 3) are empty.
    std::uint8_t at(int floor, int x, int z) const;
    // Floor -1: the ground under the tower, which has only the lit flag.
    bool ground_lit(int x, int z) const { return ground_[std::size_t(z)][std::size_t(x)]; }

private:
    std::array<std::array<std::array<std::uint8_t, size>, size>, floors> cells_{}; // [floor][z][x]
    std::array<std::array<bool, size>, size> ground_{};
};

} // namespace babel::game
