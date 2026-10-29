// The fourth adventure's tweens. See a4_tween.hpp.
#include "system/a4_tween.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace picarat::adventure4 {
namespace {

// The ARM9's sine table (4096 steps a turn, 1.0 = 4096; ov6 reads it at
// 02082c1c): round(4096 sin(2 pi i / 4096)), checked against the table in
// the ROM (a research check; no unit test yet).
std::int32_t sine(int index) {
    static const auto table = [] {
        std::array<std::int16_t, 4096> t{};
        for (int i = 0; i < 4096; ++i) {
            t[std::size_t(i)] = std::int16_t(std::lround(4096.0 * std::sin(2 * M_PI * i / 4096.0)));
        }
        return t;
    }();
    return table[std::size_t(index & 0xfff)];
}

// (a * b) / 4096 rounded toward zero, as the game's fixed-point products.
std::int32_t product(std::int64_t a, std::int64_t b) {
    return std::int32_t((a * b) / kOne);
}

std::int32_t square(std::int32_t p) {
    return std::int32_t(std::int16_t(product(p, p)));
}

} // namespace

float (*Tween::random)(void*) = nullptr;
void* Tween::randomContext = nullptr;

bool Tween::begin(std::int32_t frames, int curve, bool lock) {
    if (locked_) {
        return false;
    }
    if (curve < 0 || curve >= 0x24) {
        return false;
    }
    total_ = frames < 0 ? 0 : frames;
    elapsed_ = 0;
    progress_ = 0;
    weight_ = 0;
    velocity_ = 0;
    curve_ = curve;
    locked_ = lock;
    return true;
}

void Tween::stop() {
    curve_ = -1;
    locked_ = false;
    total_ = elapsed_ = 0;
    progress_ = weight_ = 0;
}

bool Tween::advance(std::int32_t elapsed, bool finish) {
    elapsed_ += elapsed;
    bool done = false;
    if (curve_ == 0x20) {
        // A pulse that never ends: the time wraps at the total (DS divider
        // remainder) and the weight is (1 - cos(2 pi p)) / 2, from the
        // cosine column of the sine table (02082064 case 0x20).
        if (total_ > 0) {
            elapsed_ %= total_;
            progress_ = std::int32_t((std::int64_t(elapsed_) << 12) / total_);
        } else {
            elapsed_ = progress_ = 0;
        }
        const std::int32_t c = sine((std::int16_t(progress_) & 0xfff) + 0x400);
        weight_ = std::int16_t(0x800 - std::int16_t(c * 0x800 / kOne));
        return false;
    }
    if (curve_ > 0x20) {
        // 0x21..0x23 run forever like 0x20 (the time wraps at the total)
        // and wobble at random (02082064):
        if (total_ > 0) {
            elapsed_ %= total_;
            progress_ = std::int32_t((std::int64_t(elapsed_) << 12) / total_);
        } else {
            elapsed_ = progress_ = 0;
        }
        const float frames = float(total_) / 4096.0f;
        if (curve_ == 0x21) {
            // a new weight 0..1 each frame
            weight_ = std::int32_t(draw() * 4096.0f);
        } else if (curve_ == 0x22) {
            // a random walk: up to one total's worth per frame either way
            const float w = float(std::int16_t(weight_)) / 4096.0f;
            const float step = (2.0f * draw() - 1.0f) / frames;
            weight_ = std::clamp(std::int32_t((w + step) * 4096.0f), 0, kOne);
        } else {
            // 0x23: a walk with momentum, turning back at the ends: the
            // velocity keeps (total - 1) parts of itself and gains a
            // random -1..1, per total (clamped to -1..1).
            const std::int64_t kept = (std::int64_t(total_ - kOne) * velocity_) / kOne;
            const std::int32_t sum =
                std::int32_t(kept) + std::int32_t((2.0f * draw() - 1.0f) * 4096.0f);
            velocity_ = total_ > 0 ? std::clamp(std::int32_t((std::int64_t(sum) << 12) / total_),
                                                -kOne, kOne)
                                   : 0;
            const std::int32_t step =
                total_ > 0 ? std::int32_t((std::int64_t(velocity_) << 12) / total_) : 0;
            std::int32_t w = std::int16_t(weight_) + step;
            if (w < 1 || w > 0xfff) {
                w = std::int16_t(weight_) - step; // bounces
                velocity_ = -velocity_;
            }
            weight_ = std::clamp(w, 0, kOne);
        }
        weight_ = std::int16_t(weight_);
        return false;
    }
    if (!finish && elapsed_ < total_) {
        progress_ = std::int32_t((std::int64_t(elapsed_) << 12) / total_);
    } else {
        progress_ = kOne;
        elapsed_ = total_;
        done = true;
    }
    const std::int32_t p = std::int16_t(progress_);
    switch (curve_) {
    case 0: // linear
        weight_ = p;
        break;
    case 1: // ease in
        weight_ = square(p);
        break;
    case 2: // ease out
        weight_ = kOne - square(kOne - p);
        break;
    case 3: // ease in and out
        weight_ = p <= 0x800 ? 2 * square(p) : kOne - 2 * square(kOne - p);
        break;
    case 4: // there and back: sin(pi p)
        weight_ = sine(std::int16_t(product(p, 0x800)) & 0xfff);
        break;
    case 5: // there and back: 4 p (1 - p)
        weight_ = 4 * std::int16_t(product(p, std::int16_t(kOne - p)));
        break;
    default: // other curves below 0x20 leave the weight as it was
        break;
    }
    weight_ = std::int16_t(weight_);
    return done;
}

// --------------------------------------------------------------- values

void TweenValue::set(std::int32_t v) {
    tween_.stop();
    changed_ = value_ != v;
    if (value_ != v) {
        value_ = previous_ = v;
    }
    target_ = start_ = v;
}

bool TweenValue::move(std::int32_t target, std::int32_t frames, int curve, bool lock) {
    if (tween_.begin(frames, curve, lock)) {
        start_ = value_;
        target_ = target;
        return true;
    }
    set(target);
    return false;
}

bool TweenValue::pulse(std::int32_t from, std::int32_t to, std::int32_t frames, bool lock) {
    // ov6 020ab930: from `from` to `to` and back every `frames` (curve 0x20).
    if (tween_.locked()) {
        return false;
    }
    tween_.stop();
    if (value_ != from) {
        value_ = previous_ = from;
    }
    start_ = target_ = from;
    if (!tween_.begin(frames, 0x20, lock)) {
        value_ = previous_ = start_ = target_ = to;
        return false;
    }
    start_ = value_;
    target_ = to;
    return true;
}

void TweenValue::update(std::int32_t elapsed, bool finish) {
    changed_ = false;
    const bool was = tween_.running() && tween_.curve() < 0x20;
    tween_.update(elapsed, finish, [&](std::int32_t w) {
        // ov7 020d3234
        const std::int32_t old = value_;
        value_ = start_ + product(target_ - start_, w);
        previous_ = old;
        changed_ = old != value_;
    });
    if (angle && was && !tween_.running()) {
        // 02083ea4: a finished turn ends within -pi..pi.
        const std::int32_t a = wrapAngle(value_);
        target_ = a;
        if (value_ != a) {
            value_ = previous_ = a;
        }
        start_ = a;
    }
}

std::int32_t wrapAngle(std::int32_t a) {
    if ((a < 0 ? -a : a) <= kPi) {
        return a;
    }
    // The DS divider's remainder (toward zero).
    const std::int32_t r = (a + kTwoPi / 2) % kTwoPi;
    return r < 0 ? r + kPi : r - kPi;
}

bool TweenValue::turn(std::int32_t target, std::int32_t frames, int curve, bool lock) {
    // 02083c04: from the current angle (wrapped) the short way: a start a
    // turn away when the target is more than pi off (the target itself is
    // not wrapped).
    const std::int32_t current = wrapAngle(value_);
    const std::int32_t d = target - current;
    const std::int32_t from =
        (d < 0 ? -d : d) > kPi ? (d < 0 ? current - kTwoPi : current + kTwoPi) : current;
    tween_.stop();
    target_ = start_ = from;
    if (value_ != from) {
        value_ = previous_ = from;
    }
    if (!tween_.begin(frames, curve, lock)) {
        tween_.stop();
        if (value_ != target) {
            value_ = previous_ = target;
        }
        target_ = start_ = target;
        return false;
    }
    start_ = value_;
    target_ = target;
    return true;
}

// --------------------------------------------------------------- points

void TweenPoint::set(Point p) {
    tween_.stop();
    if (value_ != p) {
        value_ = previous_ = p;
    }
    target_ = start_ = p;
}

bool TweenPoint::move(Point target, std::int32_t frames, int curve, bool lock) {
    if (tween_.begin(frames, curve, lock)) {
        start_ = value_;
        target_ = target;
        return true;
    }
    set(target);
    return false;
}

void TweenPoint::update(std::int32_t elapsed, bool finish) {
    tween_.update(elapsed, finish, [&](std::int32_t w) {
        // ov7 020d3100
        previous_ = value_;
        value_ = {start_[0] + product(target_[0] - start_[0], w),
                  start_[1] + product(target_[1] - start_[1], w)};
    });
}

std::array<int, 2> TweenPoint::cellAt(Point p) const {
    if (grid_.size <= 0) {
        return {};
    }
    std::array<int, 2> cell{};
    for (std::size_t k = 0; k < 2; ++k) {
        // Half a cell rounds to the nearest (divisions toward zero).
        const int px = std::int16_t((p[k] + 0x800) >> 12);
        const int v = std::int16_t(grid_.size / 2 + px - grid_.origin[k]) / grid_.size;
        cell[k] = std::min(std::max(v, 0), grid_.count[k]);
    }
    return cell;
}

bool TweenPoint::snap(Point p, int curve, bool lock) {
    if (grid_.size <= 0) {
        return false;
    }
    grid_.cell = cellAt(p);
    const Point target{(grid_.cell[0] * grid_.size + grid_.origin[0]) * kOne,
                       (grid_.cell[1] * grid_.size + grid_.origin[1]) * kOne};
    return move(target, grid_.frames, curve, lock);
}

bool TweenPoint::setCell(std::array<int, 2> cell) {
    if (grid_.size < 1) {
        grid_.cell = {};
        return false;
    }
    for (std::size_t k = 0; k < 2; ++k) {
        grid_.cell[k] = std::min(std::max(int(std::int16_t(cell[k])), 0), grid_.count[k]);
    }
    set({std::int16_t(grid_.cell[0] * grid_.size + grid_.origin[0]) * kOne,
         std::int16_t(grid_.cell[1] * grid_.size + grid_.origin[1]) * kOne});
    return true;
}

} // namespace picarat::adventure4
