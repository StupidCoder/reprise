// Side B run by the computer in a one-player match: B1 receives (dig,
// bump, the low and the diving plays shared with a human B1), goes up at
// the net, B2 sets, blocks and covers; and side B's serve.
#include "beach_volley_amiga/match/match.hpp"

namespace beach_volley::match {
namespace {

Image picture_at(std::uint32_t address, int width, int height) {
    return {address - sprite_block_address, width, height};
}

} // namespace

// $2807A: does the player's feet box (x to x+32 at y+height) meet the
// landing mark's (20 x 17)? Leaves `d0` as it was when not.
int Match::over_mark(Object who, int d0) const {
    const auto& p = objects_[who];
    const auto& m = objects_[landing];
    const int x1 = p.x, x2 = p.x + 32, feet = p.y + p.image.height;
    const bool a = m.x < x2, b = m.x + 20 >= x1, c = m.y < feet, d = m.y + 17 >= feet;
    return a && b && c && d ? 1 : d0;
}

// The touch every B1 play ends in.
void Match::b1_touch() {
    b_serve_rally_ = false;
    b_jump_contact_ = false;
    a_jump_contact_ = false;
    b1_acting_ = false;
    b1_hit_done_ = true;
    b1_hit_wait_ = false;
    swing_started_ = false;
    if (rally_over_) return;
    play(MatchSound::Effect::hit);
    falling_ = false;
    hit();
}

// The six receiving plays ($2DD24, $2DE04, $2DFCE, $2E0B0, $2E284,
// $2E3EE): each runs on a countdown with its pictures at fixed counts.
void Match::b1_play() {
    auto& p = objects_[b1];
    const auto& ball_o = objects_[ball];
    const auto& mark = objects_[landing];
    const auto end = [&](int slot) {
        b1_action_[std::size_t(slot)].on = false;
        b1_acting_ = false;
        b1_hit_wait_ = false;
        b_fire_latch_ = false;
        b1_hit_done_ = false;
    };
    const auto shuffle = [&](int dx_limit_kind) { // toward the mark, 4 a pass
        if (b1_hit_done_) return;
        if (p.x >= mark.x) {
            p.x -= 4;
            court_x_[2] -= 4;
        } else if (dx_limit_kind == 0 ? p.x + 8 < mark.x : !(p.x + 8 >= mark.x)) {
            p.x += 4;
            court_x_[2] += 4;
        }
    };
    const auto under = [&]() { return ball_o.x < p.x + 32 && p.x < ball_o.x + 16; };
    if (b1_action_[0].on) { // dig
        auto& a = b1_action_[0];
        if (--a.timer == 0) {
            end(0);
            return;
        }
        p.image = picture_at(0x5C404, 48, 58);
        if (b1_hit_done_) return;
        if (under() && p.y - ball_o.y < 10) {
            b1_hit_done_ = true;
            b1_hit_wait_ = false;
            if (rally_over_) return;
            swing_started_ = false;
            b_turn_ = false;
            a_jump_contact_ = false;
            b_jump_contact_ = false;
            play(MatchSound::Effect::hit);
            falling_ = false;
            hit();
        }
        return;
    }
    if (b1_action_[1].on) { // bump
        auto& a = b1_action_[1];
        if (--a.timer == 0) {
            end(1);
            return;
        }
        if (a.timer == 19) p.image = picture_at(0x5C404, 48, 58);
        if (a.timer == 17) p.image = picture_at(0x5C408, 48, 58);
        if (a.timer == 12) p.image = picture_at(0x5C40C, 48, 54);
        shuffle(0);
        if (!b1_hit_done_ && under() && ball_o.y >= p.y) b1_touch();
        const int rise = data_->dive_rise[std::size_t(a.timer)];
        if (p.y >= 30) {
            p.y -= rise;
            court_y_[2] -= rise;
        }
        return;
    }
    if (b1_action_[2].on) { // crouch
        auto& a = b1_action_[2];
        if (--a.timer == 0) {
            end(2);
            p.y -= 10;
            court_y_[2] -= 10;
            return;
        }
        if (!b1_hit_done_ && under() && p.y - ball_o.y < 10 && !rally_over_) {
            b_serve_rally_ = false;
            b_jump_contact_ = false;
            a_jump_contact_ = false;
            b1_hit_wait_ = false;
            b1_hit_done_ = true;
            swing_started_ = false;
            play(MatchSound::Effect::hit);
            falling_ = false;
            hit();
        }
        return;
    }
    if (b1_action_[3].on) { // low dig
        auto& a = b1_action_[3];
        if (--a.timer == 0) {
            end(3);
            return;
        }
        if (a.timer == 19) p.image = picture_at(0x5CCD0, 48, 48);
        if (a.timer == 17) p.image = picture_at(0x5CCD4, 48, 48);
        if (a.timer == 12) p.image = picture_at(0x5CCD8, 48, 48);
        shuffle(1);
        if (!b1_hit_done_ && under() && p.y + 40 >= ball_o.y && ball_o.y >= p.y) b1_touch();
        const int fall = data_->dig_rise[std::size_t(a.timer)];
        if (p.y < 130) {
            p.y += fall;
            court_y_[2] += fall;
        }
        return;
    }
    for (int slot : {4, 5}) { // the dives, toward +x and toward -x
        auto& a = b1_action_[std::size_t(slot)];
        if (!a.on) continue;
        if (--a.timer == 0) {
            end(slot);
            p.y -= 30;
            court_y_[2] -= 30;
            p.image = {data_->stance[1].at, 48, 59};
            return;
        }
        if (a.timer == 19) {
            p.image = slot == 4 ? picture_at(0x5CCE2, 64, 58) : picture_at(0x5CCDC, 64, 58);
            p.y += 10;
            court_y_[2] += 10;
        }
        if (a.timer == 17) {
            p.image = slot == 4 ? picture_at(0x5D0D0, 96, 24) : picture_at(0x5CD10, 96, 24);
            p.y += 20;
            court_y_[2] += 20;
        }
        if (!b1_hit_done_ && ball_o.y >= p.y && !rally_over_) {
            const bool reach = slot == 4 ? !(p.x + 40 >= ball_o.x)
                                         : (ball_o.x + 16 >= p.x && p.x + 32 >= ball_o.x);
            if (reach) {
                b_serve_rally_ = false;
                b_jump_contact_ = false;
                a_jump_contact_ = false;
                if (slot == 4) a1_acting_ = false; // the code clears A1's flag here (original quirk)
                else b1_acting_ = false;
                b1_hit_wait_ = false;
                swing_started_ = false;
                b1_hit_done_ = true;
                play(MatchSound::Effect::hit);
                falling_ = false;
                hit();
            }
        }
        const int shift = data_->dive_shift[std::size_t(a.timer)];
        if (slot == 4) {
            if (court_x_[2] < 410) {
                p.x += shift;
                court_x_[2] += shift;
            }
        } else if (court_x_[2] >= 296) {
            p.x -= shift;
            court_x_[2] -= shift;
        }
        return;
    }
}

// $2F224: B1 (computer) on side B's first touch: a play by where the
// ball will come down, or a reach when he is under it.
void Match::b1_actions(int d0) {
    if (b1_jumping_) return;
    for (int slot : {4, 5, 1, 3})
        if (b1_action_[std::size_t(slot)].on) {
            b1_play();
            return;
        }
    if (touch_ != 3) {
        if (b1_reach_wait_ && --b1_reach_count_ == 0) {
            b1_reach_count_ = 10;
            b1_acting_ = false;
            b1_reach_wait_ = false;
        }
        return;
    }
    if (!falling_ || rally_over_) return;
    auto& p = objects_[b1];
    auto& ball_o = objects_[ball];
    if (!b1_acting_) {
        if (!over_mark(b1, d0)) {
            // $2F3C0: not at the mark; dive or bump for it when the ball is low.
            const auto& m = objects_[landing];
            if (m.y - ball_o.y >= 70 || rally_over_) return;
            const int behind = p.y + p.image.height - (m.y + m.image.height);
            if (behind >= 0 && std::uint16_t(behind) > 10) {
                b1_action_[1] = {true, 20};
            } else if (m.y - (p.y + p.image.height) >= 10) {
                b1_action_[3] = {true, 20};
            } else if (std::uint16_t(p.x + 32) > std::uint16_t(m.x)) {
                b1_action_[5] = {true, 20};
            } else {
                b1_action_[4] = {true, 20};
            }
            return;
        }
        if (ball_o.x + 32 < p.x) return;
    }
    const int gap = p.y + p.image.height - ball_o.y;
    if (gap < 100) {
        p.image = picture_at(0x5C3EC, 48, 57);
        b1_acting_ = true;
    }
    if (gap < 80) p.image = picture_at(0x5C3F0, 48, 57);
    if (gap >= 66) return;
    p.image = picture_at(0x5C3F4, 48, 57);
    falling_ = false;
    a_jump_contact_ = false;
    swing_started_ = false;
    if (rally_over_) return;
    ball_o.y = p.y + 5;
    play(MatchSound::Effect::hit);
    hit();
    b1_reach_count_ = 10;
    b1_reach_wait_ = true;
    b1_from_x_ = p.x;
    b1_to_x_ = p.x - 50;
}

// $2D6EC: B1 goes up at the net for the attack when the ball is over him.
void Match::b1_jump() {
    if (b2_jumping_) return;
    auto& p = objects_[b1];
    const auto rise = [&]() {
        const auto& jump = data_->jump;
        if (b1_jump_at_ >= int(jump.size())) {
            b1_jump_at_ = 0;
            b1_jumping_ = false;
            b_jump_contact_ = false;
            b1_recover_at_ = 0;
            b1_may_jump_ = false;
            b1_jump_hit_ = false;
            b1_recover_delay_ = 2;
            return;
        }
        p.y += jump[std::size_t(b1_jump_at_++)];
    };
    if (b1_jump_hit_) {
        rise();
        return;
    }
    if (!falling_ || b1_action_[1].on || b1_action_[4].on || b1_action_[5].on || b1_action_[3].on) return;
    if (!b_attack_ || !b1_may_jump_) return;
    if (!b1_jumping_) {
        if (!over_mark(b1, 0)) return;
        b1_jumping_ = true;
    }
    const auto& f = data_->jump_b;
    if (b1_jump_at_ == 0) p.image = f[0];
    if (b1_jump_at_ == 3) p.image = f[1];
    if (b1_jump_at_ == 6) p.image = f[2];
    if (b1_jump_at_ == 10) p.image = f[3];
    if (b1_jump_at_ == 12) p.image = f[4];
    if (!b1_jump_hit_) {
        // $2C4D2: the ball within 20 rows above his head.
        const auto& ball_o = objects_[ball];
        if (ball_o.y + 20 >= p.y && ball_o.y + 20 < p.y + 20) {
            p.image = f[5];
            b1_jump_hit_ = true;
            b_jump_contact_ = true;
            auto& m = objects_[landing];
            m.y = objects_[b1_shadow].y;
            m.x = ball_o.x;
            objects_[ball].x -= 16;
            ball_court_x_ -= 16;
            play(MatchSound::Effect::smash); // $2D87C
            hit();
            a_attack_ = false;
            b1_spiked_ = true;
        }
    }
    rise();
}

// $2E8A2: B2 finishing B1's jump after a swap (two players).
void Match::b2_jump() {
    if (!b2_jumping_) return;
    auto& p = objects_[b2];
    const auto& jump = data_->jump;
    if (b1_jump_at_ >= int(jump.size())) {
        b1_jump_at_ = 0;
        b2_jumping_ = false;
        b1_jumping_ = false;
        b_jump_contact_ = false;
        reach_ = false;
        b_fire_latch_ = false;
        b1_bumping_ = false;
        b1_jump_hit_ = false;
        return;
    }
    p.y += jump[std::size_t(b1_jump_at_++)];
    p.image = picture_at(0x6BE7A, 64, 60);
}

// $2EE90: B2 jumps to block A's attack.
void Match::b2_block() {
    if (!b2_blocking_) return;
    auto& p = objects_[b2];
    const auto& jump = data_->jump;
    if (b2_block_at_ >= int(jump.size())) {
        b2_block_at_ = 0;
        b2_blocking_ = false;
        b2_set_ = false;
        b2_block_done_ = false;
        return;
    }
    p.y += jump[std::size_t(b2_block_at_++)];
    p.image = picture_at(0x6BE60, 64, 60);
    if (b2_block_at_ >= 5) p.image = picture_at(0x64C9A, 32, 69);
    if (court_x_[3] >= 324) return;
    if ((shot_kind_ & 2) || (chance_ != 5 && chance_ != 2 && (shot_kind_ & 5))) b2_block_miss_ = true;
    if (std::uint16_t(objects_[ball].x + 16) <= std::uint16_t(p.x) || b2_block_miss_ || b2_block_done_) return;
    b2_block_done_ = true;
    play(MatchSound::Effect::hit);
    a_attack_ = false;
    b2_set_ = true;
    touch_ = 2;
    touch_order_at_ = 5;
    a_jump_contact_ = false;
    b_jump_contact_ = false;
    hit();
    touch_ = 1;
    touch_order_at_ = 4;
    a2_from_x_ = objects_[a2].x;
    a2_to_x_ = objects_[a2].x - 10;
    a2_follow_ = true;
}

// $2F01A: B2 sets for B1 when the ball comes down over him (phase 4).
void Match::b2_set() {
    auto& p = objects_[b2];
    const auto& ball_o = objects_[ball];
    if (!b2_set_wait_) {
        if (b2_blocking_ || touch_ != 4 || rally_over_ || ball_court_x_ >= 479 || !falling_) return;
        if (ball_o.y + 60 >= p.y && ball_o.y < p.y) {
            b2_setting_ = true;
            p.image = picture_at(0x64180, 48, 72);
            if (!b2_raised_) {
                p.y -= 16;
                b2_raised_ = true;
            }
        }
        if (ball_o.y + 10 < p.y) return;
        p.image = picture_at(0x64184, 48, 72);
        if (rally_over_) return;
        play(MatchSound::Effect::hit);
        a_jump_contact_ = false;
        b_jump_contact_ = false;
        a_attack_ = false;
        b_attack_ = true;
        falling_ = false;
        hit();
        b2_set_wait_ = true;
        b2_set_count_ = 10;
    }
    if (--b2_set_count_ != 0) return;
    b2_set_wait_ = false;
    b2_setting_ = false;
    b2_raised_ = false;
    p.y += 16;
    if (touch_ != 2) {
        const auto& a2p = objects_[a2];
        const auto& mark = objects_[landing];
        a2_from_y_ = a2p.y;
        a2_to_y_ = 190 - (mark.y + mark.image.height - 90) - 50;
        a2_to_x_ = a2p.x;
        a2_from_x_ = a2p.x;
        a2_follow_ = true;
    }
    b2_to_x_ = p.x + 60;
    b2_from_x_ = p.x;
    if (chance_ == 4) b2_to_x_ += 40;
    const auto& b1p = objects_[b1];
    b2_to_y_ = b1p.y;
    b2_to_y_ += (b1p.y + b1p.image.height >= 130) ? -50 : 50;
    b2_follow_ = true;
}

} // namespace beach_volley::match
