// The ball: every touch decides a shot ($2950C), the shot sets the flight
// ($2A9AC, $2AA0C, $2FC82, $2AA96) and the flight runs each pass ($2AB6A)
// until the ball comes down at the landing mark. The camera follows the
// shot along an easing run ($28A0A).
#include "beach_volley_amiga/match/match.hpp"
#include <cstdlib>

namespace beach_volley::match {
namespace {

int abs16(int v) { return v < 0 ? -v : v; }

} // namespace

// The shot for the touch that just happened. Each branch fills the plan:
// travel (how far the camera eases), step (pixels a pass along the court),
// toward_minus_x, dy (the landing mark's move across the court) and the
// arc's height.
void Match::hit() {
    auto& p = plan_;
    const auto set = [&](int travel, int step, int minus_x, int dy) {
        p.travel = travel;
        p.step = step;
        p.minus_x = minus_x;
        p.dy = dy;
    };
    const int landing_y = objects_[landing].y;
    shot_kind_ = 0;

    if (rally_over_) {
        // The bounce after the ball came down.
        a_jump_contact_ = false;
        b_jump_contact_ = false;
        b1_may_jump_ = false;
        b2_follow_ = false;
        a2_follow_ = false;
        b1_follow_ = false;
        a2_from_x_ = a2_to_x_ = objects_[a2].x;
        b1_from_x_ = b1_to_x_ = objects_[b1].x;
        b1_from_y_ = b1_to_y_ = objects_[b1].y;
        b2_from_x_ = b2_to_x_ = objects_[b2].x;
        p.travel = 10;
        p.dy = 1;
        arc_height_ = 20;
        // divu #3 of a register whose high half another routine left: when
        // the quotient overflows the step stays, and ten or more becomes 1
        // (original behaviour, kept).
        const std::uint32_t dividend = std::uint32_t(d0_high_) << 16 | std::uint16_t(p.step);
        const std::uint32_t q = dividend / 3;
        int d0 = std::int16_t(q > 0xFFFF ? std::uint16_t(p.step) : std::uint16_t(q));
        if (d0 >= 10) d0 = 1;
        ball_step_ = d0;
        start_arc();
        start_shadow();
        start_ease();
        objects_[ball_shadow].y = objects_[ball].y + 17;
        return;
    }
    const auto early = [&](int travel, int step, int minus_x, int dy, int height, bool rising) {
        set(travel, step, minus_x, dy);
        arc_height_ = height;
        if (rising) falling_ = false;
        aim();
        start_arc();
        start_shadow();
        start_ease();
    };
    if (b2_set_) {
        early(100, 2, 1, 1, 10, true);
        b2_set_ = false;
        return;
    }
    if (b1_bump_) {
        early(100, 2, 1, 1, 10, false);
        b1_bump_ = false;
        return;
    }
    if (a1_bump_) {
        early(100, 2, 0, 1, 20, true);
        a1_bump_ = false;
        return;
    }

    bool to_finish = true;
    Then then = Then::finish;
    if (b_serve_hit_) {
        if (players_count_ == 1) {
            set(200, 6, 1, 1);
            arc_height_ = 40;
            if (chance_ != 4 && chance_ == 2) p.step = 5;
            if (chance_ == 4 || chance_ == 2) p.dy = 1;
        } else {
            const auto b = sticks_.b;
            if (b & 2) set(120, 4, 1, 1);
            else if (b & 8) set(200, 5, 1, 1);
            else set(170, 5, 1, 1);
            arc_height_ = 60;
            if (swing_started_) {
                shot_kind_ = 0;
                set(170, 7, 1, 1);
                arc_height_ = 16;
                if (b & 8) {
                    set(200, 9, 1, 1);
                    shot_kind_ = 8;
                }
                if (b & 2) {
                    set(170, 7, 1, 1);
                    shot_kind_ = 2;
                }
            }
        }
        if (!swing_started_) p.dy = (190 - landing_y) - (landing_y - 84);
    } else if (b_jump_contact_) {
        if (players_count_ == 1) {
            set(150, 10, 1, 1);
            arc_height_ = 1;
            falling_ = true;
            shot_kind_ = 0;
            bool skip_to_spike = false;
            if (deep_ || chance_ == 3) {
                set(150, 7, 1, 1);
                arc_height_ = 10;
                shot_kind_ = 8;
                falling_ = false;
                skip_to_spike = deep_;
            }
            if (!skip_to_spike) {
                if (a1_bumping_ && (chance_ == 2 || chance_ == 1)) {
                    set(50, 3, 1, 1);
                    arc_height_ = 10;
                    falling_ = false;
                    shot_kind_ = 2;
                    finish_hit();
                    return;
                }
                if (chance_ == 5) {
                    set(50, 5, 1, 1);
                    arc_height_ = 10;
                    falling_ = false;
                    if (objects_[landing].y < 130) {
                        shot_kind_ = 4;
                        p.dy = 10;
                        then = Then::down_low;
                    } else {
                        shot_kind_ = 1;
                        p.dy = -10;
                        then = Then::down_high;
                    }
                    to_finish = false;
                }
            }
            if (to_finish && chance_ == 4) {
                shot_kind_ = 16;
                set(150, 20, 1, -10);
                arc_height_ = 1;
                falling_ = true;
            }
        } else {
            jump_shot(sticks_.b, 1, true, then);
        }
    } else if (a_jump_contact_) {
        jump_shot(sticks_.a, 0, false, then);
    } else if (serving_a_) {
        fire_latch_a_ = false;
        play(MatchSound::Effect::serve); // $29E88
        const auto a = sticks_.a;
        if (a & 2) set(120, 4, 0, 1);
        else if (a & 8) set(200, 5, 0, 1);
        else set(170, 5, 0, 1);
        arc_height_ = 60;
        if (swing_started_) {
            shot_kind_ = 0;
            set(170, 7, 0, 1);
            arc_height_ = 16;
            if (a & 8) {
                set(200, 9, 0, 1);
                arc_height_ = 16;
                shot_kind_ = 8;
            }
        }
        if (!swing_started_) p.dy = (190 - landing_y) - (landing_y - 84);
    } else {
        touch_shot();
        return;
    }
    if (then == Then::down_low) {
        if (objects_[landing].y < 130) p.dy = 50;
        else p.dy = objects_[landing].y >= 170 ? 10 : 30;
    } else if (then == Then::down_high) {
        if (objects_[landing].y >= 130) p.dy = -50;
        else p.dy = objects_[landing].y < 96 ? -10 : -30;
    }
    finish_hit();
}

// A jump's shot by the joystick held at the contact ($29C66 side A,
// $29A70 side B in a two-player match). Exact values only: a diagonal not
// listed keeps the hard downward shot.
void Match::jump_shot(std::uint8_t stick, int minus_x, bool clear_falling, Then& then) {
    auto& p = plan_;
    const auto set = [&](int travel, int step, int dy) {
        p.travel = travel;
        p.step = step;
        p.minus_x = minus_x;
        if (dy != 0) p.dy = dy;
    };
    b1_may_jump_ = false;
    p.dy = 1;
    set(150, 10, 1);
    arc_height_ = 10;
    falling_ = true;
    shot_kind_ = 0;
    switch (stick) {
    case 2:
        set(50, 3, 1);
        arc_height_ = 10;
        shot_kind_ = 2;
        falling_ = false;
        break;
    case 8:
        set(150, 7, 1);
        arc_height_ = 10;
        shot_kind_ = 8;
        if (clear_falling) falling_ = false;
        break;
    case 3:
        set(50, 5, 0);
        arc_height_ = 10;
        shot_kind_ = 3;
        if (clear_falling) falling_ = false;
        then = Then::down_high;
        break;
    case 6:
        set(50, 5, 10);
        arc_height_ = 20;
        shot_kind_ = 6;
        if (clear_falling) falling_ = false;
        then = Then::down_low;
        break;
    case 4:
    case 12:
        set(50, 5, 10);
        arc_height_ = 10;
        shot_kind_ = 4;
        if (clear_falling) falling_ = false;
        then = Then::down_low;
        break;
    case 1:
    case 9:
        set(50, 5, 1);
        arc_height_ = 10;
        shot_kind_ = 1;
        if (clear_falling) falling_ = false;
        then = Then::down_high;
        break;
    default: break;
    }
}

} // namespace beach_volley::match

namespace beach_volley::match {

// $29FB8: a set or a pass, by the touch phase and where the players stand.
void Match::touch_shot() {
    auto& p = plan_;
    const auto set = [&](int travel, int step, int minus_x) {
        p.travel = travel;
        p.step = step;
        p.minus_x = minus_x;
    };
    // $2AA62: the landing mark moves across by the gap between two shadows.
    const auto across = [&](Object from, Object to) {
        p.dy = objects_[to].y - objects_[from].y;
        if (p.dy == 0) p.dy = 1;
    };
    arc_height_ = 40;
    const int ly = objects_[landing].y;
    const auto settle_high = [&]() { // $2A5B2, $2A7F6
        p.dy = ly >= 140 ? (180 - ly) - (ly - 135) : (140 - ly) - (ly - 90);
    };
    if (touch_ == 2) {
        deep_ = false;
        const int a2x = court_x_[1];
        const auto by_a2 = [&]() {
            if (a2x < 110) set(50, 2, 0);
            else if (a2x < 145) set(50, 15, 0);
            else if (a2x < 160) set(50, 1, 0);
            else set(0, 50, 0);
        };
        if (court_x_[0] < 210) {
            deep_ = true;
            if (court_x_[0] >= a2x || a2x < 240) by_a2();
            else set(20, 1, 1); // A2 at 240 or beyond (the 0 / 50 case for below 239 cannot happen)
        } else {
            if (a2x >= 245) set(0, 0, 0);
            else if (a2x >= 210) set(0, 50, 0);
            else if (a2x >= 160) set(60, 15, 0);
            else set(200, 2, 0);
        }
        across(a2_shadow, a1_shadow);
        // $2FC2C: the arc by how far apart the two stand.
        const int gap = abs16(objects_[a2_shadow].y - objects_[a1_shadow].y);
        arc_height_ = gap < 10 ? 36 : gap < 30 ? 38 : 42;
        if (deep_) arc_height_ += 2;
        if (!deep_ && objects_[net].x >= 160) p.travel = objects_[net].x - 160;
    } else if (touch_ == 4) {
        deep_ = false;
        swing_started_ = false;
        const int b2x = court_x_[3];
        if (court_x_[2] >= 338) {
            deep_ = true;
            if (b2x < 317) set(0, 50, 0);
            else if (b2x < 338) set(20, 50, 0);
            else if (b2x < 360) set(10, 50, 1);
            else if (b2x < 390) set(20, 1, 1);
            else if (b2x < 430) set(50, 15, 1);
            else if (b2x < 420) set(100, 2, 1);
            else set(100, 3, 1);
        } else {
            if (b2x < 330) set(0, 0, 1);
            else if (b2x < 370) set(10, 1, 1);
            else if (b2x < 420) set(50, 15, 1);
            else set(50, 2, 1);
        }
        across(b2_shadow, b1_shadow);
        arc_height_ = 42;
        b_shot_chance();
    } else if (touch_ == 3) {
        a2_follow_ = false;
        serve_contact_ = false;
        b_turn_ = false;
        swing_started_ = false;
        if (!b_attack_) {
            const int b1x = court_x_[2];
            if (b1x < 350) {
                set(10, 50, 0);
                p.dy = 40;
                arc_height_ = 50;
            } else if (b1x < 400) {
                set(20, 15, 1);
                p.dy = 40;
            } else if (b1x < 420) {
                set(40, 2, 1);
                p.dy = 40;
            } else {
                set(10, 2, 1);
                p.dy = 40;
            }
            // $2A618
            p.dy = objects_[b2_shadow].y - objects_[b1_shadow].y;
            if (p.dy == 0) p.dy = 1;
            if (std::uint16_t(objects_[landing].y) > 180) p.dy = -40;
            else if (objects_[landing].y < 95) p.dy = 40;
        } else {
            a_attack_ = false;
            if (court_x_[2] < 320) set(150, 3, 1);
            else set(200, 4, 1);
            p.dy = 40;
            settle_high();
        }
    } else if (touch_ == 1) {
        objects_[landing].y = objects_[a1_shadow].y;
        const int y = objects_[landing].y;
        b_serve_rally_ = false;
        swing_started_ = false;
        const int a1x = court_x_[0];
        bool high = false;
        if (!a_attack_) {
            if (a1x >= 230) { set(50, 1, 1); p.dy = -40; }
            else if (a1x >= 200) { set(10, 50, 0); p.dy = -40; }
            else if (a1x >= 150) { set(50, 1, 0); p.dy = 40; }
            else { set(50, 2, 0); p.dy = 10; }
        } else {
            high = true;
            if (a1x >= 210) { set(150, 3, 0); p.dy = -50; }
            else { set(200, 4, 0); p.dy = 50; }
        }
        if (!high) {
            arc_height_ = 50;
            p.dy = y < 90 ? 40 : y >= 170 ? -40 : 1;
        } else {
            arc_height_ = 40;
            p.dy = y >= 140 ? (180 - y) - (y - 135) : (140 - y) - (y - 90);
        }
    }
    finish_hit();
}

// $2A8B0: in a one-player match B1 may go up for the next ball unless the
// chance value says 1.
void Match::b_shot_chance() {
    if (players_count_ != 2 && chance_ != 1) {
        b1_may_jump_ = true;
        b1_jump_at_ = 0;
    }
}

// $2A86A.
void Match::finish_hit() {
    objects_[ball_shadow].y = objects_[landing].y + 8; // $2A896
    next_touch();
    start_arc();
    start_shadow();
    aim();
    start_ease();
    hit_pending_ = false;
}

// $2A974: the touch phase steps through the rally's order.
void Match::next_touch() {
    const auto& order = data_->touch_order;
    if (touch_order_at_ >= int(order.size())) touch_order_at_ = 0;
    touch_ = order[std::size_t(touch_order_at_)];
    if (!rally_over_) ++touch_order_at_;
}

// $2A9AC: the arc starts deeper in the table for a shot that moves the
// landing mark toward the viewer.
void Match::start_arc() {
    int depth = arc_height_;
    if (plan_.dy < 0) depth += (-plan_.dy >> 3) << 1;
    arc_depth_ = depth;
    arc_left_ = depth;
    arc_at_ = 64 - ((depth + 1) >> 1); // ($2AF38 - depth) & ~1, as a word index
    landing_y_at_hit_ = objects_[landing].y;
    shadow_target_ = objects_[landing].y + plan_.dy;
}

// $2AA0C: the shadow's pace toward its target.
void Match::start_shadow() {
    int dy = plan_.dy;
    shadow_dir_ = 1;
    if (dy < 0) {
        dy = -dy;
        shadow_dir_ = -1;
    }
    if (dy == 0) dy = 1;
    int rate = (arc_depth_ & 0xFFFF) / dy;
    if (a_jump_contact_) rate = 0;
    shadow_rate_ = rate + 1;
    shadow_count_ = 0;
}

// $2AA96: the camera's easing run for the shot's travel.
void Match::start_ease() {
    const auto& t = data_->ease_thresholds;
    std::size_t k = 0;
    while (k < t.size() && plan_.travel >= t[k]) ++k;
    ease_run_ = int(k);
    ease_at_ = 0;
}

} // namespace beach_volley::match

namespace beach_volley::match {

// $2FC82: where the ball will come down (the landing mark), and who goes
// for it: each side's AI targets, and the human side's player under
// control becomes the one nearer the mark.
void Match::aim() {
    auto& p = plan_;
    ball_step_ = p.step;
    ball_minus_x_ = p.minus_x != 0;
    auto& mark = objects_[landing];
    mark.y += p.dy;
    b2_follow_ = false;

    // The code reads the plan through a register that some branches below
    // point at a player instead; a branch that matches no shot kind then
    // reads the player's y as the step and his picture address's low word
    // as dy (original behaviour, kept).
    int step = p.step, dy = p.dy;
    const auto through = [&](Object o) {
        step = objects_[o].y;
        dy = std::int16_t((objects_[o].image.at + sprite_block_address) & 0xFFFF);
    };
    bool placed = false;
    if (b2_set_) {
        mark.x = objects_[b2].x - 70;
        mark.y = objects_[b2_shadow].y;
        placed = true;
    } else if (b1_bump_) {
        mark.x = objects_[b1].x - 70;
        mark.y = objects_[b1_shadow].y;
        placed = true;
    } else if (a1_bump_) {
        mark.x = objects_[a1].x + 90;
        mark.y = objects_[a1_shadow].y;
        placed = true;
    } else {
        const auto by_kind = [&](const int (&shift)[8]) {
            const int kinds[8] = {16, 0, 2, 8, 3, 6, 4, 1};
            for (int k = 0; k < 8; ++k)
                if (shot_kind_ == kinds[k] && shift[k] != 0) {
                    mark.x += shift[k];
                    return true;
                }
            return false;
        };
        if (b_jump_contact_) {
            through(b1);
            placed = by_kind({-200, -110, -120, -185, -100, -110, -110, -100});
        }
        if (!placed && a_jump_contact_) {
            through(a1);
            placed = by_kind({0, 110, 100, 185, 100, 110, 110, 90});
        }
    }
    if (!placed) {
        int d0;
        bool plain = false;
        if (step == 50) d0 = arc_count(1);
        else if (step == 15) d0 = arc_count(2);
        else {
            d0 = std::int16_t(std::uint16_t(arc_height_ * std::uint16_t(step)));
            plain = !((touch_ == 3 && b_attack_) || (touch_ == 1 && a_attack_));
            if (plain) {
                const int ady = abs16(dy);
                std::uint32_t d2 = std::uint32_t(std::uint16_t(step)) * std::uint32_t(std::uint16_t(ady));
                d2 = std::uint16_t(std::uint16_t(step) > 4 ? d2 / 6 : d2 / 4);
                d0 = std::int16_t(d0 + int(d2));
                if (step >= 4 && ady < 40) d0 = std::int16_t(d0 + step * 4);
            }
        }
        if (ball_minus_x_) d0 = -d0;
        mark.x = objects_[ball].x + d0;
        if (touch_ == 1 && a_attack_) mark.x -= 10;
        else if (b_serve_rally_) mark.x -= 35;
        else if (serving_a_) mark.x += 10;
    }
    if (swing_started_) {
        const int add = (shot_kind_ & 2) ? 100 : (shot_kind_ & 8) ? 150 : 120;
        mark.x += touch_ == 3 ? add : -add;
    }

    b1_follow_ = false;
    if (touch_ == 2) {
        a2_from_x_ = objects_[a2].x;
        a2_from_y_ = objects_[a2].y;
        a2_to_x_ = mark.x;
        a2_to_y_ = mark.y - 50;
    }
    if (touch_ == 3) {
        b1_from_x_ = objects_[b1].x;
        b1_from_y_ = objects_[b1].y;
        b1_to_x_ = mark.x;
        b1_to_y_ = mark.y - 50;
    }
    b2_from_x_ = objects_[b2].x;
    b2_from_y_ = objects_[b2].y;
    b2_to_x_ = mark.x;
    b2_to_y_ = mark.y - 50;
    if (touch_ != 3) b1_next_height();

    // Side A: the nearer player takes over ($30174).
    const bool kind_5 = (shot_kind_ & 5) != 0;
    const bool consider = b_serve_hit_ || (!rally_over_ && !a_jumping_ &&
                                           (kind_5 || (!a1_bumping_ && touch_ == 1 && !a_attack_ && !fire_latch_a_)));
    if (consider) {
        bool swap = true;
        if (!a1_bumping_) {
            const auto [d1, d2] = nearness(a1, a2);
            if (std::int16_t(std::uint16_t(d2 - d1)) >= 0 || a2_hopping_) swap = false;
        }
        if (swap) {
            swap_side_a();
            fire_latch_a_ = false;
            if (a1_bumping_) {
                a2_hopping_ = true;
                a1_bumping_ = false;
            }
        }
    }
    // Side B in a two-player match ($301C4).
    if (players_count_ == 2) {
        const bool consider_b =
            serving_a_ || (!b1_jumping_ && (kind_5 || (!b1_bumping_ && touch_ == 3 && !b_attack_)));
        if (consider_b) {
            bool swap = true;
            if (!b1_bumping_) {
                const auto [d1, d2] = nearness(b1, b2);
                if (std::int16_t(std::uint16_t(d2 - d1)) >= 0) swap = false;
            }
            if (swap) {
                fire_latch_b_ = false;
                swap_side_b();
                if (b1_bumping_) {
                    b2_hopping_ = true;
                    b1_bumping_ = false;
                }
            }
        }
    }
}

// $302D8: an arc's length counted in halves, `per` a frame.
int Match::arc_count(int per) const {
    int d0 = 0;
    bool odd = false;
    for (int n = 0; n <= arc_height_; ++n) {
        d0 += per;
        if (!odd) d0 -= 1;
        odd = !odd;
    }
    return d0;
}

// $30272: each player's squared distance to the landing mark, feet to
// mark, in 16 bits as the program adds them.
std::pair<int, int> Match::nearness(Object first, Object second) const {
    const auto& mark = objects_[landing];
    const auto d = [&](Object o) {
        const auto& s = objects_[o];
        const int dx = abs16(mark.x - s.x);
        const int dy = abs16(s.y + s.image.height - mark.y);
        return std::uint16_t(std::uint16_t(dx * dx) + std::uint16_t(dy * dy));
    };
    return {d(first), d(second)};
}

// $2FBBA: B1's next height when he is not the one going for the ball.
void Match::b1_next_height() {
    if (b1_jumping_) return;
    const auto& t = data_->setter_targets;
    if (b1_height_at_ >= int(t.size())) b1_height_at_ = 0;
    b1_from_y_ = objects_[b1].y;
    b1_to_y_ = t[std::size_t(b1_height_at_++)];
    b1_follow_ = true;
}

} // namespace beach_volley::match

namespace beach_volley::match {

// $2AB6A: one pass of the ball's flight.
void Match::fly() {
    lift_for_ball();
    auto& b = objects_[ball];
    auto& shadow = objects_[ball_shadow];
    if (shot_kind_ == 16) {
        b.image = {0x53E16 - sprite_block_address, 48, b.image.height};
    } else {
        spin_ball();
    }
    shadow.x = b.x;
    bool move = false;
    if (shot_kind_ == 6 || shot_kind_ == 4) {
        shadow_rate_ = 1;
        shadow_dir_ = 4;
        move = true;
    } else if (shot_kind_ == 3 || shot_kind_ == 1) {
        shadow_dir_ = -4;
        shadow_rate_ = 1;
        move = true;
    } else if (++shadow_count_ == shadow_rate_) {
        shadow_count_ = 0;
        move = true;
    }
    if (move) {
        // Toward the target; a step down the screen that passes it takes a
        // second step on (original behaviour, kept).
        bool go = true;
        if (shadow_dir_ >= 0) {
            if (shadow_target_ < shadow.y) go = false;
            else shadow.y += shadow_dir_;
        }
        if (go && shadow_target_ < shadow.y) shadow.y += shadow_dir_;
    }
    if (!ball_in_play_) return;
    step_ball_along();
    // After a jump the ball goes straight down by the shot's kind; kind 2
    // (the lob) keeps the arc.
    int down = -1;
    if (a_jump_contact_ || b_jump_contact_) {
        switch (shot_kind_) {
        case 16: case 0: case 8: down = 15; break;
        case 2: break;
        case 3: case 1: down = 8; break;
        case 6: case 4: down = 12; break;
        default: down = 0; break;
        }
    }
    if (down >= 0) {
        b.y += down;
    } else if (!arc_step()) {
        return; // still rising
    }
    // $2AD84: down at the mark?
    if (b.y + 12 < objects_[landing].y || rally_over_) return;
    clock_running_ = false;
    // $2AE7E: the mark where it came down.
    objects_[ball_mark].x = b.x;
    objects_[ball_mark].y = b.y;
    objects_[ball_mark].image.at = 0x5D524 - sprite_block_address;
    show_ball_mark_ = true;
    rally_over_ = true;
    rally_over_count_ = 26;
    falling_ = false;
    play(MatchSound::Effect::landing); // $2ADCE
    hit();
    if (ball_court_x_ < 65) rally_winner_ = 3;
    else if (ball_court_x_ >= 484) rally_winner_ = touch_ == 4 ? 3 : 1;
    else if (b.y < 70 || b.y >= 176) rally_winner_ = touch_ == 1 ? 3 : 1;
}

// $2AD2E: the arc, up the table then back down it. False while rising.
bool Match::arc_step() {
    auto& b = objects_[ball];
    int d0 = data_->arc[std::size_t(arc_at_)];
    if (!falling_) d0 = -d0;
    b.y += d0;
    if (!falling_) {
        ++arc_at_;
        arc_left_ -= 2;
        if (arc_left_ < 0) falling_ = true;
        return false;
    }
    --arc_at_;
    arc_left_ += 2;
    return true;
}

// $2AF98: the ball along the court. Near the net on the attacking side it
// stops moving along while the attack is on.
void Match::step_ball_along() {
    if (touch_ == 3 && b_attack_ && ball_court_x_ < 308) return;
    if (touch_ == 4 && ball_court_x_ < 308) return;
    if (!ball_minus_x_) {
        if (touch_ == 1 && ball_court_x_ >= 255) return;
        if (touch_ == 2 && ball_court_x_ >= 250) return;
    }
    int d0;
    if (a_jump_contact_ || b_jump_contact_) {
        switch (shot_kind_) {
        case 16: d0 = 20; break;
        case 0: d0 = 12; break;
        case 8: d0 = 21; break;
        case 3: case 6: case 4: case 1: d0 = 8; break;
        default: d0 = pace(); break; // kind 2: by the plan
        }
    } else {
        d0 = pace();
    }
    if (ball_minus_x_) d0 = -d0;
    objects_[ball].x += d0;
    ball_court_x_ += d0;
}

// $2B0C2: the step along the court; the steps 50, 15, 25 and 35 stand for
// alternating paces (1/0, 2/1, 3/2, 4/3 pixels).
int Match::pace() {
    int base = 0;
    switch (ball_step_) {
    case 50: base = 1; break;
    case 15: base = 2; break;
    case 25: base = 3; break;
    case 35: base = 4; break;
    default: return ball_step_;
    }
    const int d0 = alternate_ ? base : base - 1;
    alternate_ = !alternate_;
    return d0;
}

// $2AF44: while the ball is above the screen the scene is drawn lower, 4
// rows a pass up to 32 (not while side A's setter or side B's setter has
// it, phases 2 and 4).
void Match::lift_for_ball() {
    if (touch_ != 2 && touch_ != 4 && objects_[ball].y < 0) {
        if (lift_ != 32) lift_ += 4;
    } else if (lift_ != 0) {
        lift_ -= 4;
    }
}

// $28A0A: the camera eases along the run the shot chose; at its end it
// creeps 4 pixels a pass toward an attack at the net.
void Match::follow_camera() {
    const auto& run = data_->ease[std::size_t(ease_run_)];
    int d0;
    if (ease_at_ >= int(run.size())) {
        d0 = 0;
        if (!rally_over_) {
            if (touch_ == 3 && b_attack_ && objects_[net].x < 160) d0 = ball_minus_x_ ? 4 : -4;
            if (touch_ == 1 && a_attack_ && std::uint16_t(objects_[net].x) > 160) d0 = ball_minus_x_ ? -4 : 4;
        }
        --ease_at_;
    } else {
        d0 = run[std::size_t(ease_at_)];
    }
    if (ball_minus_x_) d0 = -d0;
    ++ease_at_;
    if (d0 < 0) {
        if (camera_bytes_ >= 0) {
            shift_scene(d0);
            const int d1 = camera_shift_ - d0 - 16;
            register_d1_ = d1;
            camera_shift_ -= d0;
            if (camera_shift_ >= 16) {
                camera_shift_ = d1 >= 0 ? d1 : 0;
                camera_bytes_ -= 2;
            }
        }
    } else if (camera_bytes_ < 34) {
        shift_scene(d0);
        int d1 = camera_shift_ - d0;
        camera_shift_ = d1;
        if (d1 < 0) {
            camera_shift_ = 15;
            d1 = -d1 - 1;
            camera_shift_ -= d1;
            camera_bytes_ += 2;
        }
        register_d1_ = d1;
    }
    draw_court();
}

// $30B1C: the camera moved; everything placed on the court moves the other
// way on the screen.
void Match::shift_scene(int d0) {
    for (const Object o : {a1, a2, b1, b2, ball, net, setter_mark, landing, ball_mark}) objects_[o].x -= d0;
}

} // namespace beach_volley::match
