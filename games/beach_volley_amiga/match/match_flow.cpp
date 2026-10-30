// The match's course: the vertical blank, the loop passes and the pauses
// between them, points, side B's serve in a one-player match, and side
// A1's movement under the joystick.
#include "beach_volley_amiga/match/match.hpp"
#include <cstdio>

namespace beach_volley::match {
namespace {

Image picture_at(std::uint32_t address, int width, int height) {
    return {address - sprite_block_address, width, height};
}

// Blanks the program spends between the frames it shows around a point
// (measured on the original: its waits are CPU loops and fades).
constexpr int pause_after_point = 155; // $26172's volume fade
constexpr int pause_before_toss = 26;  // $27DE2's delay after B's serve stance shows

} // namespace

// The level-3 interrupt's vertical blank part ($28388): it decodes the
// joysticks and steps the chance table; the program's loop shows a frame
// whenever it is done with one, at least a blank after the last.
void Match::tick(const MatchInput& input) {
    const auto decode = [](const Stick& s, bool toward_net_is_right) {
        std::uint8_t v = 0;
        if (s.up) v |= 1;
        if (s.down) v |= 4;
        if (toward_net_is_right ? s.right : s.left) v |= 8;
        if (toward_net_is_right ? s.left : s.right) v |= 2;
        return v;
    };
    // A pass reads the sticks as they stood a blank before its frame shows;
    // the serve's wait tests the button after its frame shows (`fire_now_`).
    const auto& older = recent_[std::size_t((blanks_ + 2) % 3)];
    sticks_.a = decode(older.port1, true);
    sticks_.b = decode(older.port0, false);
    sticks_.port1 = older.port1;
    sticks_.port0 = older.port0;
    fire_now_ = input.port1.fire;
    recent_[std::size_t(blanks_ % 3)] = input;
    if (chance_at_ >= int(data_->chance.size())) chance_at_ = 0;
    chance_before_ = chance_after_;
    chance_after_ = data_->chance[std::size_t(chance_at_++)];
    // A rally pass's logic runs before the blank that falls while the
    // court is blitted; its drawing and the ball's flight after it.
    chance_ = chance_after_;
    if (clock_running_ && players_count_ == 1 && ++clock_blanks_ >= 1100) {
        clock_blanks_ = 0;
        if (clock_dots_ < 22) clock_dots_ += 2;
    }
    if (players_count_ == 1 && clock_dots_ == 22) time_up_ = true;

    stepped_ = false;
    ++blanks_;
    if (wait_ > 0) {
        --wait_;
        return;
    }
    stepped_ = true;
    wait_ = 1;
    sound_ = Sound::none;
    switch (phase_) {
    case Phase::serve_a_place:
        // $262AA shows the older buffer first, then places everyone (before
        // the serve's first frame).
        phase_ = Phase::serve_a_wait;
        place_pending_ = true;
        wait_ = 3;
        break;
    case Phase::serve_a_wait: serve_a_wait_step(); break;
    case Phase::serve_a_toss: serve_a_toss_step(); break;
    case Phase::rally: rally_step(); break;
    case Phase::point_shown: point_shown(); break;
    case Phase::scores_shown: scores_shown(); break;
    case Phase::serve_b_court: serve_b_stance(); break;
    case Phase::serve_b_stance: serve_b_toss_start(); break;
    case Phase::serve_b_toss: serve_b_toss_step(); break;
    case Phase::match_over: break;
    }
    if (run_on_) {
        // The pass went on into the next without showing a frame; the
        // next runs at the following blank and shows one.
        run_on_ = false;
        stepped_ = false;
        wait_ = 0;
        return;
    }
    flip();
    // The flip leaves the low word of the shown buffer's last plane in d0's
    // high half ($30C14 ends with a swap).
    d0_high_ = back_ ? 0x8C10 : 0x15D0;
    if (scores_pending_) {
        scores_pending_ = false;
        Blit b;
        b.scores = true;
        b.score_a = score_a_;
        b.score_b = score_b_;
        buffers_[std::size_t(back_ ^ 1)].push_back(b);
    }
}

// $260C2: the rally is over. The point is scored; the last frame shows.
void Match::point_over() {
    if (rally_winner_ == 3 || (rally_winner_ != 1 && (touch_ == 3 || touch_ == 4))) {
        ++score_a_;
        server_ = 1;
    } else {
        ++score_b_;
        server_ = 2;
    }
    board_ = {score_a_, score_b_}; // $26102 draws them into the court
    phase_ = Phase::point_shown;
    // The pass that ended the rally drew its frame; it shows now, and the
    // court follows a blank later.
    wait_ = 0;
}

// $26102: the court at the middle with the net; once shown, the scores
// are drawn onto it, and the program pauses.
void Match::point_shown() {
    camera_bytes_ = 16;
    camera_shift_ = 0;
    draw_court();
    objects_[net].x = 148;
    draw(net);
    scores_pending_ = true;
    rally_over_ = false;
    if (score_a_ == 7 || score_b_ == 7) {
        phase_ = Phase::match_over;
        return;
    }
    wait_ = pause_after_point;
    if (!point_pauses_.empty()) {
        wait_ = point_pauses_.front() - 1;
        point_pauses_.erase(point_pauses_.begin());
    }
    new_rally();
}

void Match::scores_shown() {}

// $25180, $26008: the rally's state back to its start, and the next serve.
void Match::new_rally() {
    clock_running_ = false;      // $25180
    a2_hopping_ = false;
    a_swapped_ = false;
    hit_pending_ = false;
    swing_started_ = false;
    serve_contact_ = false;
    touch_order_at_ = 0;
    rally_over_ = false;
    a1_bumping_ = false;
    b1_bumping_ = false;
    a1_bump_done_ = false;
    a_jump_contact_ = false;
    a2_setting_ = false;
    a2_raised_ = false;
    b2_set_ = false;
    b2_block_done_ = false;
    b2_blocking_ = false;
    b1_acting_ = false;
    b1_jumping_ = false;
    b_jump_contact_ = false;
    b1_may_jump_ = false;
    shot_kind_ = 0;
    rally_winner_ = 0;
    ball_in_play_ = true;
    lift_ = 0;                   // $26008
    b1_jump_at_ = 0;
    b1_spiked_ = false;
    a2_set_wait_ = false;
    a2_set_count_ = 10;
    b2_set_wait_ = false;
    b2_set_count_ = 10;
    b2_raised_ = false;
    swing_delay_ = 4;
    swing_done_ = false;
    fire_latch_a_ = false;
    serve_follow_ = 10;
    swing_frame_delay_ = 1;
    swing_frame_ = 0;
    serve_wait_ = 1000;
    jump_at_ = 0;
    a_jumping_ = false;
    ease_at_ = 0;
    reach_ = false;
    a1_recover_delay_ = 2;
    if (server_ == 1) {
        phase_ = Phase::serve_a_place;
    } else if (players_count_ == 1) {
        phase_ = Phase::serve_b_court;
    }
}

// $26B20's clears before side A serves.
void Match::clear_for_serve_a() {
    a_swapped_ = false;
    a1_bump_ = false;
    b1_bump_ = false;
    b2_set_ = false;
    a_jump_contact_ = false;
    swing_started_ = false;
    lift_ = 0;
    touch_order_at_ = 0;
    touch_ = 0;
    a2_hopping_ = false;
    b2_hopping_ = false;
    a1_hop_at_ = 0;
    a2_jumping_ = false;
    b2_jumping_ = false;
    a_attack_ = false;
    a2_follow_ = false;
    b2_follow_ = false;
    b_serve_rally_ = false;
    b_serve_hit_ = false;
    b_turn_ = true;
    b1_jumping_ = false;
    b1_jump_at_ = 0;
    b_jump_contact_ = false;
    b_attack_ = false;
    b1_reach_wait_ = false;
    b1_jump_hit_ = false;
    b1_bumping_ = false;
    b2_blocking_ = false;
    for (auto& a : b1_action_) a.on = false;
}

// $27ACC: side B serves (computer). The court is shown from its right.
void Match::serve_b_stance() {
    lift_ = 0;
    b_turn_ = false;
    a_swapped_ = false;
    shot_kind_ = 0;
    a_jump_contact_ = false;
    b_jump_contact_ = false;
    fire_latch_a_ = false;
    a_jumping_ = false;
    a1_acting_ = false;
    a2_hopping_ = false;
    a2_jumping_ = false;
    a1_bumping_ = false;
    a1_hop_at_ = 0;
    b2_hopping_ = false;
    b2_jumping_ = false;
    b1_bumping_ = false;
    b2_blocking_ = false;
    a2_setting_ = false;
    a2_follow_ = false;
    b2_follow_ = false;
    b_serve_rally_ = true;
    b_toss_at_ = 0;
    b_serve_hit_ = false;
    b_serve_follow_ = 10;
    serve_follow_ = 2;
    camera_bytes_ = 34;
    camera_shift_ = 0;
    draw_court(); // $2654E shows the bare court first
    phase_ = Phase::serve_b_stance;
}

void Match::serve_b_toss_start() {
    auto& p = objects_[b1];
    p.x = 230;
    const auto& heights = data_->serve_heights_b;
    if (b_serve_height_at_ >= int(heights.size())) b_serve_height_at_ = 0;
    p.y = heights[std::size_t(b_serve_height_at_++)];
    court_x_[2] = p.x + 240;
    court_y_[2] = p.y;
    // The code puts this shadow in A1's record (original quirk).
    objects_[a1_shadow].x = p.x + 3;
    objects_[a1_shadow].y = p.y + 68;
    p.image = picture_at(0x6C7C0, 48, 68);
    auto& b = objects_[ball];
    b.x = p.x - 4;
    ball_court_x_ = b.x + 272;
    b.y = p.y + 31;
    objects_[ball_shadow].x = p.x - 4;
    objects_[ball_shadow].y = p.y + 64;
    objects_[b1_shadow].x = p.x + 3;
    objects_[b1_shadow].y = p.y + 67;
    auto& q = objects_[b2];
    q.x = 100;
    court_x_[3] = 372;
    q.y = 120;
    court_y_[3] = 120;
    q.image = {data_->stance[3].at, 48, 60};
    objects_[b2_shadow].x = q.x + 4;
    objects_[b2_shadow].y = q.y + 57;
    objects_[net].x = 4;
    objects_[net].y = 10;
    objects_[a1].x = -132;
    objects_[a1].y = 50;
    court_x_[0] = 140;
    court_y_[0] = 50;
    objects_[a2].x = -92;
    objects_[a2].y = 100;
    court_x_[1] = 180;
    court_y_[1] = 100;
    objects_[clock].x = 16;
    objects_[clock].y = 160;
    objects_[clock].image.width = 48;
    objects_[clock].image.height = 31;
    draw_serve_b();
    objects_[landing].x = p.x;
    objects_[landing].y = p.y + 64;
    phase_ = Phase::serve_b_toss;
    wait_ = pause_before_toss - 1;
}

void Match::draw_serve_b() {
    draw_court();
    draw(b2, true);
    draw(ball);
    draw(ball_shadow);
    draw(b1_shadow);
    draw(b2_shadow);
    draw(net);
    draw(b1);
    draw(clock);
}

// $27DF2: the toss and the serve.
void Match::serve_b_toss_step() {
    draw_serve_b();
    auto& p = objects_[b1];
    objects_[ball].y += b_toss_[std::size_t(b_toss_at_)];
    ++b_toss_at_;
    if (b_toss_at_ == 2) p.image = picture_at(0x6C7C4, 64, 68);
    if (b_toss_at_ == 7) p.image = picture_at(0x6C7CA, 64, 68);
    if (b_toss_at_ == 11) p.image = picture_at(0x6C7D0, 64, 68);
    if (!b_serve_hit_ && b_toss_at_ == 20) {
        p.image = picture_at(0x6C7D6, 64, 68);
        touch_order_at_ = 3;
        b_serve_hit_ = true;
        a_attack_ = false;
        sound_ = Sound::hit;
        falling_ = false;
        hit();
        serve_follow_ = 2;
    }
    if (b_toss_at_ == 23) {
        p.image = picture_at(0x6C7DC, 64, 68);
        p.x -= 14;
        b_toss_at_ = 0;
        phase_ = Phase::rally;
        run_on_ = true; // the rally's first pass follows without a frame between
        return;
    }
    spin_ball();
}

// $2B1DC: A1 under the joystick (read from the port directly).
void Match::move_a1() {
    if (a1_falling_ || fire_latch_a_ || a_jumping_ || a1_bumping_) return;
    const int speed = a1_speed_x_;
    moving_[0] = 0;
    auto& p = objects_[a1];
    const auto& s = sticks_.port1;
    const bool down = s.down && !s.up, up = s.up && !s.down;
    const int raw = raw_stick_a();
    register_d1_ = std::int16_t(raw ^ (raw >> 1));
    if (s.right && court_x_[0] < 246) {
        p.x += speed;
        court_x_[0] += speed;
        moving_[0] |= 2;
    }
    if (s.left && std::uint16_t(court_x_[0]) > 90) {
        p.x -= speed;
        court_x_[0] -= speed;
        moving_[0] |= 8;
    }
    if (down && p.y < 127) {
        p.y += speed;
        moving_[0] |= 4;
    }
    if (up && std::uint16_t(p.y) > 36) {
        p.y -= speed;
        moving_[0] |= 1;
    }
}

} // namespace beach_volley::match

namespace beach_volley::match {

std::string Match::debug() const {
    char line[256];
    std::snprintf(line, sizeof line,
                  "chance %d touch %d latch %d acting %d jumping %d bumping %d contact %d serving %d attack %d/%d "
                  "fire %d stick %d",
                  chance_at_, touch_, fire_latch_a_, a1_acting_, a_jumping_, a1_bumping_, serve_contact_, serving_a_,
                  a_attack_, b_attack_, sticks_.port1.fire, sticks_.a);
    return line;
}

} // namespace beach_volley::match
