#pragma once
#include <array>
#include <cstdint>

// The play screen's character grid. Bruce Lee's world is made of characters:
// the room map, the lanterns, the status line and the changes rooms make
// while playing all live here, and the game reads them back as terrain
// (collisions test the actors' shapes against the glyphs under them). The
// picture is drawn from it.
namespace bruce_lee::game {

struct Cell {
    std::uint8_t code{};   // character
    std::uint8_t colour{}; // cell colour; bit 3 multicolour
};

class Playfield {
public:
    static constexpr int columns = 40, rows = 25;

    Cell& at(int column, int row) { return cells_[std::size_t(row * columns + column)]; }
    const Cell& at(int column, int row) const { return cells_[std::size_t(row * columns + column)]; }

private:
    std::array<Cell, columns * rows> cells_{};
};

} // namespace bruce_lee::game
