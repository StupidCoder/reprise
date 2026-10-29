#pragma once
// The fourth adventure's jump-and-vanish puzzles (loop 3190, overlay 26
// lt4CLoopNazoVanish; puzzle type 27).
//
// How they play
// -------------
// Balls sit on a board of cells (some cells are holes). Dragging a ball over
// a neighbouring ball onto the free cell behind it makes the jumped ball
// vanish; dropped anywhere else, the ball goes back. While a ball is held,
// its cell and the cells it may land on are tinted. The puzzle is solved
// when one ball is left (on the goal cell, when the puzzle names one). The
// last jump can be taken back, one at a time, and everything put back.
//
// Commands (2400, ov26 021075a0): 0 everything back, 1 take back a jump,
// 10 board, 11 a block of holes, 20 judge, 21 goal cell, 30 balls on the
// board, 31 balls that can jump, 40 a ball, 41 pick a ball up, 42 drop it.
// The draw hook (021070f8) tints the cells.
// Reconstructed from overlay 26; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class VanishLoop final : public Loop {
public:
    VanishLoop(Scene& scene, Services& services, std::span<const Value> arguments);
    ~VanishLoop() override;

    bool command(gss::Call& call, int id) override;

protected:
    void endFrame() override;

private:
    struct Ball {
        std::uint32_t name = 0;
        int startX = 0, startY = 0; // +4
        int x = -1, y = -1;         // +8: -1 once it vanished
    };

    // A cell (0x28 bytes): its neighbours and the cells behind them, by
    // direction (right, down, left, up; table 0207b828).
    struct Cell {
        std::array<int, 4> next{-1, -1, -1, -1}; // +0x00
        std::array<int, 4> far{-1, -1, -1, -1};  // +0x10
        int px = 0, py = 0;                      // +0x20
        bool hole = false;                       // +0x24 bit 0
        int ball = -1;                           // +0x26
    };

    // A jump to take back (+0x180).
    struct Jump {
        int ball = 0, jumped = 0;
        int fromX = 0, fromY = 0, jumpedX = 0, jumpedY = 0;
    };

    bool setup(gss::Call& c);
    bool addHoles(gss::Call& c);
    bool addBall(gss::Call& c);
    bool judge(gss::Call& c);
    // 021065ec / 0210677c.
    void reset();
    bool takeBack();
    // 02106af0 / 02106c34.
    bool pickUp(const std::string& name);
    int drop(const std::string& name);
    // 02106940 / 021069c0.
    int onBoard() const;
    int canJump() const;

    int cellAt(int x, int y) const;
    Cell* cell(int index);
    bool landing(const Cell& from, int direction) const;
    void moveBall(const Ball& ball, int px, int py);
    std::string text(std::uint32_t address) const;

    int across_ = 0, down_ = 0;       // +0x150, +0x152
    int originX_ = 0, originY_ = 0;   // +0x14c, +0x14e
    int size_ = 0;                    // +0x14a
    std::array<int, 2> goal_{-1, -1}; // +0x154
    std::string animation_, look_;    // +0x120 the cell tint's animation, +0x134 its sequence
    std::vector<Ball> balls_;         // +0x160
    std::vector<Cell> cells_;         // +0x170
    std::vector<Jump> jumps_;         // +0x180
    int held_ = -1, heldCell_ = -1;   // +0x11c, +0x11e
};

} // namespace picarat::adventure4
