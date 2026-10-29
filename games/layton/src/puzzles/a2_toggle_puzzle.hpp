#pragma once
// Grid painting (puzzle type 23, class lt2qOnOff2Object).
// ARM9 overlay 45: ROM 0xb5600 (+0x7f0 bytes), loaded at 020f78a0, vtable 020f81a4.
//
// How the puzzle plays
// --------------------
// A grid of cells lies over the picture. Tapping a cell fills it with a
// translucent colour; tapping it again clears it. The player fills the cells
// that form the answer and presses "Submit". The answer is right when exactly
// the cells inside the answer rectangles are filled: every one of them, and
// no other. Some cells may be disabled and cannot be tapped.
//
// The parameter script supplies the grid and its colour (command 0x4a), the
// answer as up to 16 rectangles of cells (0x4b) and disabled cells (0x6e).
//
// After a wrong answer and "Try again", the filled cells are kept (virtual
// functions +22c/+230 save and restore them).
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace picarat {

class TogglePuzzle {
public:
    // Sound effects: 0xf7 a cell is filled, 0xf8 it is cleared.
    EffectQueue sounds;

    // A rectangle of cells (answer areas), in cell units.
    struct Rect {
        std::int16_t x = 0, y = 0, width = 0, height = 0;
    };

    explicit TogglePuzzle(std::span<const std::uint8_t> script);

    // Clears every cell (virtual function +204, 020f79f8).
    void reset();

    // One frame of input (020f7d48).
    void update(const TouchFilter& touch);

    // Virtual function +1fc (020f7a8c).
    bool solved() const;

    // A filled cell to draw, in lower-screen pixels (020f7e2c).
    struct Cell {
        int x = 0, y = 0, width = 0, height = 0;
    };

    std::vector<Cell> cells() const;

    // Fill colour (RGB555) and translucency (0..31). The judgment frames draw
    // the cells opaque instead.
    std::uint16_t color() const { return color_; }

    int alpha() const { return alpha_; }

private:
    // Cell values in grid_.
    static constexpr std::int8_t kDisabled = -1;
    static constexpr std::int8_t kClear = 0;
    static constexpr std::int8_t kFilled = 1;

    std::int16_t originX_ = 0, originY_ = 0; // +0x68: top-left of the grid
    std::int16_t columns_ = 0, rows_ = 0;
    std::int16_t cellWidth_ = 1, cellHeight_ = 1; // cell size in pixels (+0x72)
    std::uint16_t color_ = 0;                     // +0x80, RGB555
    std::uint8_t alpha_ = 0;                      // +0x82
    std::array<std::int8_t, 0x64> grid_{};        // +0x83: rows of 10 cells
    std::vector<Rect> answer_;                    // +0xe8, at most 16

    std::int8_t& at(int x, int y) { return grid_[std::size_t(y * 10 + x)]; }

    // Pixel to cell index (020f79b0 / 020f79d4).
    int cellX(int pixel) const;
    int cellY(int pixel) const;
};

} // namespace picarat
