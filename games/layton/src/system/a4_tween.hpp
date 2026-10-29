#pragma once
// The fourth adventure's tweens: values that move from a start to a target
// over a number of frames along a curve. Everything that animates in the
// game's scenes uses them: object positions and offsets, rotation, scale,
// colour and alpha, screen brightness and scrolling.
//
// Time is counted in frames as 20.12 fixed point (4096 a frame); values are
// 20.12 fixed point too. Each frame the tween advances by the frame time;
// its progress (0..4096) is elapsed * 4096 / total, and the curve turns the
// progress into a weight w (0..4096); the value is
// start + trunc((target - start) * w / 4096). A tween of zero frames ends on
// its next update.
//
// Reconstructed from the engine library in overlay 6 (the gnCMove family):
// start 02081f68, update 02082064 (curves; 0x20 pulses forever), reset 02081f0c; values
// 02082de4 (set) / 02082e44 (start), points 02082f48; the value update
// rules of ov7 020d3234 (one value) and 020d3100 (a point).
#include <array>
#include <cstdint>

namespace picarat::adventure4 {

// 1.0 in the game's fixed point.
inline constexpr std::int32_t kOne = 4096;

// The progress of a tween and its curve (gnCMove).
class Tween {
public:
    // 02081f68: begins a tween of `frames` (20.12; negative counts as 0)
    // along `curve` (0..35); false for an unknown curve or a locked tween.
    bool begin(std::int32_t frames, int curve, bool lock = false);
    // 02081f0c: stops (the value stays where it is).
    void stop();

    // 02082064: advances by `elapsed` (20.12 frames); `finish` ends at
    // once. Returns true while the tween ran this frame (the value changed
    // through apply()).
    template <class Apply>
    void update(std::int32_t elapsed, bool finish, Apply&& apply) {
        if (curve_ < 0) {
            return;
        }
        if (advance(elapsed, finish)) {
            apply(weight_);
            stop();
        } else {
            apply(weight_);
        }
    }

    bool running() const { return curve_ >= 0; }

    bool locked() const { return locked_; }

    int curve() const { return curve_; }

    std::int32_t weight() const { return weight_; }

    // The game's random numbers (0207b7f8: a float 0..1 from the shared
    // xorshift), which curves 0x21..0x23 draw on; the scene that owns the
    // generator installs it (null: 0.5).
    static float (*random)(void* context);
    static void* randomContext;

private:
    float draw() const { return random ? random(randomContext) : 0.5f; }

    // Advances and computes the weight; true when the tween ends.
    bool advance(std::int32_t elapsed, bool finish);
    int curve_ = -1; // +4: 0xffff idle
    bool locked_ = false;
    std::int32_t total_ = 0, elapsed_ = 0; // +8, +0xc
    std::int32_t progress_ = 0;            // +0x10 (0..4096)
    std::int32_t weight_ = 0;              // +0x12
    std::int32_t velocity_ = 0;            // +0x14 (curve 0x23)
};

// One value with a tween (gnCMoveVal / gnCMoveFix).
class TweenValue {
public:
    // 02082de4: sets the value at once.
    void set(std::int32_t v);
    // 02082e44: moves to `target` over `frames` along `curve`; an unknown
    // curve sets the value at once.
    bool move(std::int32_t target, std::int32_t frames, int curve, bool lock = false);
    // An angle (radians, 20.12; ov6 02083c04): turns the short way round
    // and wraps into -pi..pi when a turn ends (02083ea4).
    bool turn(std::int32_t target, std::int32_t frames, int curve, bool lock = false);
    // 020ab930 (5144): starts at `from` and pulses to `to` and back every
    // `frames` until stopped (curve 0x20).
    bool pulse(std::int32_t from, std::int32_t to, std::int32_t frames, bool lock = false);
    void update(std::int32_t elapsed, bool finish = false);

    std::int32_t value() const { return value_; }

    // +0x1c: the value before the last update.
    std::int32_t previous() const { return previous_; }

    std::int32_t target() const { return target_; }

    bool moving() const { return tween_.running(); }

    bool changed() const { return changed_; }

    bool angle = false; // update wraps it like an angle

private:
    Tween tween_;
    std::int32_t value_ = 0, previous_ = 0, target_ = 0, start_ = 0;
    bool changed_ = false;
};

// pi and 2 pi in the game's fixed point (ov6 020838a0, 02083890).
inline constexpr std::int32_t kPi = 0x3244, kTwoPi = 0x6488;
// 02083ea4: an angle beyond -pi..pi brought back into it.
std::int32_t wrapAngle(std::int32_t a);

// A point with a tween (gnCMoveFixVct2).
class TweenPoint {
public:
    using Point = std::array<std::int32_t, 2>;
    // 02082f48: sets the point at once.
    void set(Point p);
    bool move(Point target, std::int32_t frames, int curve, bool lock = false);
    void update(std::int32_t elapsed, bool finish = false);

    Point value() const { return value_; }

    Point target() const { return target_; }

    bool moving() const { return tween_.running(); }

    int curve() const { return tween_.curve(); }

    // A grid the point snaps to (ov6 020833b4, +0x78..+0x88): cells of
    // `size` pixels from `origin`, `count` of them across and down, moves
    // between them taking `frames` (20.12). A size of 0 means no grid.
    struct Grid {
        std::array<int, 2> cell{}, origin{}, count{};
        int size = 0;
        std::int32_t frames = 0;
    };

    void setGrid(const Grid& g) { grid_ = g.size > 0 ? g : Grid{}; }

    const Grid& grid() const { return grid_; }

    // 02083410: the cell nearest to a point, within 0..count.
    std::array<int, 2> cellAt(Point p) const;
    // 0208354c: moves to the cell nearest to `p` along `curve`.
    bool snap(Point p, int curve, bool lock = false);
    // 02083230: straight onto a cell (clamped to the grid).
    bool setCell(std::array<int, 2> cell);

private:
    Tween tween_;
    Point value_{}, previous_{}, target_{}, start_{};
    Grid grid_;
};

} // namespace picarat::adventure4
