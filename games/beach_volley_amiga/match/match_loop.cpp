// The rally: one pass of the program's main loop ($2526A), side B's serve
// in a one-player match ($27ACC), the computer partners' jumps, hops and
// sets, and the drawing order of a frame.
#include "beach_volley_amiga/match/match.hpp"
#include <algorithm>

namespace beach_volley::match {
namespace {

Image picture_at(std::uint32_t address, int width, int height) {
    return {address - sprite_block_address, width, height};
}

} // namespace

void Match::rally_step() {
    chance_ = chance_before_;
    clock_running_ = true;
    follow_camera();
    if (!serving_a_) {
        if (a1_falling_) {
            a1_fall();
        } else {
            move_a1();
            if (!fire_latch_a_ && !a_jumping_ && !a1_acting_ && !a1_bumping_) animate(0);
            a1_bump_step();
            a1_bump_step();
            a1_actions();
            a1_jump();
        }
    } else if (--serve_follow_ == 0) {
        serving_a_ = false;
        serve_follow_ = 10;
        swap_side_a();
        auto& p = objects_[a2];
        a2_from_x_ = p.x;
        a2_to_x_ = p.x + 50;
        p.y = 70;
        a2_follow_ = true;
    }
    a2_jump();
    a2_hop();
    a2_hop();
    move_a2();
    if (!a2_jumping_ && !a2_hopping_ && !a2_setting_) animate(1);
    if (!rally_over_) a2_set();
    if (players_count_ == 1) {
        if (!b_serve_hit_) {
            move_b1();
            int d0 = b1_register_;
            const bool still = b_fire_latch_ || b1_jumping_ || b1_acting_ || b1_action_[5].on ||
                               b1_action_[4].on || b1_action_[1].on || b1_action_[3].on || b1_bumping_;
            if (!still) d0 = animate(2);
            b1_actions(d0);
            b1_jump();
        }
        if (b_serve_hit_ && --b_serve_follow_ == 0) {
            b_serve_hit_ = false;
            b_serve_follow_ = 10;
            b1_to_x_ -= 100;
            b1_follow_ = true;
        }
        b2_jump();
        b2_block();
        move_b2();
        b2_set();
        if (!b2_hopping_ && !b2_jumping_ && !b2_blocking_ && !b2_setting_) animate(3);
    }
    objects_[clock].x = 18;
    objects_[clock].y = 166 - lift_;
    chance_ = chance_after_;
    draw_frame();
    if (time_up_) {
        score_b_ = 7;
        phase_ = Phase::match_over;
        return;
    }
    if (rally_over_) {
        if (rally_winner_ != 0 && rally_over_count_ == 26) sound_ = Sound::whistle;
        if (show_ball_mark_) {
            if (rally_over_count_ < 26) objects_[ball_mark].image.at = 0x5D528 - sprite_block_address;
            if (rally_over_count_ < 23) show_ball_mark_ = false;
            draw(ball_mark);
        }
        if (--rally_over_count_ == 0) {
            point_over();
            return;
        }
    }
    if (rally_over_count_ == 17) sound_ = Sound::whistle;
    draw(clock);
    // $31D24 marks the current minute used; $31D7C turns the hand.
    ring_plotted_ = std::max(ring_plotted_, clock_dots_ / 2 + 1);
    objects_[clock_dots].x = objects_[clock].x;
    objects_[clock_dots].y = objects_[clock].y;
    draw(clock_dots);
    const int positions = int(data_->clock_positions.size());
    for (auto& h : hand_at_)
        if (++h >= positions) h = 0;
    fly();
}

// $26A92, $26572, $266A8, $2683C: the frame's objects back to front.
void Match::draw_frame() {
    // The landing mark blinks while side A (or a human side B) receives.
    if ((players_count_ == 1 && touch_ == 1) || (players_count_ == 2 && (touch_ == 1 || touch_ == 3))) {
        landing_blink_ = !landing_blink_;
        if (landing_blink_) {
            objects_[landing].image = {0x53E08 - sprite_block_address, 48, 16};
            draw(landing);
        }
    }
    draw(ball_shadow);
    // Shadows at the players' feet, unless they are off the ground.
    const auto feet = [&](Object who, Object shadow, bool airborne) {
        auto& p = objects_[who];
        objects_[shadow].x = p.x + 3;
        if (!airborne) objects_[shadow].y = p.y + p.image.height - 3;
    };
    feet(a1, a1_shadow, a_jumping_ || a1_falling_ || a1_bumping_);
    feet(a2, a2_shadow, a2_low_ || a2_hopping_);
    feet(b1, b1_shadow, b1_jumping_ || b1_bumping_);
    feet(b2, b2_shadow, b2_blocking_ || b2_jumping_ || b2_hopping_);
    draw(a1_shadow);
    draw(a2_shadow);
    draw(b1_shadow);
    draw(b2_shadow);
    // Side B nearer the viewer last (B2 in the other team colours).
    bool b1_first;
    if (b1_jumping_) b1_first = b_order_;
    else b1_first = objects_[b2].y >= objects_[b1].y;
    if (b1_first) {
        draw(b1);
        draw(b2, true);
    } else {
        draw(b2, true);
        draw(b1);
    }
    b_order_ = b1_first;
    draw(net);
    draw(ball);
    // Side A the same way; the colours stay with the person across swaps.
    bool a1_first;
    if (fire_latch_a_) a1_first = a_order_;
    else a1_first = objects_[a2].y >= objects_[a1].y;
    if (a1_first) {
        draw(a1, a_swapped_);
        draw(a2, !a_swapped_);
    } else {
        draw(a2, !a_swapped_);
        draw(a1, a_swapped_);
    }
    a_order_ = a1_first;
    // The cursor over side A's player, blinking every 5 passes.
    if (!fire_latch_a_ && !a_jumping_) {
        if (++cursor_count_a_ == 5) {
            cursor_count_a_ = 0;
            cursor_on_a_ = !cursor_on_a_;
        }
        if (cursor_on_a_) {
            objects_[cursor].x = objects_[a1].x + 12;
            objects_[cursor].y = objects_[a1].y - 26;
            draw(cursor);
        }
    }
    if (touch_ == 2) {
        objects_[setter_mark].x = objects_[a2].x - 5;
        objects_[setter_mark].y = objects_[a2].y - 20;
        draw(setter_mark);
    }
    if (touch_ == 4 && !rally_over_ && !b2_blocking_) {
        objects_[setter_mark].x = objects_[b2].x - 5;
        objects_[setter_mark].y = objects_[b2].y - 20;
        draw(setter_mark);
    }
    // The spike's mark over the spiker, blinking for 10 passes.
    const auto spike_mark = [&](bool& on, int& count, Object who) {
        if (!on) return;
        objects_[bump_mark].x = objects_[who].x;
        objects_[bump_mark].y = objects_[who].y - 10;
        if (count >= 10) {
            on = false;
            count = 0;
            return;
        }
        ++count;
        spike_mark_blink_ = !spike_mark_blink_;
        if (spike_mark_blink_) draw(bump_mark);
    };
    spike_mark(spike_mark_a_, spike_mark_count_a_, a1);
    spike_mark(spike_mark_b_, spike_mark_count_b_, b1);
}

// ---- side A's partner (A2) ---------------------------------------------------

// $2E82C: A2's jump (after A1's block call), by the jump table shared with A1.
void Match::a2_jump() {
    if (!a2_jumping_) return;
    auto& p = objects_[a2];
    const auto& jump = data_->jump;
    if (jump_at_ >= int(jump.size())) {
        jump_at_ = 0;
        a2_jumping_ = false;
        a_jumping_ = false;
        a_jump_contact_ = false;
        reach_ = false;
        fire_latch_a_ = false;
        return;
    }
    p.y += jump[std::size_t(jump_at_)];
    p.image = picture_at(0x6B51A, 64, 60);
    ++jump_at_;
}

// $2E7C4: A2's hop after a bump, by the hop table.
void Match::a2_hop() {
    if (!a2_hopping_) return;
    auto& p = objects_[a2];
    p.image = picture_at(0x64C9C, 32, 69);
    const auto& hop = data_->hop;
    if (a1_hop_at_ >= int(hop.size())) {
        a1_hop_at_ = 0;
        a1_bump_done_ = false;
        a2_hopping_ = false;
        p.x -= 10;
        return;
    }
    p.y += hop[std::size_t(a1_hop_at_++)];
}

// $2E58A: A2 sets the ball for A1's attack when it comes down over him.
void Match::a2_set() {
    if (b2_block_done_ || ball_court_x_ < 88) return;
    auto& p = objects_[a2];
    const auto& b = objects_[ball];
    if (touch_ == 2) {
        if (!falling_) return;
        if (b.y + 60 >= p.y) {
            a2_setting_ = true;
            p.image = picture_at(0x64180, 48, 72);
            if (!a2_raised_) {
                p.y -= 16;
                a2_raised_ = true;
            }
        }
        if (b.y + 10 >= p.y) {
            p.image = picture_at(0x64184, 48, 72);
            if (rally_over_) return;
            objects_[landing].y = p.y + p.image.height - 10;
            sound_ = Sound::hit;
            a_attack_ = true;
            b_attack_ = false;
            falling_ = false;
            touch_order_at_ = 5;
            hit();
            b1_may_jump_ = false;
            a2_set_wait_ = true;
            a2_set_count_ = 10;
            // B1 lines up for the block.
            const auto& b1p = objects_[b1];
            b1_from_y_ = b1_to_y_ = b1p.y + b1p.image.height;
            b1_to_y_ -= objects_[landing].y - 120;
            b1_from_x_ = b1p.x;
            b1_to_x_ = b1p.x + 60;
            if (chance_ == 4) b1_to_x_ += 10;
            b1_follow_ = true;
        }
    }
    if (!a2_set_wait_ || --a2_set_count_ != 0) return;
    a2_set_wait_ = false;
    a2_setting_ = false;
    a2_raised_ = false;
    p.y += 16;
    if (touch_ == 1) {
        a2_from_x_ = p.x;
        a2_to_x_ = p.x - 40;
        a2_from_y_ = objects_[landing].y; // the code writes the from height here
        a2_follow_ = true;
    }
    if (players_count_ == 2 && touch_ != 4) {
        b2_from_y_ = objects_[b2].y;
        const auto& mark = objects_[landing];
        b2_to_y_ = 190 - (mark.y + mark.image.height - 90) - 50;
        b2_follow_ = true;
    }
}

} // namespace beach_volley::match
