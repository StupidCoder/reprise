#pragma once
// The fourth adventure's line-drawing puzzles (loop 3120, overlay 19
// lt4CLoopNazoLine; puzzle type 11).
//
// How they play
// -------------
// Dots (places) are spread over the lower screen. Dragging from a dot to
// another draws a line between them; drawing the same line again, or
// tapping a line and letting go on it, rubs it out (it shrinks to its middle).
// A line let go away from a dot shrinks back to where it started. Only so
// many lines may be drawn (a message says so); some puzzles cap a line's
// length, keep lines straight across or down, or forbid lines along the
// outer edge of the dots. The puzzle script judges the lines itself
// (commands 4 and 5).
//
// Commands (2400, ov19 02106e2c): 0 width, reach and limits, 1 and 2 the
// colours of the line being drawn and of the others, 3 rub everything out,
// 4 is the number of lines n, 5 is there a line between two dots, 6 options.
// The pen hook (021066b4) draws; the update hook (0210689c) runs the
// shrinking; the draw hook (02106958) draws the lines and their ends.
// Reconstructed from overlay 19; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class LineLoop final : public Loop {
public:
    LineLoop(Scene& scene, Services& services, std::span<const Value> arguments);
    ~LineLoop() override;

    bool command(gss::Call& call, int id) override;

protected:
    void afterPen() override;
    void afterUpdate() override;
    void endFrame() override;

private:
    // A line between two places (by name hash).
    struct Line {
        std::uint32_t a = 0, b = 0;
    };

    bool setup(gss::Call& c);
    bool options(gss::Call& c);
    bool setColour(gss::Call& c, std::uint16_t& colour);
    void clear();
    // 021075a0 / 02107714 / 02107924: the pen pressed, held and lifted.
    void press(float x, float y, bool inside);
    void drag(float x, float y, bool inside);
    void lift(float x, float y);
    // 021083d4: where the end of the line being drawn goes for a pen
    // position (onto the edge of the drawing area, held straight, cut to
    // the longest length).
    void adjust(float& x, float& y, bool inside) const;
    // 02107e60: the last line near a point, or -1.
    int lineAt(float x, float y) const;
    // 0210822c: the line between two places (either way round), or -1.
    int find(std::uint32_t a, std::uint32_t b) const;
    // 0210828c: a line rubbed out, shrinking to its middle.
    void remove(int index);
    // 02108718: a straight line along the outer edge of the dots.
    bool alongEdge(std::uint32_t a, std::uint32_t b) const;
    // The place a point lies on (020d7bb0 within the reach), as a hash.
    std::uint32_t placeAt(float x, float y) const;
    bool tooLong(std::uint32_t a, std::uint32_t b) const;
    std::array<std::int32_t, 2> where(std::uint32_t place) const;
    void showError(bool shown);
    void noLinesFlag(bool on);
    std::string text(std::uint32_t address) const;

    float width_ = 0, reach_ = 0;             // +0x144, +0x148
    std::uint32_t maxLines_ = 0;              // +0x14c
    int drawSound_ = 0, addSound_ = 0;        // +0x150, +0x154
    int removeSound_ = 0;                     // +0x158
    std::uint16_t current_ = 0x8000;          // +0x15c: the line being drawn or chosen
    std::uint16_t other_ = 0x8000;            // +0x15e
    std::vector<Line> lines_;                 // +0x11c
    std::uint32_t start_ = 0;                 // +0x12c: the place the line starts at
    float endX_ = 0, endY_ = 0;               // +0x130
    int chosen_ = -1;                         // +0x138: the line tapped
    std::int32_t errorTime_ = 0;              // +0x13c
    bool drawing_ = false, choosing_ = false; // +0x140 bits 0, 1
    bool error_ = false, shrinking_ = false;  // bits 2, 3
    TweenPoint shrinkA_, shrinkB_;            // +0x160, +0x198
    bool noDots_ = false;                     // +0x1d0
    int maxLength_ = 0;                       // +0x1d4 (pixels; 0 none)
    bool keepY_ = false, keepX_ = false;      // +0x1d8, +0x1dc
    bool noEdge_ = false;                     // +0x1e0
    std::array<int, 4> bounds_{};             // +0x1e4: left, top, right, bottom
};

} // namespace picarat::adventure4
