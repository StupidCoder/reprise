#pragma once
// The fourth adventure's "lead the cat" puzzles (loop 3180, overlay 25
// lt4CLoopNazoLead; puzzle type 26).
//
// How they play
// -------------
// A cat stands on a grid with walls between some cells. Tapping a cell in
// line with the cat puts down a fish there (from a limited number, shown by
// a counter) and the cat walks to it in a straight line, unless a wall is in
// the way; it eats the fish and waits. The puzzle is solved when the cat
// reaches the goal with every fish used; with all fish used elsewhere, a
// message appears and the next tap after 90 frames starts again.
//
// Commands (2400, ov25 021069c8): 0 board, cat and fish, 1 a row of walls
// (between columns or between rows), 2 back to the start, 3 a row of cells
// where fish may go. The pen hook (02106504) places fish and drives the cat;
// the update hook (02106974) counts down the walk for the footsteps.
// Reconstructed from overlay 25; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class LeadLoop final : public Loop {
public:
    LeadLoop(Scene& scene, Services& services, std::span<const Value> arguments);

    bool command(gss::Call& call, int id) override;

protected:
    void afterPen() override;
    void afterUpdate() override;

private:
    struct Cell {
        std::uint32_t name = 0; // "x_y"
        int px = 0, py = 0;     // its middle in pixels
        bool fish = false;      // a fish lies there
        bool allowed = true;    // fish may go there
    };

    struct Fish {
        std::uint32_t name = 0;    // "fish_n"
        std::string placed, eaten; // looks
        int x = -1, y = -1;
    };

    bool setup(gss::Call& c);
    bool addWalls(gss::Call& c);
    bool addAllowed(gss::Call& c);
    // 02107a54.
    void restart();
    // 02107028: a fish on the cell at a screen point, the cat on its way.
    bool place(float x, float y);
    // 02106cd4 / 02106e98: the cat turns toward the cell; false when it is
    // not in line or a wall is in the way.
    bool turnToward(int x, int y);
    bool clear(int direction, int x, int y, int tx, int ty) const;
    // 02106b40: the cat to a cell, at once or walking.
    void moveCat(int x, int y, bool walking);
    void showCount();
    std::string text(std::uint32_t address) const;

    std::array<int, 2> origin_{}, count_{}, cell_{}; // +0x11c, +0x120, +0x124
    std::array<int, 2> start_{}, goal_{};            // +0x128, +0x12c
    int placeSound_ = 0;                             // +0x130
    int fishCount_ = 0;                              // +0x13c
    int used_ = 0;                                   // +0x140
    int frames_ = 0;                                 // +0x144
    bool busy_ = false;                              // +0x148
    int direction_ = 0;                              // +0x150: up, down, left, right
    std::uint32_t cat_ = 0;                          // +0x14c
    // The cat's looks (its values 4..10): standing, facing up, down, sideways,
    // eating up, down, sideways.
    std::array<std::string, 7> looks_;
    std::array<int, 2> at_{};           // +0x170 the cat's cell
    bool walking_ = false;              // +0x178
    bool arrived_ = false;              // +0x17c
    std::vector<Cell> cells_;           // +0x180
    std::vector<bool> wallsAcross_;     // +0x190: between columns
    std::vector<bool> wallsDown_;       // +0x1a0: between rows
    int acrossRows_ = 0, downRows_ = 0; // +0x134, +0x138
    std::vector<Fish> fish_;            // +0x1b0
    int walk_ = 0, walkBefore_ = 0;     // +0x1c0, +0x1c4
    int allowedRows_ = 0;               // +0x1c8
    Chara* eaten_ = nullptr; // the fish eaten this time (the original keeps it in a register)
};

} // namespace picarat::adventure4
