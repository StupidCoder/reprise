#pragma once
// The fourth adventure's trampoline puzzles (loop 3200, overlay 27
// lt4CLoopNazoTrampoline; puzzle type 28).
//
// How they play
// -------------
// Someone at the top drops an object; the player draws trampolines between
// dots of a grid (a limited total length, shown by a gauge), moves them by
// dragging their middle, re-draws them by an end, or taps one to take it
// away. The drop button runs a simulation: the object falls, bounces off
// the trampolines and must land in the catcher's arms. Hitting an obstacle
// or the ground, leaving the screen or getting stuck breaks it ("msg_crash").
//
// Commands (2400, ov27 02107270): 0 grid, 1 catcher, object size, bounce
// and ground, 2 width and length, 3 colours, 4 sounds, 5 room for
// trampolines and obstacles, 6 an obstacle, 7 everything back, 8 drop,
// 9 stop the drop. The update hook (02106ac0) runs the simulation; the
// draw hook (02106d58) draws trampolines, the one being drawn and the gauge.
// Reconstructed from overlay 27; see .analysis/adventure4/docs/special-loops.md.
#include "puzzles/a4_puzzle.hpp"

namespace picarat::adventure4 {

class TrampolineLoop final : public Loop {
public:
    TrampolineLoop(Scene& scene, Services& services, std::span<const Value> arguments);
    ~TrampolineLoop() override;

    bool command(gss::Call& call, int id) override;

protected:
    void afterPen() override;
    void afterUpdate() override;
    void endFrame() override;

private:
    using Point = std::array<std::int32_t, 2>; // 20.12 pixels

    // A trampoline or an obstacle between two grid dots (24 bytes).
    struct Segment {
        int a = -1, b = -1;      // dot numbers
        std::int32_t where = -1; // +8: where the last bounce hit (0..4096)
        int frames = -1;         // +0xc: frames of the bounce's bend left
        Point speed{-1, -1};     // +0x10: the object's speed then
    };

    enum Flag : unsigned {
        Drawing = 1,    // a trampoline being drawn from an end
        Moving = 2,     // a whole trampoline being moved
        Unchanged = 4,  // picked up and not moved yet
        Shrinking = 8,  // one going away
        Invalid = 0x10, // the one being drawn or moved would not fit
        Picked = 0x20,  // taken from the trampolines
        Falling = 0x40, // the simulation runs
        Crashed = 0x80, // the crash message shows
    };

    bool setup(gss::Call& c);
    bool setCatch(gss::Call& c);
    bool setColours(gss::Call& c);
    bool addObstacle(gss::Call& c);
    void reset();

    // 02107f54 / 02108138 / 0210867c.
    void press(int x, int y, bool inside);
    void drag(int x, int y, bool inside);
    void release();
    // 021096bc: a trampoline taken up again, `end` the end that stays.
    void pickUp(int index, int end);
    // 021095ec: a trampoline added between two points.
    void add(Point a, Point b, int sound);
    // 021097fc / 02109f68: shrinking away to the middle, or back to the end.
    void shrinkAway(Point a, Point b);
    void shrinkBack();
    // 02109a68: where the drawn end goes for a pen position.
    Point drawnEnd(Point pen, bool inside) const;
    // 021090b0 / 0210947c: the trampoline whose end / body is under the pen.
    int endAt(int x, int y) const;
    int bodyAt(int x, int y) const;
    // 02109c2c: would a line from a to b cross a trampoline or an obstacle.
    bool crosses(Point a, Point b) const;
    bool crosses(std::array<int, 2> a, std::array<int, 2> b,
                 const std::vector<Segment>& list) const;

    // 021099a4 / 02109918: the dot nearest a point (-1 off the grid) and a
    // dot's point.
    int dotAt(Point p) const;
    Point dotPoint(int dot) const;
    std::int32_t length(Point a, Point b) const;

    // The simulation (0210a07c): 0 on, 1 crashed, 2 out, 3 stuck, 4 caught.
    int step();
    int bounceOffTrampolines(int& result);
    bool hitObstacle();
    int catchCheck();
    bool touches(Point p, Point a, Point b) const;
    // 0210aeb0: back out of a contact by halving steps along the speed.
    void backOff(Point a, Point b, std::int32_t divisor);
    // 0210b178: reflect the speed off a segment; true when it is too slow
    // going up. `segment` gets the bend.
    bool bounce(Point a, Point b, Segment* segment);

    void showMessage(bool shown);
    std::string text(std::uint32_t address) const;

    int originX_ = 0, originY_ = 0, across_ = 0, down_ = 0, cell_ = 0;     // +0x11c..
    std::int32_t width_ = 0, total_ = 0;                                   // +0x128, +0x12c
    Point catchA_{}, catchB_{};                                            // +0x130, +0x138
    std::int32_t radius_ = 0, bounciness_ = 0, ground_ = 0;                // +0x140, +0x144, +0x174
    std::array<std::uint16_t, 4> colours_{0x8000, 0x8000, 0x8000, 0x8000}; // +0x148
    // +0x150: drawing, taking away, bouncing, crashing, caught.
    std::array<int, 5> sounds_{};
    std::vector<Segment> obstacles_;   // +0x164
    std::vector<Segment> trampolines_; // +0x224
    std::size_t obstacleRoom_ = 0;
    Chara* object_ = nullptr;       // +0x178 "bound_obj"
    Point from_{}, to_{};           // +0x17c, +0x184: the one being drawn or moved
    Point savedFrom_{}, savedTo_{}; // +0x18c, +0x194
    int crashWait_ = 0;             // +0x1a0
    int pickWait_ = 0;              // +0x1a4
    TweenPoint shrinkA_, shrinkB_;  // +0x1a8, +0x1e0
    std::int32_t left_ = 0, drawing_ = 0, shrinkingLength_ = 0; // +0x218..+0x220
    Point position_{}, speed_{};                                // +0x234, +0x23c
    int stuck_ = 0;                                             // +0x244
    unsigned flags_ = 0;                                        // +0x24c
    // The overlay's statics: frames since a bounce, touching the catcher.
    int sinceBounce_ = 0;
    bool touchingCatcher_ = false;
};

} // namespace picarat::adventure4
