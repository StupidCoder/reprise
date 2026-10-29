#pragma once
// Tweened values of the 3DS adventures' engine: an object's position,
// offset, scale, alpha and colour change either at once or over a number of
// frames along one of the engine's curves.
//
// Reconstructed from the fifth adventure's code: start 0x2635c4 (two
// components) / 0x2af7f8 (one), set 0x2aeda4, arm 0x2b0ed8 (curve <= 0x40,
// duration > 0), update 0x2b2044. The progress t = elapsed / duration (one
// step per frame) is shaped by the curve and the value is
// from + (to - from) * f(t):
//   0 linear, 1 t^2, 2 1 - (1 - t)^2, 3 2t^2 / 1 - 2(1 - t)^2 (halves),
//   4 sin(pi t) (out and back), 5 4t(1 - t) (out and back);
//   curves below 0x20 stop at the end; 0x20..0x3f repeat (0x20 linear,
//   0x21 (1 - cos 2 pi t) / 2, 0x22..0x24 random wobbles); 0x40 linear
//   without end.
// Values seen in the reference (0x2b2084 writes elapsed, 0x2b2184 f(t)):
// curve 1 over 16 frames gives 1/256, 4/256, 9/256 ... in frames 1, 2, 3.
#include <array>
#include <cmath>
#include <cstdint>

namespace picarat::adventure56 {

// The curve value f(t) for t in [0, 1].
float tweenCurve(int curve, float t);

// 0x2af518: an angle in radians wrapped into [-pi, pi): unchanged while
// a / 2pi lies in [-0.5, 0.5), else a - floor(a / 2pi + 0.5) * 2pi.
inline float wrapAngle(float a) {
    const float t = a * 0.159154937f; // 0x3e22f983
    if (t >= -0.5f && t < 0.5f) {
        return a;
    }
    return a - std::floor(t + 0.5f) * 6.28318548f; // 0x40c90fdb
}

template <int N>
class Tween {
public:
    using Value = std::array<float, N>;

    const Value& value() const { return value_; }

    float operator[](int i) const { return value_[std::size_t(i)]; }

    bool moving() const { return active_; }

    const Value& target() const { return to_; }

    // 0x2aeda4: the value at once (any running change stops).
    void set(const Value& v) {
        active_ = false;
        value_ = v;
        from_ = to_ = v;
    }

    // 0x2635c4 / 0x2b0ed8: towards `v` over `frames` along `curve`; a zero
    // duration, an unknown curve or no change set the value at once.
    void start(const Value& v, float frames, int curve) {
        if (v == value_ || frames <= 0 || curve < 0 || curve > 0x40) {
            set(v);
            return;
        }
        from_ = value_;
        to_ = v;
        duration_ = frames;
        elapsed_ = 0;
        curve_ = curve;
        active_ = true;
    }

    // 0x2b2044: one frame; at a frame ratio above 1 (2400) the game's
    // tweens advance by the display frames a game frame takes (sixth
    // adventure console: a 12-frame move under 2400 2 ends after 6 game
    // frames).
    void step(float frames = 1.0f) {
        if (!active_) {
            return;
        }
        elapsed_ += frames;
        bool done = false;
        if (curve_ < 0x20) {
            if (elapsed_ >= duration_) {
                elapsed_ = duration_;
                done = true;
            }
        } else if (curve_ < 0x40) {
            while (elapsed_ >= duration_) {
                elapsed_ -= duration_;
            }
        }
        const float f = tweenCurve(curve_, elapsed_ / duration_);
        for (int i = 0; i < N; ++i) {
            value_[std::size_t(i)] =
                from_[std::size_t(i)] + (to_[std::size_t(i)] - from_[std::size_t(i)]) * f;
        }
        if (done) {
            active_ = false;
            // The curve's own end value stays (out-and-back curves return).
        }
    }

private:
    Value value_{}, from_{}, to_{};
    float duration_ = 0, elapsed_ = 0;
    int curve_ = 0;
    bool active_ = false;
};

} // namespace picarat::adventure56
