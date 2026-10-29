#pragma once
// Peg solitaire (puzzle type 29, ARM9 overlay 50, code 020f78a0..020f8278).
//
// How the puzzle plays
// --------------------
// The board is the classic cross of 33 holes inside a 7x7 grid: the 3x3
// corners are not part of the board. Some holes start with a ball ("peg").
// The player drags a ball over a direct horizontal or vertical neighbour into
// the empty hole right behind it; the jumped ball is removed. The puzzle is
// solved automatically once only one ball is left on the board.
//
// Each puzzle's parameter script decides which holes start filled, with
// "rectangle" commands (opcode 0x65) applied to the fixed cross.
//
// The engine is a small state machine updated once per frame:
//
//   Idle ──press on a ball──▶ Dragging ──release──▶ Settling ──ball arrived──▶ Idle
//                                                        │
//                                         one ball left  └──▶ Complete (wait 45 frames)
//
// Field names and their comments follow the original object's layout (offsets
// such as "+68" are byte offsets inside the game's puzzle object); the research
// probes compare these fields with the original game frame by frame.
#include "graphics/graphics.hpp"
#include "system/effect_queue.hpp"
#include "system/script.hpp"

namespace picarat {

class PegPuzzle {
public:
    // Contents of one cell of the 7x7 grid.
    static constexpr std::uint8_t kNoHole = 0;    // outside the cross
    static constexpr std::uint8_t kBall = 1;      // a hole holding a ball
    static constexpr std::uint8_t kEmptyHole = 2; // an empty hole

    // Engine states (field +68).
    static constexpr int kIdle = 0;     // waiting for the stylus to press on a ball
    static constexpr int kDragging = 1; // a ball follows the stylus
    static constexpr int kSettling = 2; // the dropped ball glides to its hole
    static constexpr int kComplete = 3; // one ball left; judgment follows shortly

    // Board geometry on the lower screen: 7x7 cells of 24 pixels, the first
    // cell's ball sprite drawn at (14, 11).
    static constexpr int kSize = 7;
    static constexpr int kCellPitch = 24;
    static constexpr int kBoardX = 14;
    static constexpr int kBoardY = 11;

    // Sound effects requested this frame (played by the puzzle scene):
    // 0x118 pick up, 0x119 a ball lands, 0x11a a jumped ball is removed.
    EffectQueue sounds;

    // Builds the starting board from the puzzle's parameter script.
    explicit PegPuzzle(std::span<const std::uint8_t> script);

    // Puts every ball back to its starting position (the "Reset" button).
    void reset();

    // One frame of input handling and board changes (020f7ba8).
    void update(const Touch& touch);

    // One frame of the gliding animation of the held or settling ball (020f80e4).
    void animate();

    // Draws all resting balls, then the moving one on top.
    void render(Surface& target, const Sprite& sprite, int yOffset) const;

    // The game waits 45 frames after the last jump before judging the answer,
    // so the final move is visible before the "solved" screen.
    bool solved() const { return state == kComplete && counter >= 45; }

    int state = kIdle;                       // +68
    std::int16_t counter = 0;                // frames spent in kComplete
    std::int16_t touchX = -1, touchY = -1;   // stylus position while dragging
    std::int16_t column = -1, row = -1;      // cell the held ball came from
    std::int16_t targetX = -1, targetY = -1; // where the moving ball is heading (pixels)
    float displayX = 0, displayY = 0;        // +7c/+80: where the moving ball is drawn
    // +88: the starting board and the live board (stored interleaved in the
    // original object), indexed row * 7 + column.
    std::array<std::uint8_t, 49> initial{}, cells{};

private:
    // Pixel position to cell index. The +370/+373 offsets keep the division
    // positive so that it rounds towards minus infinity for positions left of
    // or above the board (those give negative, i.e. invalid, cells).
    static int cellX(int x) { return (x + 370) / 24 - 16; }

    static int cellY(int y) { return (y + 373) / 24 - 16; }

    static int cellIndex(int column, int row) { return row * kSize + column; }
};

} // namespace picarat
