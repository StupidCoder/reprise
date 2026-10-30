// The players: walking pictures and stances, the computer's movement
// toward its targets, and the side swaps that keep slot 1 of a side under
// control.
#include "beach_volley_amiga/match/match.hpp"

namespace beach_volley::match {
namespace {

constexpr Object slot_object[4] = {a1, a2, b1, b2};

} // namespace

// $2B41C (A1), $2B5E6 (A2), $2B786 (B1), $2B962 (B2): one routine per
// player in the original, identical but for their counters and B1 judging
// the ball from 16 pixels further right. A player walking shows the walk
// for his direction, a frame every third pass; standing, he faces the
// ball: reaching up when it is above him, crouched when its shadow is
// behind his feet.
int Match::animate(int slot) {
    auto& w = walk_[std::size_t(slot)];
    auto& p = objects_[slot_object[slot]];
    const std::uint8_t m = moving_[std::size_t(slot)];
    int dir = -1; // index into the walk tables
    if ((m & 3) == 3) dir = 2;
    else if ((m & 6) == 6) dir = 3;
    else if ((m & 9) == 9) dir = 4;
    else if ((m & 12) == 12) dir = 5;
    else if (m & 8) dir = 0;
    else if (m & 2) dir = 1;
    else if (m & 1) dir = 2;
    else if (m & 4) dir = 3;
    if (dir >= 0) {
        // The register keeps the direction bits tested, or the picture's
        // address once a frame is taken (both nonzero).
        if (--w.delay >= 0) return m;
        w.delay = 2;
        const auto& frames = data_->walk[std::size_t(dir)];
        auto& at = w.step[std::size_t(dir)];
        if (at >= int(frames.size())) at = 0;
        p.image = frames[std::size_t(at++)];
        d0_high_ = int((p.image.at + sprite_block_address) >> 16);
        return int(p.image.at + sprite_block_address);
    }
    w.delay = 0;
    const auto& ball_o = objects_[ball];
    const auto& shadow = objects_[ball_shadow];
    const auto& s = data_->stance;
    const int x = slot == 2 ? p.x + 16 : p.x;
    const int base = x < ball_o.x ? 0 : 2;
    p.image = {s[std::size_t(base)].at, 48, 59};
    if (p.y + 59 >= shadow.y) p.image.at = s[std::size_t(base + 4)].at;
    if (p.y - 60 < ball_o.y) {
        p.image.at = s[std::size_t(base + 1)].at;
        if (p.y + 59 >= shadow.y) p.image.at = s[std::size_t(base + 5)].at;
        return p.y + 59;
    }
    return p.y - 60;
}

// $25B72: A1 and A2 trade places in the object table, with their court x
// and shadows, so the one nearer the ball comes under control.
void Match::swap_side_a() {
    if (rally_over_) return;
    a_swapped_ = !a_swapped_;
    std::swap(objects_[a1], objects_[a2]);
    std::swap(court_x_[0], court_x_[1]);
    std::swap(objects_[a1_shadow].x, objects_[a2_shadow].x);
    std::swap(objects_[a1_shadow].y, objects_[a2_shadow].y);
}

// $25C18: the same for B1 and B2 (their pictures' sizes stay; the shadows'
// heights stay with the slots).
void Match::swap_side_b() {
    auto& p = objects_[b1];
    auto& q = objects_[b2];
    std::swap(p.x, q.x);
    std::swap(p.y, q.y);
    std::swap(p.image.width, q.image.width);
    std::swap(p.image.height, q.image.height);
    std::swap(court_x_[2], court_x_[3]);
    std::swap(court_y_[2], court_y_[3]);
    std::swap(p.image.at, q.image.at);
}

// $2F688: A2 (the computer partner) walks toward its target, 4 pixels a
// pass each way, inside his half.
void Match::move_a2() {
    moving_[1] = 0;
    if (a2_setting_ || a2_hopping_ || a2_jumping_) return;
    if (touch_ != 2 && !a2_follow_) return;
    moving_[1] = 0;
    auto& p = objects_[a2];
    const auto& mark = objects_[landing];
    if (a2_to_x_ >= a2_from_x_) {
        int target = touch_ == 2 ? mark.x : a2_to_x_;
        target -= 16;
        if (target >= p.x && court_x_[1] < (touch_ == 2 ? 250 : 180)) {
            p.x += 4;
            court_x_[1] += 4;
            moving_[1] |= 2;
        }
    } else {
        int target = touch_ == 2 ? mark.x : a2_to_x_;
        target -= 16;
        if (target < p.x && court_x_[1] >= 120) {
            p.x -= 4;
            court_x_[1] -= 4;
            moving_[1] |= 8;
        }
    }
    if (a2_to_y_ >= a2_from_y_) {
        if (a2_to_y_ >= p.y && p.y < (touch_ == 2 ? 136 : 120)) {
            p.y += 4;
            moving_[1] |= 4;
        }
    } else if (a2_to_y_ < p.y && p.y >= (touch_ == 2 ? 36 : 50)) {
        p.y -= 4;
        moving_[1] |= 1;
    }
}

// $2FA0A: B1 (computer) walks toward its target, its own speed along the
// court and 5 up.
void Match::move_b1() {
    const auto& act = b1_action_;
    if (act[5].on || act[4].on || act[1].on || act[3].on || b1_acting_ || b1_jumping_) return;
    if (touch_ != 3 && !b1_follow_) return;
    moving_[2] = 0;
    const int speed = b1_speed_;
    auto& p = objects_[b1];
    const auto& mark = objects_[landing];
    if (b1_to_x_ >= b1_from_x_) {
        const int target = touch_ == 3 ? mark.x : b1_to_x_;
        if (target >= p.x && court_x_[2] < (touch_ == 3 ? 450 : 390)) {
            p.x += speed;
            court_x_[2] += speed;
            moving_[2] |= 2;
        }
    } else {
        const int target = touch_ == 3 ? mark.x : b1_to_x_;
        if (target < p.x && std::uint16_t(court_x_[2] - 5) > 283 && std::uint16_t(court_x_[2]) > 286) {
            p.x -= 5;
            court_x_[2] -= 5;
            moving_[2] |= 8;
        }
    }
    if (b1_to_y_ >= b1_from_y_) {
        if (std::uint16_t(b1_to_y_) > std::uint16_t(p.y) && p.y < 127) {
            p.y += speed;
            moving_[2] |= 4;
        }
    } else if (b1_to_y_ < p.y && std::uint16_t(p.y) > 36) {
        p.y -= 5;
        moving_[2] |= 1;
    }
}

// $2F830: B2 walks toward its target, 4 pixels a pass.
void Match::move_b2() {
    moving_[3] = 0;
    if (players_count_ == 2) {
        if (b2_jumping_) return;
        if (touch_ != 4 && !b2_follow_) return;
    } else {
        if (b2_blocking_) return;
        if (touch_ != 4 && touch_ != 1 && !b2_follow_) return;
    }
    if (b2_setting_) return;
    auto& p = objects_[b2];
    const auto& mark = objects_[landing];
    if (b2_to_x_ >= b2_from_x_) {
        int target = touch_ == 4 ? mark.x : b2_to_x_;
        target -= 16;
        if (target >= p.x && court_x_[3] < 500) {
            p.x += 4;
            court_x_[3] += 4;
            moving_[3] |= 2;
        }
    } else if (mark.x < p.x) {
        int target = touch_ == 4 ? mark.x : b2_to_x_;
        target -= 8;
        const int limit = (touch_ == 4 || players_count_ == 1) ? 298 : 388;
        if (target < p.x && std::uint16_t(court_x_[3]) > limit) {
            p.x -= 4;
            court_x_[3] -= 4;
            moving_[3] |= 8;
        }
    }
    if (b2_to_y_ >= b2_from_y_) {
        if (b2_to_y_ >= p.y && p.y < 136) {
            p.y += 4;
            moving_[3] |= 4;
        }
    } else if (b2_to_y_ < p.y && p.y >= 36) {
        p.y -= 4;
        moving_[3] |= 1;
    }
}

} // namespace beach_volley::match

