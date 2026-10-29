#include "wizball_c64/game/catellite.hpp"

namespace wizball::game {
namespace {
// $8708/$870E: a pull limited to hi..lo the way the game does it, by the
// sign of the subtraction rather than a signed compare (so $80 becomes hi).
std::int8_t limit(std::uint8_t a, std::uint8_t hi, std::uint8_t lo) {
    if (!(std::uint8_t(a - hi) & 0x80)) a = hi;
    if (std::uint8_t(a - lo) & 0x80) a = lo;
    return std::int8_t(a);
}

// CLC, ADC a, ADC b: the carry of the first add goes into the second.
std::uint8_t add_chained(std::uint8_t v, std::int8_t a, std::int8_t b) {
    const int first = v + std::uint8_t(a);
    return std::uint8_t((first & 0xFF) + std::uint8_t(b) + (first >> 8));
}

// Towards 0 by one ($8696).
std::int8_t decay(std::int8_t v) { return v > 0 ? std::int8_t(v - 1) : v < 0 ? std::int8_t(v + 1) : v; }
} // namespace

void Catellite::lose() {
    x_ = y_ = 0;
    sprite_.y = 0;
    shield_ = 0;
}

void Catellite::clear_for_life() {
    x_ = y_ = 0;
    stick_dx_ = stick_dy_ = vx_ = vy_ = 0;
    anim_clock_ = anim_frame_ = offset_ = steer_clock_ = hits_ = 0;
    following_ = false;
    sprite_.x = sprite_.y = 0;
}

std::uint8_t Catellite::wander_stick(RandomSource& random, bool& beep) {
    beep = false;
    if ((++wander_clock_ & 0x0F) == 0) {
        wander_value_ = random.next();
        beep = (wander_value_ & 0x10) != 0;
    }
    return std::uint8_t(wander_value_ | 0x10); // it never fires by itself
}

bool Catellite::shield_step() {
    // $895B
    if (shield_ == 0) return false;
    if (++shield_clock_ == 0x32) {
        shield_clock_ = 0;
        --shield_;
    }
    bool warn = false;
    if (shield_warned_ == 0 && std::int8_t(shield_ - 8) < 0) {
        warn = true;
        ++shield_warned_;
    }
    if (std::int8_t(shield_ - 5) < 0) shield_ = 0;
    return warn;
}

bool Catellite::hit() {
    // $8998
    if (shield_ != 0) {
        shield_ = std::uint8_t(shield_ - 8);
        return false;
    }
    ++hits_;
    return !(hits_ & 0x80);
}

void Catellite::follow(const Wiz& wiz) {
    // $86ED: the trail entry eight passes back, a little below the wiz and
    // to the side it last moved away from.
    stick_dx_ = stick_dy_ = 0;
    const int i = (wiz.probe().trail_index + 1) & 7;
    vy_ = limit(std::uint8_t(wiz.trail_y()[std::size_t(i)] + 3 - y_), 4, 0xFC);
    if (wiz.last_horizontal() >= 0) {
        if (offset_ != 0) --offset_;
    } else if (offset_ != 0x15) {
        ++offset_;
    }
    vx_ = limit(std::uint8_t(wiz.trail_x()[std::size_t(i)] - 8 + offset_ - x_), 2, 0xFE);
}

void Catellite::move(Stick stick, const Wiz& wiz, bool bonus) {
    // $8673
    following_ = true;
    bool steer = steer_clock_ != 0;
    if (!steer) {
        if (!bonus && (stick.bits & 0x0F) == 0x0F) {
            follow(wiz);
        } else {
            steer_clock_ = 0x0A;
            steer = true;
        }
    }
    if (steer) {
        // $8690: the stick moves it directly; the pull dies away.
        --steer_clock_;
        following_ = false;
        vx_ = decay(vx_);
        vy_ = decay(vy_);
        stick_dx_ = stick_dy_ = 0;
        if (stick.up()) stick_dy_ = -4;
        if (stick.down()) stick_dy_ = 3;
        if (stick.left()) stick_dx_ = -3;
        if (stick.right()) stick_dx_ = 2;
    }
    // $874E
    x_ = add_chained(x_, stick_dx_, vx_);
    y_ = add_chained(y_, stick_dy_, vy_);
    if (x_ >= 0xFA) x_ = 0;
    if (!wiz.appearing() && x_ < 0x0F) x_ = 0x0F;
    if (x_ >= 0xA0) x_ = 0xA0;
    if (!bonus) {
        if (y_ < 0x47) y_ = 0x47;
        if (y_ >= 0xC8) y_ = 0xC7;
    }
    sprite_.y = y_;
    sprite_.x = x_ << 1;
}

void Catellite::animate(const std::array<std::uint8_t, 8>& wander_colours) {
    // $87CD: three frames, four passes each.
    if ((++anim_clock_ & 3) == 0 && ++anim_frame_ == 3) anim_frame_ = 0;
    sprite_.pointer = std::uint8_t(0x0D + anim_frame_);
    sprite_.colour = 5;
    if (hits_ == 8) sprite_.colour = wander_colours[std::size_t((++colour_clock_ >> 2) & 7)];
    // $880A: it blinks black while its shield is up.
    if (shield_ != 0 && !(++shield_blink_ & 2)) sprite_.colour = 0;
}

} // namespace wizball::game
