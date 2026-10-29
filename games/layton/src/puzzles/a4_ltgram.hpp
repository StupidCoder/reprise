#pragma once
// The fourth adventure's shape-fitting puzzles (loop 3150, overlay 20
// lt4CLoopNazoLtgram; puzzle type 14).
//
// How they play
// -------------
// Pieces (objects of kind 20) are dragged onto a grid, turned in quarter
// turns and, with a button, flipped. The piece scripts of lt4_prog_nazo
// (Nazo_ObjLTGramNormal / _Slide / _Rotate) do the dragging and turning;
// the loop keeps the board and judges. A puzzle is solved when the pieces'
// boxes cover every cell of the target regions (or, for a region with a
// count, at least that many of its cells). Some puzzles first have to fill
// "before" regions exactly: then listed pieces change their look and become
// fixed (kind 19) before the real target counts.
//
// Commands (2400, ov20 021065bc): 0 board and the move/turn icons (and the
// flip button), 1 every piece back, 2 a target region, 3 a piece's grid,
// 4 judge after a move and put the flip button by the piece, 5 flip a
// piece, 6 a piece's extent around its position, 7 a "before" region,
// 8 a piece's look once the "before" regions are filled.
// Reconstructed from overlay 20; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class LtgramLoop final : public Loop {
public:
    LtgramLoop(Scene& scene, Services& services, std::span<const Value> arguments);

    bool command(gss::Call& call, int id) override;

private:
    // A region of cells: x, y, across, down (+0x120 / +0x26c).
    struct Region {
        int x = 0, y = 0, across = 0, down = 0;
    };

    struct Look {
        std::uint32_t object = 0, sequence = 0;
    };

    void reset();
    bool board(gss::Call& c);
    bool restart(gss::Call& c);
    bool addTarget(gss::Call& c);
    bool grid(gss::Call& c);
    bool moved(gss::Call& c);
    bool flip(gss::Call& c);
    bool extent(gss::Call& c);
    bool addBefore(gss::Call& c);
    bool addLook(gss::Call& c);

    // 02107480: judges the board; true once solved.
    bool judge();
    // 02107640: whether the pieces' boxes (area id `areaId`) cover the
    // regions.
    bool covered(const std::vector<Region>& regions, int areaId);

    std::array<int, 2> origin_{}, count_{}; // +0x114, +0x118
    int size_ = 1;                          // +0x11c
    std::vector<Region> targets_;           // +0x120 (16)
    // +0x22c: cells of target region i that must be covered (0: all); the
    // judge reads it by index for the "before" regions too.
    std::array<int, 16> least_{};
    bool solved_ = false;        // +0x224
    std::vector<Region> before_; // +0x26c (16)
    bool beforeDone_ = true;     // +0x370
    std::vector<Look> looks_;    // +0x374 (16)
    int targetArea_ = -1;        // +0x3f8: area id the target counts
};

} // namespace picarat::adventure4
