#pragma once
// Knight's tour (puzzle type 17, ARM9 overlay 43, code 020f78a0..020f8314).
//
// How the puzzle plays
// --------------------
// A chess knight stands on a small board. Tapping a square the knight can
// reach with a chess knight's move (two squares in one direction and one to
// the side) makes it jump there; visited squares are marked red and can never
// be entered again. The puzzle is solved when every square has been visited.
// Squares the knight can currently jump to are highlighted.
//
// The parameter script supplies the board: its position on the lower screen,
// the cell size, the number of columns and rows, and the starting square
// (opcode 0x44).
//
// A tap counts only when the stylus is pressed and released on the same
// square. A jump is animated over 15 frames along a parabola one cell high.
//
// Field names follow the original object; offsets such as "+6c" are byte
// offsets inside it, used when comparing with the original game.
#include "graphics/graphics.hpp"
#include "system/effect_queue.hpp"
#include "system/script.hpp"

namespace picarat {

class KnightPuzzle {
public:
    // Contents of a board cell (+90).
    static constexpr std::uint8_t kUnvisited = 0;
    static constexpr std::uint8_t kVisited = 1;
    static constexpr std::uint8_t kReachable = 2; // unvisited and one knight's move away
    // A visited cell that is also highlighted. The original code clears and
    // accepts this value, and draws it orange, but none of the recovered paths
    // creates it.
    static constexpr std::uint8_t kVisitedReachable = 3;

    // Engine states (+6c).
    static constexpr int kIdle = 0;     // waiting for a press
    static constexpr int kPressed = 1;  // stylus down on a square, waiting for release
    static constexpr int kComplete = 2; // every square visited

    // Frames a jump takes.
    static constexpr int kJumpFrames = 15;

    // Sound effects: 3 when the knight jumps (not for placing it on the start
    // square), 0x11b when it lands.
    EffectQueue sounds;

    explicit KnightPuzzle(std::span<const std::uint8_t> script);

    // Back to the starting square with every other square unvisited.
    void reset();

    // One frame of input handling.
    void update(const Touch& touch);

    // Advances the jump animation; the original draw service does this right
    // before drawing (020f7ce4).
    void animate();

    void render(Surface& target, const Sprite& sprite, int yOffset) const;

    // Judged automatically once the board has been complete for 46 frames.
    bool solved() const { return state == kComplete && counter > 45; }

    int state = kIdle;                     // +6c
    std::int16_t counter = 0;              // frames spent in kComplete
    std::int16_t column = 0, row = 0;      // the knight's square
    std::int16_t targetX = 0, targetY = 0; // pixel position of that square
    float displayX = 0, displayY = 0;      // where the knight is drawn
    float startX = 0, startY = 0;          // where the current jump began
    std::int16_t jump = -1;                // +8c: frames left in the jump (15..0); -1 = standing
    std::vector<std::uint8_t> cells;       // +90: one entry per square, row by row

    // Board layout from the parameter script.
    std::int16_t originX = 0, originY = 0; // top-left corner of the board (lower screen)
    std::int16_t cellSize = 0;             // square size in pixels
    std::int16_t columns = 0, rows = 0;
    std::int16_t initialColumn = 0, initialRow = 0; // starting square

    // Square under the stylus when it was pressed (-1 when none).
    std::int16_t pressedColumn = -1, pressedRow = -1;

private:
    // Last stylus position while held: a release reports no position, so the
    // square is taken from where the stylus last touched.
    int lastX_ = 0, lastY_ = 0;

    // The board cell at (x, y), or nullptr outside the board.
    std::uint8_t* cell(int x, int y);

    // Jumps to (x, y) if it is highlighted; returns whether the knight moved.
    bool move(int x, int y);

    // Recomputes the highlighted squares around the knight.
    void markReachable();
};

} // namespace picarat
