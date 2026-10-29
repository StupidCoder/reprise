#pragma once
// The fourth adventure's lily-pad puzzles (loop 3170, overlay 24
// lt4CLoopNazoJump; puzzle type 24).
//
// How they play
// -------------
// A frog sits on a grid of lily pads of three sizes; it jumps as many cells
// as the size of the pad it sits on, up, down, left or right, and the pad it
// leaves sinks. Four panels (objects "panel_up", "panel_down", ...) show
// where it can land: a pad not yet visited (or, once only one pad is left,
// any pad). The puzzle is solved when the frog stands on the goal with at
// most one unvisited pad left. With no landing left, a message ("f_msg")
// appears and the next tap puts everything back.
//
// Commands (2400, ov24 021065a8): 0 board and frog, 1 a row of pads, 2 back
// to the start, 3 jump toward a panel, 4 judge, 5 place the panels.
// Reconstructed from overlay 24; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class JumpLoop final : public Loop {
public:
    JumpLoop(Scene& scene, Services& services, std::span<const Value> arguments);

    bool command(gss::Call& call, int id) override;

protected:
    void afterPen() override;

private:
    struct Pad {
        std::uint32_t name = 0; // the pad object "x_y"
        int size = 0;           // 1..3; 0 none; -1 sunk from the start
        bool used = false;      // landed on (start: size -1)
    };

    struct Frog {
        std::uint32_t name = 0;
        int x = 0, y = 0, step = 0;
    };

    // The four panels (ov24 table 021078f0): name, offset, landing cell.
    struct Panel {
        std::uint32_t name = 0;
        int dx = 0, dy = 0, x = 0, y = 0;
    };

    bool setup(gss::Call& c);
    bool addRow(gss::Call& c);
    bool jump(gss::Call& c);
    bool judge(gss::Call& c);

    // 021074e4: everything back to the start.
    void restart(const std::string& frog);
    // 021066e4: an object to a cell's middle, at once or jumping; returns
    // the size of the pad the frog jumps from.
    int moveTo(std::uint32_t object, int x, int y, bool jumping);
    // 02106974: a pad's look (sinking, chosen or plain).
    void look(Chara* pad, int size, bool sinking, bool chosen);
    // 02106d34 / 02106b1c.
    void placePanels();
    void updatePanels();
    Pad* pad(int x, int y);
    std::string padName(int x, int y) const;
    bool onBoard(int x, int y) const;

    std::array<int, 2> origin_{}, count_{}; // +0x11c, +0x130
    int cellW_ = 0, cellH_ = 0;             // +0x134, +0x138
    std::array<int, 2> start_{}, goal_{};   // +0x120, +0x128
    std::vector<Pad> pads_;                 // +0x13c
    std::vector<Frog> frogs_;               // +0x14c
    std::array<Panel, 4> panels_{};
    int rows_ = 0;            // +0x15c
    bool lastPad_ = false;    // +0x160
    int messageFrames_ = 0;   // +0x164
    bool restarting_ = false; // +0x168
};

} // namespace picarat::adventure4
