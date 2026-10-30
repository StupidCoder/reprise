// Side A1 under the player's button ($2C632 and its plays, $2E986, $2D25E,
// $2C508): the button with the stick chooses a play by where the ball will
// come down; each play runs on a countdown with fixed pictures and touches
// the ball when it comes within reach.
#include "beach_volley_amiga/match/match.hpp"

namespace beach_volley::match {
namespace {

Image picture_at(std::uint32_t address, int width, int height) {
    return {address - sprite_block_address, width, height};
}

} // namespace

// The stick as the hardware reports it (JOY1DAT): some routines test the
// register's bits, and its value lingers in a register the block test
// below compares (original behaviour, kept).
int Match::raw_stick_a() const {
    const auto& s = sticks_.port1;
    int v = 0;
    if (s.right) v |= s.down ? 0x0002 : 0x0003;
    else if (s.down) v |= 0x0001;
    if (s.left) v |= s.up ? 0x0200 : 0x0300;
    else if (s.up) v |= 0x0100;
    return v;
}

// $28040: the button, once pressed, stays pressed for the play.
void Match::latch_fire_a() {
    if (!fire_latch_a_ && sticks_.port1.fire) fire_latch_a_ = true;
}

// $2E986 (called twice a pass): at the net A1 hops to block on button and
// up; the hop touches the ball coming over him.
void Match::a1_bump_step() {
    if (a_jumping_ || b_serve_rally_ || a2_hopping_) return;
    auto& p = objects_[a1];
    const auto& b = objects_[ball];
    if (!a1_bumping_) {
        if (register_d1_ - b.x >= 0) return;
        if (touch_ == 2 || a_attack_ || court_x_[0] < 220) return;
        latch_fire_a();
        if (!fire_latch_a_) return;
        sticks_.a &= 1; // the routine masks the decoded stick in place
        if (!sticks_.a) return;
        a1_bumping_ = true;
    }
    const auto& hop = data_->hop;
    if (a1_hop_at_ >= int(hop.size())) {
        a1_bumping_ = false;
        a1_hop_at_ = 0;
        a1_bump_ = false;
        a1_bump_done_ = false;
        fire_latch_a_ = false;
        for (auto& a : a1_action_) a.on = false;
        return;
    }
    p.y += hop[std::size_t(a1_hop_at_)];
    if (a1_hop_at_ == 0) p.image = picture_at(0x6B500, 64, 60);
    if (a1_hop_at_ == 2) {
        p.image = picture_at(0x64C9C, 32, 69);
        p.x += 10;
    }
    if (a1_hop_at_ == 41) {
        p.image = picture_at(0x6B500, 64, 60);
        p.x -= 10;
    }
    ++a1_hop_at_;
    register_d1_ = p.x + 16;
    if (register_d1_ < b.x) return;
    register_d1_ = p.y;
    if (b.y + 16 < p.y) a1_bump_done_ = true;
    if (shot_kind_ & 7) a1_bump_done_ = true;
    register_d1_ = p.y + 50;
    if (b.y >= p.y + 50) a1_bump_done_ = true;
    if (a1_bump_done_) return;
    b_jump_contact_ = false;
    a_jump_contact_ = false;
    a1_bump_done_ = true;
    sound_ = Sound::hit;
    a1_bump_ = true;
    hit();
    touch_ = 3;
    touch_order_at_ = 1;
    a_attack_ = true;
    b1_follow_ = false;
    if (b1_jumping_) {
        b2_jumping_ = true;
        swap_side_b();
    }
    b_attack_ = false;
    b1_acting_ = false;
    b1_bumping_ = false;
    b1_jumping_ = false;
    b_fire_latch_ = false;
    a1_acting_ = false;
    b1_may_jump_ = false;
    b2_follow_ = false;
    if (players_count_ == 2) return;
    b1_from_x_ = objects_[b1].x;
    b1_from_y_ = objects_[b1].y;
    b1_to_x_ = objects_[landing].x;
    b1_to_y_ = objects_[landing].y - 50;
    b1_follow_ = true;
}

// $2C632: A1's plays. A running play goes on; otherwise the button starts
// one: a jump at the net (up, attacking), else by where the mark is.
void Match::a1_actions() {
    if (a1_bumping_ || a_jumping_ || serve_contact_) return;
    for (int slot : {4, 5, 0, 1, 2, 3})
        if (a1_action_[std::size_t(slot)].on) {
            a1_play(slot);
            return;
        }
    latch_fire_a();
    if (!fire_latch_a_) return;
    auto& p = objects_[a1];
    if (!rally_over_ && a_attack_ && touch_ == 1 && (sticks_.a & 1) && court_x_[0] >= 180 && !b1_jumping_) {
        a_jumping_ = true;
        jump_at_ = 0;
        a1_reach_ = false;
        if (players_count_ == 1) {
            // Side B's blocker answers by the chance value.
            const auto& b2s = objects_[b2_shadow];
            const auto& m = objects_[landing];
            if (b2s.y >= m.y && m.y + 30 > b2s.y && (chance_ == 5 || chance_ == 2 || chance_ == 4)) {
                b2_blocking_ = true;
                b2_block_miss_ = false;
                b2_block_at_ = 0;
            }
            return;
        }
        // Two players: the play selection below runs as well.
    }
    if (touch_ == 1) {
        const auto& m = objects_[landing];
        const int feet = p.y + p.image.height;
        const int below = (m.y + m.image.height) - feet;
        if (below < 0) {
            if (-below < 10) a1_action_[0] = {true, 15};
            else a1_action_[1] = {true, 20};
            a1_acting_ = true;
            return;
        }
        const int ahead = m.y - feet;
        if (ahead >= 0) {
            if (ahead < 5) {
                a1_action_[2] = {true, 15};
                a1_acting_ = true;
                p.y += 10;
                court_y_[0] += 10;
                p.image = picture_at(0x5CCD0, 48, 48);
            } else {
                a1_action_[3] = {true, 20};
                a1_acting_ = true;
            }
            return;
        }
        if (m.x - (p.x + 32) >= 0) {
            a1_action_[4] = {true, 20};
            a1_acting_ = true;
            return;
        }
        if (p.x - (m.x + 16) >= 0) {
            a1_action_[5] = {true, 20};
            a1_acting_ = true;
            return;
        }
    }
    // $2C8B8: no play fits; a touch when the ball drops on him, and the
    // recovery frames.
    a1_catch();
    a1_acting_ = over_mark(a1, a1_acting_ ? 1 : 0) != 0;
    if (--a1_recover_delay_ != 0) return;
    a1_recover_delay_ = 2;
    const auto& frames = ball_left_of_a1_ ? data_->recover_left : data_->recover_right;
    if (a1_recover_at_ >= int(frames.size())) {
        a1_recover_delay_ = 2;
        a1_recover_at_ = 0;
        a1_acting_ = false;
        fire_latch_a_ = false;
        a1_touched_ = false;
        reach_ = false;
        return;
    }
    p.image = frames[std::size_t(a1_recover_at_++)];
}

// $2C30C: in phase 1, the ball falling onto A1 is touched.
void Match::a1_catch() {
    if (touch_ != 1 || !falling_) return;
    const auto& p = objects_[a1];
    const auto& b = objects_[ball];
    ball_left_of_a1_ = b.x < p.x + 10;
    if (b.y + 5 < p.y || b.y + 5 >= p.y + 40) return;
    if (!ball_left_of_a1_) {
        if (b.x >= p.x + 32) return;
    } else if (b.x < p.x - 32) {
        return;
    }
    if (a1_touched_ || rally_over_) return;
    a1_touched_ = true;
    b_serve_rally_ = false;
    b_jump_contact_ = false;
    a_jump_contact_ = false;
    reach_ = false;
    swing_started_ = false;
    sound_ = Sound::hit;
    falling_ = false;
    hit();
}

// The six plays ($2C962 dig, $2CA68 bump, $2CC30 crouch, $2CD36 low dig,
// $2CF14 and $2D084 the dives).
void Match::a1_play(int slot) {
    auto& a = a1_action_[std::size_t(slot)];
    auto& p = objects_[a1];
    const auto& b = objects_[ball];
    const auto& m = objects_[landing];
    const auto end = [&]() {
        a.on = false;
        a1_acting_ = false;
        reach_ = false;
        fire_latch_a_ = false;
        a1_touched_ = false;
    };
    const auto touch = [&](bool clear_acting) {
        b_serve_rally_ = false;
        b_jump_contact_ = false;
        a_jump_contact_ = false;
        if (clear_acting) a1_acting_ = false;
        reach_ = false;
        swing_started_ = false;
        a1_touched_ = true;
        sound_ = Sound::hit;
        falling_ = false;
        hit();
    };
    const auto under = [&]() { return b.x < p.x + 32 && p.x < b.x + 16; };
    const auto shuffle = [&]() {
        if (a1_touched_) return;
        if (p.x >= m.x) {
            p.x -= 4;
            court_x_[0] -= 4;
        } else if (p.x + 8 < m.x) {
            p.x += 4;
            court_x_[0] += 4;
        }
    };
    const auto fall_after = [&]() {
        if (players_count_ != 2 && (shot_kind_ & 16)) {
            a1_falling_ = true;
            a1_fall_at_ = 0;
        }
    };
    if (--a.timer == 0) {
        end();
        if (slot == 2) {
            p.y -= 10;
            court_y_[0] -= 10;
        }
        if (slot >= 4) {
            p.y -= 30;
            court_y_[0] -= 30;
            p.image = {data_->stance[1].at, 48, 59};
        }
        return;
    }
    switch (slot) {
    case 0: // dig
        p.image = picture_at(0x5C404, 48, 58);
        if (a1_touched_ || !under() || p.y - b.y >= 10 || rally_over_) return;
        a1_touched_ = true;
        reach_ = false;
        fall_after();
        swing_started_ = false;
        b_serve_rally_ = false;
        b_jump_contact_ = false;
        a_jump_contact_ = false;
        sound_ = Sound::hit;
        falling_ = false;
        hit();
        return;
    case 1: { // bump
        if (a.timer == 19) p.image = picture_at(0x5C404, 48, 58);
        if (a.timer == 17) p.image = picture_at(0x5C408, 48, 58);
        if (a.timer == 12) p.image = picture_at(0x5C40C, 48, 54);
        shuffle();
        if (!a1_touched_ && under() && b.y >= p.y && !rally_over_) {
            b_serve_rally_ = false;
            b_jump_contact_ = false;
            a_jump_contact_ = false;
            a1_acting_ = false;
            reach_ = false;
            swing_started_ = false;
            a1_touched_ = true;
            sound_ = Sound::hit;
            falling_ = false;
            hit();
        }
        const int rise = data_->dive_rise[std::size_t(a.timer)];
        if (p.y >= 30) {
            p.y -= rise;
            court_y_[0] -= rise;
        }
        return;
    }
    case 2: // crouch
        if (a1_touched_ || !under() || p.y - b.y >= 10 || rally_over_) return;
        a1_touched_ = true;
        reach_ = false;
        fall_after();
        b_serve_rally_ = false;
        b_jump_contact_ = false;
        a_jump_contact_ = false;
        swing_started_ = false;
        sound_ = Sound::hit;
        falling_ = false;
        hit();
        return;
    case 3: { // low dig
        if (a.timer == 19) p.image = picture_at(0x5CCD0, 48, 48);
        if (a.timer == 17) p.image = picture_at(0x5CCD4, 48, 48);
        if (a.timer == 12) p.image = picture_at(0x5CCD8, 48, 48);
        shuffle();
        const int feet = p.y + p.image.height;
        if (!a1_touched_ && under() && b.y >= p.y && std::uint16_t(feet) > std::uint16_t(m.y) && !rally_over_)
            touch(true);
        const int fall = data_->dig_rise[std::size_t(a.timer)];
        if (p.y < 130) {
            p.y += fall;
            court_y_[0] += fall;
        }
        return;
    }
    default: { // the dives
        if (a.timer == 19) {
            p.image = slot == 4 ? picture_at(0x5CCE2, 64, 58) : picture_at(0x5CCDC, 64, 58);
            p.y += 10;
            court_y_[0] += 10;
        }
        if (a.timer == 17) {
            p.image = slot == 4 ? picture_at(0x5D0D0, 96, 24) : picture_at(0x5CD10, 96, 24);
            p.y += 20;
            court_y_[0] += 20;
        }
        const bool reach = slot == 4 ? (b.x >= p.x && b.y >= p.y) : (b.x + 32 >= p.x && !a1_touched_ && b.y >= p.y);
        if (reach && !a1_touched_ && !rally_over_) touch(true);
        const int shift = data_->dive_shift[std::size_t(a.timer)];
        if (slot == 4) {
            if (court_x_[0] < 200) {
                p.x += shift;
                court_x_[0] += shift;
            }
        } else if (p.x >= 30) {
            p.x -= shift;
            court_x_[0] -= shift;
        }
        return;
    }
    }
}

// $2D25E: A1's jump at the net; the spike when the ball meets the hand.
void Match::a1_jump() {
    if (!a_jumping_ || a2_jumping_ || a1_bumping_) return;
    auto& p = objects_[a1];
    const auto& jump = data_->jump;
    if (jump_at_ >= int(jump.size())) {
        jump_at_ = 0;
        a_jumping_ = false;
        a1_acting_ = false;
        a_jump_contact_ = false;
        reach_ = false;
        fire_latch_a_ = false;
        a1_recover_at_ = 0;
        a1_spike_done_ = false;
        a1_reach_ = false;
        a1_recover_delay_ = 2;
        return;
    }
    if (!a_jump_contact_) {
        const auto& f = data_->jump_a;
        if (jump_at_ == 0) p.image = f[0];
        if (jump_at_ == 3) p.image = f[1];
        if (jump_at_ == 6) p.image = f[2];
        if (jump_at_ == 10) p.image = f[3];
        if (jump_at_ == 12) p.image = f[4];
        if (jump_at_ >= 14 && sticks_.port1.fire && p.image != picture_at(0x6B51A, 64, 60)) {
            p.image = f[5];
            if (!a1_spike_done_ && spike_reach()) {
                sound_ = Sound::spike;
                objects_[ball].x += 16;
                ball_court_x_ += 16;
                auto& mark = objects_[landing];
                mark.y = objects_[a1_shadow].y;
                mark.x = objects_[ball].x;
                a_jump_contact_ = true;
                a1_spike_done_ = true;
                b_attack_ = false;
                touch_order_at_ = 0;
                reach_ = false;
                if (touch_ == 2) {
                    rally_winner_ = 1;
                    sound_ = Sound::whistle;
                    rally_over_ = true;
                    rally_over_count_ = 30;
                }
                touch_ = 1;
                b_attack_ = false;
                hit();
                spike_mark_a_ = true;
            }
        }
    }
    p.y += jump[std::size_t(jump_at_++)];
}

// $2C402: the ball at A1's hand at the top of the jump.
bool Match::spike_reach() const {
    const auto& p = objects_[a1];
    const auto& b = objects_[ball];
    if (b.x + 20 - p.x < 0) return false;
    if (p.x + 32 - b.x < 0) return false;
    if (jump_at_ >= 23) return false;
    if (b.y + 20 < p.y) return false;
    return b.y + 20 < p.y + 30;
}

// $2C508: A1 falls after a spike from side B in a one-player match.
void Match::a1_fall() {
    auto& p = objects_[a1];
    if (!a1_fall_started_) {
        a1_fall_started_ = true;
        p.y += 23;
        court_y_[0] += 23;
    }
    const auto& fx = data_->fall_x;
    if (a1_fall_at_ >= int(fx.size())) {
        a1_falling_ = false;
        a1_fall_at_ = 0;
        fire_latch_a_ = false;
        a1_fall_started_ = false;
        p.y -= 23;
        court_y_[0] -= 23;
        return;
    }
    p.image = picture_at(0x5D4A0, 64, 34);
    p.x += fx[std::size_t(a1_fall_at_)];
    court_x_[0] += fx[std::size_t(a1_fall_at_)];
    const int dy = data_->fall_y[std::size_t(a1_fall_at_)];
    p.y += dy;
    court_y_[0] += dy;
    ++a1_fall_at_;
}

} // namespace beach_volley::match
