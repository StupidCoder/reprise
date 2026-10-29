#pragma once
// The fourth adventure's cutting puzzles (loop 3110, overlay 18
// lt4CLoopNazoCut; puzzle type 10).
//
// How they play
// -------------
// A grid of dots (places "xxx_yyy") lies on the lower screen; the puzzle
// names the grid lines that exist. Dragging from a dot along a grid line
// cuts the line from dot to dot as the pen passes the next dot (and mends a
// cut line when it goes over it again); the cuts are drawn as lines. The
// script asks whether the cuts are exactly the answer lines.
//
// In path mode (75, 133) the cuts form one path from a start dot: a press
// only takes at a start dot or at the end of the path, the path may not run
// into itself, going back along it takes it back, and dots with buckets get
// filled (the bucket's block runs with {bucket, order, 1}) or emptied on the
// way back. An arrow ("arw") shows the end of the path.
//
// Commands (2400, ov18 021069c0): 0 grid, width, reach and mode, 1 the
// colour, 2 lines that exist, 3 everything uncut, 4 no answer, 5 answer
// lines, 6 judge, 7 a bucket, 8 is a bucket filled n-th, 9 a start dot.
// Reconstructed from overlay 18; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class CutLoop final : public Loop {
public:
    CutLoop(Scene& scene, Services& services, std::span<const Value> arguments);
    ~CutLoop() override;

    bool command(gss::Call& call, int id) override;

protected:
    void afterPen() override;
    void endFrame() override;

private:
    // A grid line between neighbouring dots (12 bytes).
    struct Edge {
        bool exists = false; // bit 0: a line of the puzzle
        bool cut = false;    // bit 1
        bool answer = false; // bit 2
        std::uint32_t a = 0, b = 0;
    };

    // A bucket on a dot (0x2c bytes).
    struct Bucket {
        std::uint32_t id = 0;
        std::uint32_t place = 0;
        int order = -1; // filled as the n-th, -1 empty
        std::string block;
    };

    bool setup(gss::Call& c);
    bool setColour(gss::Call& c);
    bool markLine(gss::Call& c, bool answer);
    void reset();
    bool judge() const;
    bool addBucket(gss::Call& c);

    // 021075b4 / 02107700 / 02107eec.
    void press(float x, float y, bool inside);
    void drag(float x, float y);
    void release();
    // 02108310 (path mode): 0 free, 1 going back along the path, 2 blocked;
    // fills and empties buckets.
    int pathStep(int dx, int dy, std::uint32_t next);
    // 02108644: the arrow on the end of the path.
    void placeArrow();
    // 02107f24: cuts or mends the line from a dot in a direction.
    void toggle(int x, int y, int dx, int dy);

    // 02108150: the line from (x, y) one step in (dx, dy); -1 off the list.
    int edgeIndex(int x, int y, int dx, int dy) const;
    Edge* edge(int x, int y, int dx, int dy);
    // 021082ac: a line of the puzzle from (x, y) toward (dx, dy) on the grid.
    bool lineAt(int x, int y, int dx, int dy);
    // 021081dc: the dot's place ("%03d_%03d"), made when missing.
    std::uint32_t dot(int x, int y);
    std::uint32_t dotName(int x, int y) const;
    std::array<int, 2> cellOf(const Place& p) const;
    // +0x160 indexed by +0x170; index -1 reads the path end before it.
    std::uint32_t startAt(int i) const;
    std::string text(std::uint32_t address) const;

    int originX_ = 0, originY_ = 0;         // +0x11c
    int across_ = 0, down_ = 0;             // +0x120: cells
    int size_ = 0;                          // +0x124
    float width_ = 0, reach_ = 0;           // +0x128, +0x12c
    int sound_ = 0;                         // +0x130
    std::uint16_t colour_ = 0x8000;         // +0x134
    std::vector<Edge> edges_;               // +0x138
    std::uint32_t over_ = 0;                // +0x148: the dot under the pen
    std::uint32_t current_ = 0;             // +0x14c: the dot the cutting is at
    int cuts_ = 0;                          // +0x150
    bool pathMode_ = false;                 // +0x158
    std::uint32_t pathEnd_ = 0;             // +0x15c
    std::array<std::uint32_t, 4> starts_{}; // +0x160
    int startCount_ = 0;                    // (the overlay's static counter)
    int start_ = -1;                        // +0x170
    std::array<int, 2> lock_{};             // +0x174: the only way on from the start
    std::vector<Bucket> buckets_;           // +0x17c
    int filled_ = 0;                        // +0x18c
};

} // namespace picarat::adventure4
