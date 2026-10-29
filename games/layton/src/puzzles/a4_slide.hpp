#pragma once
// The fourth adventure's sliding-block puzzles and tilting mazes (loop
// 3130, overlay 21 lt4CLoopNazoSlide; puzzle types 12 and 30).
//
// How they play
// -------------
// The board is a grid of square cells. Pieces (objects of kind 24) are
// dragged with the pen: a piece follows the pen pixel by pixel until one of
// its collision boxes would overlap a wall or another piece, slides along
// the obstacle when it can, and snaps to the nearest cell when it is let
// go (the piece scripts Nazo_SlideObjNormal / Nazo_SlideObjSlide of
// lt4_prog_nazo do the snapping and count the moves). The board's frame and
// any inner walls are the touch areas of an object called "lt4_slide_wall".
// The puzzle is solved when every goal cell is covered by a piece showing
// the goal's sequence; then the loop either sets the verdict or calls a
// block of the puzzle's script.
//
// In the mazes the "piece" is a ball (the maze object named by command 2):
// it rolls toward the pen, and coloured gate cells turn the maze (the
// script block Nazo_MazeReverse) and switch which coloured walls (kind 15
// solid, 19 open) block it.
//
// The puzzle's script talks to the loop with command 2400 and a sub-command
// (ov21 02106648): 0 board, 1 wall cells, 2 piece look and cell, 3 piece
// boxes, 4 back to the start, 5 goal, 6 move counter, 7 drag a piece, 8 count
// a move, 9 a wall object with boxes, 10 gate, 11 block to call when solved,
// 12 a second counter. The pen hook (02108a70) checks goals and gates.
// Reconstructed from overlay 21; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class SlideLoop final : public Loop {
public:
    SlideLoop(Scene& scene, Services& services, std::span<const Value> arguments);

    bool command(gss::Call& call, int id) override;

protected:
    void afterPen() override;

private:
    struct Cell {
        int x = 0, y = 0;
    };

    // A cell rectangle in pixels (goals and gates; +0x128, +0x21c).
    struct Box {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    };

    struct Goal {
        Box box;
        std::uint32_t sequence = 0; // the piece sequence it wants (name hash)
    };

    struct Gate {
        Box box;
        int x = 0, y = 0; // where the ball goes after the turn (pixels)
        int colour = 0;   // the wall colour it is active for
        int next = 0;     // the colour it switches to
    };

    void reset();
    bool board(gss::Call& c);
    bool wallCells(gss::Call& c);
    bool pieceLook(gss::Call& c);
    bool pieceBoxes(gss::Call& c);
    bool restart(gss::Call& c);
    bool addGoal(gss::Call& c);
    bool counter(gss::Call& c);
    bool drag(gss::Call& c);
    bool countMove(gss::Call& c);
    bool wallObject(gss::Call& c);
    bool addGate(gss::Call& c);
    bool solvedBlock(gss::Call& c);
    bool namedCounter(gss::Call& c);

    // 02107fbc: moves a piece toward `target` (pixels) step by step.
    bool move(Chara& piece, Cell target, int mode);
    // 021086fc: whether the piece's own boxes at `at` overlap any of `walls`.
    bool collides(const Chara& piece, Cell at, const std::vector<Box>& walls) const;
    // 021091b8: the coloured walls of `colour` block, the others open.
    bool setColour(int colour);
    // 021089bc: the pixel position of the cell nearest a piece.
    Cell cellPixel(const Chara& piece) const;
    bool covers(const Chara& piece, const Box& box) const;
    void showCount();

    Cell origin_, count_;            // +0x114, +0x118
    int size_ = 1;                   // +0x11c cell size in pixels
    int dragSound_ = -1;             // +0x120
    int solvedSound_ = -1;           // +0x124
    std::vector<Goal> goals_;        // +0x128 (at most 16)
    bool solved_ = false;            // +0x1ec
    int moves_ = 0;                  // +0x1f0
    int inset_ = 0;                  // +0x1f4 how far the boxes stay inside the cells
    float frames_ = 1.0f;            // +0x1f8 frames of a snap
    std::string ball_;               // +0x1fc the maze's ball (none: a block puzzle)
    std::uint32_t ballSequence_ = 0; // +0x210
    bool ballOn_ = false;            // +0x214 the ball shows its "_on" sequence
    int colour_ = 0;                 // +0x218
    std::vector<Gate> gates_;        // +0x21c (at most 48)
    bool onGate_ = false;            // +0x5e0
    std::string ballLook_;           // +0x5e1
    std::string solvedBlock_;        // +0x605
};

} // namespace picarat::adventure4
