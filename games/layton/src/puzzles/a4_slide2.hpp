#pragma once
// The fourth adventure's pushing-block puzzles (loop 3140, overlay 22
// lt4CLoopNazoSlide2; puzzle type 13).
//
// How they play
// -------------
// Like the sliding-block puzzles (a4_slide.hpp), pieces (kind 24) are
// dragged pixel by pixel and slide along obstacles, but a dragged piece
// pushes the pieces in its way: pushable pieces (value 5 not zero) move
// along with it as long as nothing stops them, in turn pushing the pieces
// in their way. When the whole move cannot be pushed, a shorter move along
// each axis is tried. The board, its walls (an object "lt4_slide2_wall"),
// goals and the move counter work as in the sliding puzzles, without the
// mazes.
//
// Commands (2400, ov22 02106540): 0 board, 1 wall cells, 2 piece look and
// cell, 3 piece boxes, 4 every piece back, 5 goal, 6 move counter, 7 drag a
// piece, 8 count a move. The pen hook (021084dc) checks the goals.
// Reconstructed from overlay 22; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_loop_kit.hpp"
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class Slide2Loop final : public Loop {
public:
    Slide2Loop(Scene& scene, Services& services, std::span<const Value> arguments);

    bool command(gss::Call& call, int id) override;

protected:
    void afterPen() override;

private:
    struct Goal {
        kit::Box box;
        std::uint32_t sequence = 0;
    };

    // A piece the move may push (0210802c): its place in the object list,
    // whether it could move by itself, whether it is pushed.
    struct Push {
        Chara* object = nullptr;
        bool free = false, pushed = false;
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

    // 021074e8: moves a piece toward `target`, pushing; true when it moved.
    bool move(Chara& piece, std::array<int, 2> target);
    std::vector<Push> pushable(const Chara& piece, std::array<int, 2> delta,
                               const std::vector<kit::Box>& walls);
    bool canPush(const Chara& piece, std::array<int, 2> delta, std::vector<Push>& list);
    void shift(Chara& piece, std::array<int, 2> start, std::array<int, 2> delta,
               const std::vector<Push>& list);
    std::array<int, 2> cellPixel(const Chara& piece) const;

    std::array<int, 2> origin_{}, count_{}; // +0x114, +0x118
    int size_ = 1;                          // +0x11c
    int dragSound_ = -1;                    // +0x120
    int solvedSound_ = -1;                  // +0x124
    std::vector<Goal> goals_;               // +0x128 (16)
    bool solved_ = false;                   // +0x1ec
    int moves_ = 0;                         // +0x1f0
    float frames_ = 1.0f;                   // +0x1f4
};

} // namespace picarat::adventure4
