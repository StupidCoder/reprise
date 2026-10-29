#pragma once
// Rose placement (puzzle type 24, class lt2qRoseObject).
// ARM9 overlay 46: ROM 0xb5e00 (+0xb10 bytes), loaded at 020f78a0, vtable 020f86c4.
//
// How the puzzle plays
// --------------------
// A board is divided into square cells, with some walls between cells. The
// player plants and removes roses by tapping cells. A rose covers every cell
// it can reach in at most two steps (up, down, left or right) without passing
// through a wall, including its own cell. The puzzle is solved as soon as
// every cell of the board is covered by exactly one rose; there is no
// "Submit" button (virtual function +220 returns 0, so solving is automatic).
//
// Cells covered once show a "one" marker, cells covered more than once a "two"
// marker, so the player can see overlaps.
//
// The parameter script describes the board: command 0x4c gives the board
// position and size, command 0x4d adds one straight wall along grid lines.
//
// Implementation notes
// --------------------
// The reach of a rose depends only on the walls, so it is computed once per
// cell when the puzzle is built and stored as a 5x5 bit mask around the cell.
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace picarat {

class RosePuzzle {
public:
    // Sound effects: 0x10f a rose is planted, 0x110 it is removed.
    EffectQueue sounds;

    explicit RosePuzzle(std::span<const std::uint8_t> script);

    // Removes all roses (virtual function +204, 020f7dcc).
    void reset();

    // One frame of input (020f7fa4).
    void update(const TouchFilter& touch);

    // Virtual function +1fc (020f7e64).
    bool solved() const { return state_ == kSolved; }

    // One sprite to draw: a coverage marker ("one"/"two") or a "rose".
    struct Draw {
        int x = 0, y = 0;
        const char* animation = "";
    };

    // The sprites to draw this frame, in the original order (020f82dc): per
    // cell, row by row, the marker first and the rose on top. `count` receives
    // the number of entries used.
    std::array<Draw, 2 * 64> draws(int& count) const;

private:
    // Cell flag bits (+0x80, 8 bytes per cell, rows of 8 cells).
    static constexpr std::uint32_t kWallLeft = 1;
    static constexpr std::uint32_t kWallTop = 2;
    static constexpr std::uint32_t kWallRight = 4;
    static constexpr std::uint32_t kWallBottom = 8;
    static constexpr std::uint32_t kRose = 0x10;

    // Bits 5..29: the 5x5 reach mask, bit (dy + 3) * 5 + (dx + 2) for the
    // cell at offset (dx, dy), each in -2..2.
    static std::uint32_t reachBit(int dx, int dy) { return 1u << (((dy + 3) * 5 + dx + 2) & 0xff); }

    // Puzzle states (+0x70).
    static constexpr int kIdle = 0;
    static constexpr int kTouching = 1; // stylus down, waiting for release
    static constexpr int kSolved = 2;

    struct Cell {
        std::uint32_t flags = 0; // walls, rose and reach mask (see above)
        std::int32_t count = 0;  // number of roses covering this cell
    };

    std::array<Cell, 64> cells_{};           // the original storage holds 8x8 cells
    std::int16_t originX_ = 0, originY_ = 0; // +0x68: top-left of the board
    std::int16_t columns_ = 0, rows_ = 0;    // +0x6c
    int state_ = kIdle;                      // +0x70
    std::int16_t startX_ = -1, startY_ = -1; // +0x76: where the stylus was pressed
    std::int16_t touchX_ = -1, touchY_ = -1; // last stylus position

    Cell& at(int x, int y);

    // Walks up to two steps from cell (x, y), currently at offset (dx, dy),
    // marking every reached offset in `mask` (020f7c68).
    void flood(int x, int y, std::uint32_t& mask, int dx, int dy, int depth);

    // Pixel to cell index, 24-pixel cells (020f7bf4 / 020f7c1c).
    static int cell(int pixel, int origin);
};

} // namespace picarat
