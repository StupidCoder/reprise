#pragma once
#include "beach_volley_amiga/match/match_data.hpp"
#include "beach_volley_amiga/match/match_sound.hpp"
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// A match on one court (volley.code; game.md "Match"): two teams of two,
// side A on the left (players A1, A2), side B on the right. The program
// runs a main loop that draws a frame and waits for the beam at the bottom
// of the screen before showing it; each pass takes two vertical blanks.
// The port is driven once per vertical blank and steps the match every
// second one, and waits out the program's pauses in blanks as measured.
//
// Research state (2026-10-30): side A's serve with the court and sprites;
// the rally is added routine group by routine group, each checked against
// recordings of the original (beach_volley_assets match-check).
namespace beach_volley::match {

// One port's joystick as the hardware reports it (right = toward +x).
struct Stick {
    bool up{}, down{}, left{}, right{}, fire{};
    template <class A> void persist(A& ar) { ar(up, down, left, right, fire); }
};
struct MatchInput {
    Stick port1; // side A's player
    Stick port0; // side B's in a two-player match
    bool help{}; // the HELP key (with the cheat on: side A wins the match)
    template <class A> void persist(A& ar) { ar(port1, port0, help); }
};

class Match {
public:
    static constexpr int width = 288, height = 200;

    // The match plays its sound through `sound` (volley.code's player and
    // effects; it outlives the match: the next match goes on with it).
    Match(const MatchData& data, int players, MatchSound& sound);

    // The next match ($25078) on `data`'s venue: volley.code stays loaded,
    // so everything goes on from the last match (the objects, the chance
    // table's place, the counters) but what $25078 clears: the scores, the
    // clock, the screens; side A serves.
    void restart(const MatchData& data);

    // One vertical blank.
    void tick(const MatchInput& input);
    // After the winner's picture and jingle ($25E78): the program goes on
    // to the next match.
    bool finished() const { return phase_ == Phase::finished; }
    bool a_won() const { return score_a_ == 7; }
    // The keyboard cheat ($2835C, an 11-key sequence): HELP during a rally
    // wins the match for side A.
    void set_cheat(bool on) { cheat_ = on; }
    platform::Image picture() const;
    // Presentation (not saved): `columns` of the court shown (288 as the
    // original, up to the court's 576: widescreen shows more of it around
    // the original's window); `smooth`: the court and the sprites drawn
    // between the program's last two frames at the display's rate,
    // `fraction` of a blank past the last tick.
    void set_view(int columns, bool smooth, double fraction) {
        view_width_ = columns;
        smooth_ = smooth;
        fraction_ = fraction;
    }
    // The shown frame as colour indices (the court's palette).
    std::vector<std::uint8_t> screen() const;

    // For the research tools.
    const Sprite& object(Object n) const { return objects_[std::size_t(n)]; }
    int blanks() const { return blanks_; }
    bool over() const { return finished(); }

    // A quick save keeps all of it (the data is reloaded by venue).
    template <class A> void persist(A& ar) {
        ar(players_count_, objects_, sticks_, recent_, fire_now_, phase_, wait_, blanks_, stepped_, chance_at_, chance_, chance_before_, chance_after_, score_a_, score_b_);
        ar(server_, show_scores_, clock_running_, clock_blanks_, clock_dots_, time_up_, camera_bytes_, camera_shift_, lift_, ease_run_, ease_at_, court_x_);
        ar(court_y_, ball_court_x_, touch_, touch_order_at_, shot_kind_, plan_, arc_height_, falling_, arc_at_, arc_left_, arc_depth_, shadow_target_);
        ar(landing_y_at_hit_, shadow_rate_, shadow_count_, shadow_dir_, ball_step_, ball_minus_x_, alternate_, rally_over_, rally_over_count_, rally_winner_, ball_in_play_, show_ball_mark_);
        ar(deep_, a_attack_, b_attack_, hit_pending_, reach_, ball_spin_delay_, ball_spin_frame_, serving_a_, serve_follow_, serve_wait_, toss_at_, fire_latch_a_);
        ar(swing_delay_, swing_started_, swing_done_, swing_frame_delay_, swing_frame_, serve_contact_, swing_missed_, jump_at_, b2_height_at_, a1_speed_, a1_speed_x_, b_serve_hit_);
        ar(b_serve_follow_, b_serve_rally_, b_turn_, b_toss_at_, b_serve_height_at_, moving_, walk_, a_swapped_, a_order_, b_order_, a1_falling_, a1_acting_);
        ar(a1_recover_delay_, a_jumping_, a_jump_contact_, a1_bumping_, a1_bump_done_, a1_bump_, a1_action_, a1_hop_at_, a1_recover_at_, ball_left_of_a1_, a1_touched_, a1_reach_);
        ar(a1_spike_done_, a1_fall_at_, a1_fall_started_, register_d1_, d0_high_, a2_hopping_, a2_jumping_, a2_low_, a2_setting_, a2_raised_, a2_set_wait_, a2_set_count_, a2_follow_);
        ar(a2_from_x_, a2_from_y_, a2_to_x_, a2_to_y_, cursor_on_a_, cursor_count_a_, landing_blink_, spike_mark_a_, spike_mark_b_, spike_mark_count_a_, spike_mark_count_b_, spike_mark_blink_);
        ar(b_fire_latch_, fire_latch_b_, b1_acting_, b1_jumping_, b1_jump_at_, b1_jump_hit_, b_jump_contact_, b1_may_jump_, b1_spiked_, b1_recover_at_, b1_recover_delay_, b1_bumping_);
        ar(b1_bump_, b1_hit_done_, b1_hit_wait_, b1_action_, b1_reach_wait_, b1_reach_count_, b1_register_, b1_follow_, b1_from_x_, b1_from_y_, b1_to_x_, b1_to_y_);
        ar(b1_height_at_, b1_speed_, b2_hopping_, b2_jumping_, b2_blocking_, b2_block_at_, b2_block_miss_, b2_block_done_, b2_set_, b2_setting_, b2_raised_, b2_set_wait_);
        ar(b2_set_count_, b2_follow_, b2_from_x_, b2_from_y_, b2_to_x_, b2_to_y_, scores_pending_, buffers_, back_);
        ar(board_, ring_plotted_, hand_at_, place_pending_, run_on_);
        ar(force_flip_, opening_at_, fade_at_, fade_length_, hurry_started_, end_at_, end_volume_, cheat_);
    }

    // Where the chance table stands (the original's depends on how long
    // loading took); research tools set it to a recording's.
    void set_chance_place(int place) { chance_at_ = place; }
    int chance_place() const { return chance_at_; }
    // The pause after a point is a CPU loop in the original, its length in
    // blanks varying by a blank with the music's load; research tools set
    // the lengths a recording had (otherwise 156).
    void set_point_pauses(std::vector<int> blanks) { point_pauses_ = std::move(blanks); }
    // Research: a line of the rally's flags.
    std::string debug() const;
    bool stepped() const { return stepped_; } // the last tick ran a pass of the program's loop

private:
    enum class Phase {
        opening, // the court, two whistles, then the first rally ($25078-$25180)
        serve_a_place, serve_a_wait, serve_a_toss, rally, point_shown, scores_shown,
        serve_b_court, serve_b_stance, serve_b_toss,
        ending,  // $25CA2: the music fades, the winner's picture and jingle
        finished
    };
    enum class Then { finish, down_low, down_high };

    // The joysticks as the program sees them: the vertical blank decodes
    // each into 1 up, 2 away from the net, 4 down, 8 toward the net; some
    // routines read the ports themselves.
    struct Sticks {
        std::uint8_t a{}, b{};
        Stick port1, port0;
        template <class A> void persist(A& ar) { ar(a, b, port1, port0); }
    };
    // A shot as the touch decides it ($2A8EE).
    struct Plan {
        int travel{}, step{}, minus_x{}, dy{};
        template <class A> void persist(A& ar) { ar(travel, step, minus_x, dy); }
    };
    // One of a receiver's timed plays.
    struct Action {
        bool on{};
        int timer{};
        template <class A> void persist(A& ar) { ar(on, timer); }
    };
    struct Walk {
        int delay{};
        std::array<int, 6> step{};
        template <class A> void persist(A& ar) { ar(delay, step); }
    };

    // Side A's serve.
    void serve_a_setup();
    void serve_a_wait_step();
    void serve_a_toss_step();
    void serve_a_swing();
    void latch_fire_a();
    void place_for_rally();
    void spin_ball();
    void draw_serve_a();
    // The match's course.
    void opening_step();
    void ending_step();
    void begin_ending();
    void fade_step();
    void rally_step();
    void point_over();
    void point_shown();
    void scores_shown();
    void new_rally();
    void clear_for_serve_a();
    void serve_b_stance();
    void serve_b_toss_start();
    void serve_b_toss_step();
    void draw_serve_b();
    void draw_frame();
    // The ball.
    void hit();
    void jump_shot(std::uint8_t stick, int minus_x, bool clear_falling, Then& then);
    void touch_shot();
    void b_shot_chance();
    void finish_hit();
    void next_touch();
    void start_arc();
    void start_shadow();
    void start_ease();
    void aim();
    int arc_count(int per) const;
    std::pair<int, int> nearness(Object first, Object second) const;
    void b1_next_height();
    void fly();
    bool arc_step();
    void step_ball_along();
    int pace();
    void lift_for_ball();
    void follow_camera();
    void shift_scene(int d0);
    // The players.
    int animate(int slot);
    void swap_side_a();
    void swap_side_b();
    void move_a1();
    void move_a2();
    void move_b1();
    void move_b2();
    void a1_fall();
    void a1_bump_step();
    void a1_actions();
    void a1_play(int slot);
    void a1_catch();
    void a1_jump();
    bool spike_reach() const;
    int raw_stick_a() const;
    void a2_jump();
    void a2_hop();
    void a2_set();
    int over_mark(Object who, int d0) const;
    void b1_touch();
    void b1_play();
    void b1_actions(int d0);
    void b1_jump();
    void b2_jump();
    void b2_block();
    void b2_set();

    // Display list: what the program draws into the back buffer this pass.
    void draw_court();
    void draw(Object n, bool other_team = false);

    const MatchData* data_;
    MatchSound* sound_;
    int players_count_;
    std::array<Sprite, object_count> objects_{};
    Sticks sticks_;
    std::array<MatchInput, 3> recent_{}; // the last three blanks' input
    bool fire_now_{};
    std::vector<int> b_toss_; // $27FB0

    Phase phase_{Phase::opening};
    int wait_{};        // blanks before the program shows its next frame
    int blanks_{};
    bool stepped_{};
    int chance_at_{};   // the vertical blank's place in the chance table ($28696)
    int chance_{};      // $28694 as the running routine sees it
    int chance_before_{}, chance_after_{};
    void play(MatchSound::Effect e) { sound_->effect(e); }

    // Score and course.
    int score_a_{}, score_b_{};  // $3063C, $3063E
    int server_{1};              // $31B86: 1 side A, 2 side B
    bool show_scores_{};
    bool clock_running_{};       // $28692
    int clock_blanks_{};         // $31D7A
    int clock_dots_{};           // $31D78
    bool time_up_{};             // $28680
    bool hurry_started_{};       // $28684: the hurry song from the ninth minute
    bool cheat_{};               // $2835C
    int opening_at_{};           // blanks since the match started
    int fade_at_{}, fade_length_{}; // $26172: the crowd's fade after a point
    int end_at_{}, end_volume_{}; // $25CA2: blanks since the end began; the music's volume then

    // The camera and the scene.
    int camera_bytes_{};  // $29018
    int camera_shift_{};  // $28B56
    int lift_{};          // $30CC2 / 38: rows the scene is drawn lower
    int ease_run_{}, ease_at_{}; // $28B58, $28B5C

    // Court positions along the court (the screen x plus the camera) of
    // A1, A2, B1, B2 ($2B2C6, $2F82A, $2FBB2, $2FA04) and the ball ($2B15E).
    std::array<int, 4> court_x_{}, court_y_{};
    int ball_court_x_{};

    // The rally.
    int touch_{};            // $25B6A: 1/2 side A's touches, 3/4 side B's
    int touch_order_at_{};   // $2A8EC
    int shot_kind_{};        // $31B8E
    Plan plan_;              // $2A8EE
    int arc_height_{};       // $2A8D8
    bool falling_{};         // $2AEB2
    int arc_at_{}, arc_left_{}, arc_depth_{}; // $2A8DC, $2AEB6, $2A8E0
    int shadow_target_{}, landing_y_at_hit_{}; // $2A8E8, $2A8E2
    int shadow_rate_{}, shadow_count_{}, shadow_dir_{}; // $2A8E6, $2AEAE, $2AEB0
    int ball_step_{};        // $2AEB4
    bool ball_minus_x_{};    // $2901A
    bool alternate_{};       // $2B180
    bool rally_over_{};      // $2AF3E
    int rally_over_count_{}; // $2AF40
    int rally_winner_{};     // $2AEAA: 3 side A, 1 side B
    bool ball_in_play_{true}; // $2AEB8
    bool show_ball_mark_{};  // $2AEA8
    bool deep_{};            // $2A868
    bool a_attack_{};        // $2C3FA
    bool b_attack_{};        // $2F480
    bool hit_pending_{};     // $2C3FC
    bool reach_{};           // $28060
    int ball_spin_delay_{3}, ball_spin_frame_{}; // $2AEBC, $2AEBA

    // Side A's serve.
    bool serving_a_{};        // $27162
    int serve_follow_{10};    // $27168
    int serve_wait_{};        // $27166
    int toss_at_{};           // $27198, words
    bool fire_latch_a_{};     // $28136
    int swing_delay_{};       // $2715A
    bool swing_started_{};    // $27158
    bool swing_done_{};       // $27160
    int swing_frame_delay_{}; // $2715C
    int swing_frame_{};       // $2715E, frames
    bool serve_contact_{};    // $27164
    bool swing_missed_{};     // $2716A
    int jump_at_{};           // $2D492, words (A1's and A2's jumps)
    int b2_height_at_{};      // $26530
    int a1_speed_{2};         // $2B32A
    int a1_speed_x_{4};       // $2B2CA

    // Side B's serve.
    bool b_serve_hit_{};      // $27FAC
    int b_serve_follow_{10};  // $27FAE
    bool b_serve_rally_{};    // $31B8A
    bool b_turn_{};           // $31B8C
    int b_toss_at_{};         // $27FA8, words
    int b_serve_height_at_{}; // $28012

    // Side A.
    std::array<std::uint8_t, 4> moving_{}; // $2B2C4, $2F82E, $2FBB6, $2FA08
    std::array<Walk, 4> walk_{};
    bool a_swapped_{};        // $25C16
    bool a_order_{}, b_order_{}; // $28146, $28148
    bool a1_falling_{};       // $2812A
    bool a1_acting_{};        // $28122
    int a1_recover_delay_{2}; // $2D496
    bool a_jumping_{};        // $2D490
    bool a_jump_contact_{};   // $2D498
    bool a1_bumping_{};       // $2EC4E
    bool a1_bump_done_{};     // $2EC4A
    bool a1_bump_{};          // $2EC4C
    std::array<Action, 6> a1_action_{}; // $2CA64, $2CC2C, $2CD32, $2CF10, $2D080, $2D1F2
    int a1_hop_at_{};         // $2EC50 (the hop table, A1's and A2's)
    int a1_recover_at_{};     // $2D494
    bool ball_left_of_a1_{};  // $2C3FE
    bool a1_touched_{};       // $2C400
    bool a1_reach_{};         // $2EC46
    bool a1_spike_done_{};    // $2D48E
    int a1_fall_at_{};        // $2C5C0
    bool a1_fall_started_{};  // $2C5C2
    int register_d1_{};       // a value the block test reads from a register others leave
    int d0_high_{};           // the high half of d0 as the bounce's division finds it
    bool a2_hopping_{};       // $2EC48
    bool a2_jumping_{};       // $2E8A0
    bool a2_low_{};           // $28140
    bool a2_setting_{};       // $28124
    bool a2_raised_{};        // $2E7BC
    bool a2_set_wait_{};      // $2E7BE
    int a2_set_count_{10};    // $2E7C0
    bool a2_follow_{};        // $2E7C2
    int a2_from_x_{}, a2_from_y_{}, a2_to_x_{}, a2_to_y_{}; // $2F822-$2F828
    bool cursor_on_a_{};      // $25B66
    int cursor_count_a_{};    // $25B62
    bool landing_blink_{};    // $2AEBE
    bool spike_mark_a_{}, spike_mark_b_{};      // $26A8A, $26A8E
    int spike_mark_count_a_{}, spike_mark_count_b_{}; // $26A88, $26A8C
    bool spike_mark_blink_{}; // $26A90

    // Side B.
    bool b_fire_latch_{};     // $2813A
    bool fire_latch_b_{};     // (the same flag, two-player paths)
    bool b1_acting_{};        // $28126
    bool b1_jumping_{};       // $2D95A
    int b1_jump_at_{};        // $2D960, words (B1's and B2's jumps)
    bool b1_jump_hit_{};      // $2D962
    bool b_jump_contact_{};   // $2D6E8
    bool b1_may_jump_{};      // $31B82
    bool b1_spiked_{};        // $31B84
    int b1_recover_at_{};     // $2D95C
    int b1_recover_delay_{2}; // $2D95E
    bool b1_bumping_{};       // $2E582
    bool b1_bump_{};          // $2EE84
    bool b1_hit_done_{};      // $2E586
    bool b1_hit_wait_{};      // $2E588
    std::array<Action, 6> b1_action_{}; // $2E574-$2E57E with $2E568-$2E572
    bool b1_reach_wait_{};    // $2F484
    int b1_reach_count_{};    // $2F482
    int b1_register_{1};      // what B1's routines leave for the reach test
    bool b1_follow_{};        // $2FC0E
    int b1_from_x_{}, b1_from_y_{}, b1_to_x_{}, b1_to_y_{}; // $2FBAA-$2FBB0
    int b1_height_at_{};      // $2FC10
    int b1_speed_{4};         // $2FBB8
    bool b2_hopping_{};       // $2EE80
    bool b2_jumping_{};       // $2E922
    bool b2_blocking_{};      // $2F00E
    int b2_block_at_{};       // $2F010, words
    bool b2_block_miss_{};    // $2F006
    bool b2_block_done_{};    // $2F00A
    bool b2_set_{};           // $2F00C
    bool b2_setting_{};       // $28128
    bool b2_raised_{};        // $2F21E
    bool b2_set_wait_{};      // $2F220
    int b2_set_count_{10};    // $2F222
    bool b2_follow_{};        // $2F9FA
    int b2_from_x_{}, b2_from_y_{}, b2_to_x_{}, b2_to_y_{}; // $2F9FC-$2FA02

    // What was drawn this pass (shown from the next blank on).
    struct Blit {
        bool court{};
        int camera_bytes{}, camera_shift{}, lift{};
        Sprite sprite;
        bool other_team{};
        int object{-1};    // which object it shows (smooth motion matches them)
        int camera_x{-1};  // smooth motion: the court's column at the buffer's left, between two frames
        bool banner{}; // $25EE4: an 80 x 80 picture (rows of 10 bytes, planes $320 apart)
        bool scores{}; // $305AE: both scores in 8 x 8 blocks
        int score_a{}, score_b{};
        std::array<int, 2> board{-1, -1}; // the court's scoreboard digits
        int ring_used{-1};                // the clock ring: dots used
        std::array<int, 2> hand{-1, -1};  // the clock's hand dots
        template <class A> void persist(A& ar) {
            ar(court, camera_bytes, camera_shift, lift, sprite, other_team, object, banner, scores, score_a, score_b,
               board, ring_used, hand);
        }
    };
    bool scores_pending_{};
    std::array<int, 2> board_{};      // the scores drawn into the court ($26210)
    int ring_plotted_{};              // clock dots plotted as used ($31D24)
    std::array<int, 2> hand_at_{0, 6}; // the clock hand's dots ($31E58, $31E5A)
    bool place_pending_{};
    bool run_on_{};
    std::vector<int> point_pauses_;
    // The two screen buffers ($324C0, $3AE80): each pass draws into the
    // back one and shows it; a pass that draws nothing shows the older.
    std::array<std::vector<Blit>, 2> buffers_;
    int back_{};
    // $30C14 shows the back buffer and draws into the other; three places
    // clear its choice first ($2868E = 0): those flips show buffer B and
    // draw into A, whichever was shown (`force_flip_`).
    bool force_flip_{};
    void flip() {
        previous_shown_ = shown(); // for smooth motion
        flip_period_ = blanks_ - last_flip_;
        last_flip_ = blanks_;
        back_ = force_flip_ ? 0 : back_ ^ 1;
        force_flip_ = false;
    }
    std::vector<std::uint8_t> render(const std::vector<Blit>& list, int left, int shown_width, bool wide) const;
    std::vector<Blit> between() const;

    // Presentation.
    int view_width_{width};
    bool smooth_{};
    double fraction_{};
    std::vector<Blit> previous_shown_;
    int last_flip_{}, flip_period_{};
    const std::vector<Blit>& shown() const { return buffers_[std::size_t(back_ ^ 1)]; }
};

} // namespace beach_volley::match
