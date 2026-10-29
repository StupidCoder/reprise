#include "rock_n_roll_amiga/game/module.hpp"
#include "platform/files.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"
#include "rock_n_roll_amiga/game/ball.hpp"
#include "rock_n_roll_amiga/game/front_end.hpp"
#include "rock_n_roll_amiga/game/level_map.hpp"
#include "rock_n_roll_amiga/game/overview_map.hpp"
#include "rock_n_roll_amiga/game/records.hpp"
#include "rock_n_roll_amiga/game/screen.hpp"
#include "rock_n_roll_amiga/sound/game_sound.hpp"
#include "state/archive.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace rocknroll::game {
namespace {

constexpr std::uint32_t prompt_texts = 0x6816E;  // four lines for the prompt after a fall ($6BBA4)
constexpr std::uint32_t no_limit_text = 0x6B10C; // "NO " beside the hourglass when there is no time limit
constexpr std::uint32_t balls_text = 0x6810B;    // "3  BALLS REMAINING" (5, 5), the digit at $6810D
constexpr std::uint32_t last_ball_text = 0x68126; // "     LAST BALL" (5, 5)
constexpr std::uint32_t game_over_text = 0x680EF; // "     GAME OVER" (5, 5)
constexpr std::uint32_t out_of_time_text = 0x681E8; // "OUT OF TIME" (1, 5)
constexpr std::uint32_t warp_texts = 0x69CC2;    // the secret warp's message (a list of texts)
constexpr std::uint32_t warp_digits = 0x67836;   // its level number, in the third text
constexpr std::uint32_t rewards = 0x78008;       // a reward routine per level index ($6D8CE)
constexpr int prompt_delay_frames = 3 * 655360 * 50 / 7093790; // $6BB9E: three delays before the prompt
// Hints ($69DFA): the level number they are shown in, and their text lists.
constexpr struct {
    int level;
    std::uint32_t texts;
} hints[] = {{8, 0x69AE6}, {11, 0x69B1A}, {18, 0x69B4E}, {20, 0x69B82}, {24, 0x69BB6}, {29, 0x69BEA}, {30, 0x69C1E}};
constexpr std::uint32_t continue_texts[3] = {0x680D0, 0x68091, 0x680B1}; // "CONTINUE GAME", "LEFT BUTTON … RIGHT BUTTON", "YES … NO"
constexpr int balls_at_start = 2;               // $699F8: "3 BALLS REMAINING"
constexpr int bonus_level = 0x20;               // index of the 33rd level (reached by the first secret warp)
// $6B66E: a delay loop of 65,536 passes (about 4.6 frames); "N BALLS
// REMAINING" waits 8 of them ($68846), "GAME OVER" 15 ($6BCE8).
constexpr int balls_message_frames = 8 * 655360 * 50 / 7093790;
constexpr int game_over_frames = 15 * 655360 * 50 / 7093790;
constexpr int map_delay_frames = 655360 * 50 / 7093790 + 1; // one delay before the map ($6B6E0)
constexpr int scores_size = 6144;

// The keyboard as the handler leaves keys in $70BD8 (table $70C9E): letters
// and digits as themselves, Return '[', space '(' and Backspace '''. Indexes
// of GameInput::c64_keys.
struct TypedKey {
    int key;
    char character;
};
constexpr TypedKey typed_keys[] = {
    {0, '\''}, {1, '['},  {9, 'W'},  {10, 'A'}, {12, 'Z'}, {13, 'S'}, {14, 'E'}, {17, 'R'}, {18, 'D'}, {20, 'C'},
    {21, 'F'}, {22, 'T'}, {23, 'X'}, {25, 'Y'}, {26, 'G'}, {28, 'B'}, {29, 'H'}, {30, 'U'}, {31, 'V'}, {33, 'I'},
    {34, 'J'}, {36, 'M'}, {37, 'K'}, {38, 'O'}, {39, 'N'}, {41, 'P'}, {42, 'L'}, {62, 'Q'}, {35, '0'}, {56, '1'},
    {59, '2'}, {8, '3'},  {11, '4'}, {16, '5'}, {19, '6'}, {24, '7'}, {27, '8'}, {32, '9'},
    {55, '?'}, // the slash key: '?' on the original's keyboard map is a shifted key
};

// A list of text pointers ending with 0 ($69F4E prints them in turn).
std::vector<ScreenText> read_text_list(const data::Program& program, std::uint32_t list) {
    std::vector<ScreenText> texts;
    for (std::uint32_t at = list; program.u32(at) != 0; at += 4) texts.push_back(read_text(program, program.u32(at)));
    return texts;
}

// The original's texts: column, row, characters up to $FF.
std::vector<ScreenText> read_texts(const data::Program& program, std::uint32_t at, int count) {
    std::vector<ScreenText> texts;
    for (int i = 0; i < count; ++i) {
        texts.push_back(read_text(program, at));
        at += std::uint32_t(texts.back().text.size()) + 1;
    }
    return texts;
}

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), scores_path_(content / "scores.bin") {
        prompt_ = read_texts(assets_.program, prompt_texts, 4);
        // The records as last saved; the installed file $30 stays as imported.
        scores_ = assets_.scores;
        if (std::filesystem::exists(scores_path_)) {
            auto saved = platform::read_file(scores_path_);
            if (saved.size() == scores_size) scores_.bytes = std::move(saved);
        }
        front_end_ = std::make_unique<FrontEnd>(assets_, scores_);
        sound_ = std::make_unique<sound::GameSound>(assets_);
        // The level load area as the program's image leaves it at start-up,
        // as large as the largest file or map.
        std::size_t area = 0;
        for (const auto& level : assets_.levels)
            area = std::max({area, data::level_cells_at + level.stored_cells + level.padding.size(), data::level_cells_at + level.cells.size()});
        const auto image = assets_.program.bytes(data::level_load_address, std::uint32_t(area));
        load_area_.assign(image.begin(), image.end());
        open_front_end(std::nullopt);
    }

    double step_seconds() const override { return 1.0 / 50; } // one update per PAL frame (game.md)

    void audio(std::span<float> stereo, int rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        sound_->render(stereo, rate);
    }
    double pixel_aspect() const override { return 1.0; }
    bool captures_mouse() const override { return true; } // steered by the mouse counters' movement

    std::vector<app::GameOption> options() const override {
        return {{"Mouse speed", {"Normal", "Slow", "Fast"}, mouse_speed_,
                 "Mouse counts per point the mouse moves: 1, 1/2 or 2. The ball pushes hardest from two "
                 "counts an update on."},
                {"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
                 "Fills the window to its edges, up to 21:9, and shows more of the level left and right. "
                 "The game plays the same."},
                {"Smooth motion", {"Off", "On"}, smooth_ ? 1 : 0,
                 "The ball and the view glide at the display's own rate instead of stepping 50 times a "
                 "second. The game plays the same."}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) mouse_speed_ = std::clamp(value, 0, 2);
        if (index == 1) {
            widescreen_ = value != 0;
            set_canvas(canvas_width_, canvas_height_);
        }
        if (index == 2) smooth_ = value != 0;
    }
    void set_frame_fraction(double fraction) override { frame_fraction_ = std::clamp(fraction, 0.0, 1.0); }

    // Widescreen: as wide as the window's shape allows, from the original's
    // 304 pixels up to 21:9 (the other screens keep their own picture, in
    // the middle of one of the same shape).
    void set_canvas(int width, int height) override {
        canvas_width_ = width;
        canvas_height_ = height;
        int margin = 0;
        if (widescreen_ && width > 0 && height > 0) {
            const double tall = Screen::height;
            const double wide = std::clamp(tall * width / height, double(Screen::width), tall * 21 / 9);
            margin = std::max(0, int(wide - Screen::width) / 2);
        }
        if (margin == margin_) return;
        margin_ = margin;
        if (screen_) screen_->set_margin(margin_);
    }

    void tick(const app::GameInput& raw) override {
        app::GameInput input = raw;
        apply_touch(input);
        before_.valid = false; // (set again by an update of the ball)
        const auto [counts_x, counts_y] = mouse_counts(input);
        const bool left = input.mouse_button || input.fire, right = input.mouse_right || input.space;
        const bool left_pressed = left && !left_held_, right_pressed = right && !right_held_;
        left_held_ = left;
        right_held_ = right;
        const char key = typed_key(input);

        switch (phase_) {
        case Phase::front_end: {
            FrontEndInput in;
            in.mouse_x = counts_x;
            in.mouse_y = counts_y;
            in.left = input.mouse_button; // the space bar types a space here
            in.right = input.mouse_right;
            in.key = key;
            const bool was_naming = front_end_->in_name_entry();
            if (const auto start = front_end_->tick(in)) {
                start_game(*start);
                break;
            }
            if (!was_naming && front_end_->in_name_entry()) sound_->stop_channels(); // $693D8
            if (const int r = front_end_->take_jukebox_request(); r >= 0) {
                sound_->stop_all(); // $709E6
                if (r > 0) {        // $68DE6-$68E62: the title's music
                    song_ = 0;
                    sound_->load_jukebox(r);
                    sound_->start_music(song_);
                }
            }
            if (front_end_->music_runs()) sound_->tick();                            // $70ABE
            break;
        }
        case Phase::message:
            if (--message_frames_ <= 0) after_message();
            break;
        case Phase::waiting:
            move_counters(input, counts_x, counts_y);
            if (wait_for_start(ball_, mouse_)) phase_ = Phase::playing;
            break;
        case Phase::playing:
            before_ = Place{ball_.camera_x, ball_.camera_y, ball_.view_x, ball_.view_y, level_index_, true};
            sound_->tick(); // $68940: the loop's first call
            move_counters(input, counts_x, counts_y);
            update_ball(ball_, pocket_, map_, world(), mouse_);
            if (ball_.clock.seconds != clock_seen_) { // a second ended: $6F270 drew the digits
                clock_seen_ = ball_.clock.seconds;
                panel_seconds_ = std::max(clock_seen_ - 1, 0);
            }
            zero_key_ = key == '0' || (zero_key_ && input.c64_keys[35]);
            view_flash_ = false;
            if (ball_.reward_due || ball_.hint_bought) {
                // The reward ($6D8CE, from the cell rule) and the hint
                // ($69DFA, from the $45 shop) run in the middle of the update:
                // the sounds after them wait for the notice to end.
                const auto at = std::min(ball_.notice_sound_at, ball_.sounds.size());
                for (std::size_t i = 0; i < at; ++i) sound_->effect(ball_.sounds[i]);
                ball_.sounds.erase(ball_.sounds.begin(), ball_.sounds.begin() + std::ptrdiff_t(at));
                const bool reward = std::exchange(ball_.reward_due, false);
                const bool hint = std::exchange(ball_.hint_bought, false);
                if ((reward && give_reward()) || (hint && show_hint())) {
                    sounds_after_notice_ = std::move(ball_.sounds);
                    ball_.sounds.clear();
                    break;
                }
            }
            for (const int n : ball_.sounds) sound_->effect(n); // $70A9C, where the rules call it
            ball_.sounds.clear();
            extra_drawn_ = extra_drawn_ || ball_.kind_running;
            if (ball_.mode == BallMode::lost || ball_.mode == BallMode::warped) {
                ball_lost();
                break;
            }
            // $68A50, before the items: the keyboard handler's other keys.
            if (key == '%') { // $68A78 → $6BB2A: the loop ends as if the ball were lost
                if (ball_.mode == BallMode::falling && !ball_.energy_out) { // the fall's state: its pause and prompt
                    ball_.mode = BallMode::lost;
                    ball_lost();
                } else { // otherwise a restart with one ball less ($6BC7C)
                    sound_->stop_all(); // $6BB34
                    restart();
                }
                break;
            }
            if (key == '\xA7') { // $68ACE: the song's bit 1 (song 0 or 2), started again
                song_ ^= 2;
                sound_->start_music(song_);
            }
            if (key == '&') { // $68AAE: two sounds, then the effects on or off
                sound_->effect(0x17);
                sound_->effect(0x18);
                sound_->toggle_effects();
            }
            if (key == '?') { // $68A36: the '?' key gives up like item 0
                balls_ = 0;
                ball_.mode = BallMode::lost;
                ball_lost();
                break;
            }
            switch (use_items(ball_, pocket_, map_, items_, left, right)) {
            case ItemUse::give_up: // $6B5A8: the ball is lost and no balls are left
                balls_ = 0;
                ball_.mode = BallMode::lost;
                ball_lost();
                break;
            case ItemUse::pause: // $6B612: until the left button is let go, pressed and let go again
                phase_ = Phase::paused;
                pause_stage_ = 0;
                break;
            case ItemUse::overview: // $6B67E: the overview map (the music stops, $709E6)
                sound_->stop_all();
                overview_ = OverviewView{OverviewColours(assets_.program, pocket_.map_depth), 0, false};
                overview_texts_ = game::overview_texts(assets_.program, pocket_.map_depth);
                wait_stage_ = 0;
                wait_frames_ = 0;
                phase_ = Phase::overview;
                break;
            case ItemUse::none:
                break;
            }
            break;
        case Phase::paused:
            sound_->tick(); // $6B612's loops call $70ABE
            if (pause_stage_ == 0 && !left) pause_stage_ = 1;
            else if (pause_stage_ == 1 && left) pause_stage_ = 2;
            else if (pause_stage_ == 2 && !left) phase_ = Phase::playing;
            break;
        case Phase::prompt:
            if (left_pressed) { // continue from this point ($6BBC4)
                continue_after_fall(ball_);
                sound_->start_music(song_); // $6BDCC
                // $6BDF8: the energy gains $280 (over $800: $7FF).
                add_energy();
                --balls_;
                set_balls_message();
                phase_ = Phase::playing;
            } else if (right_pressed) {
                restart();
            }
            break;
        case Phase::fall_pause:
            if (--message_frames_ <= 0) decide_after_fall();
            break;
        case Phase::results:
        case Phase::notice:
        case Phase::continue_offer:
            // $6AB46: wait for the button's release, then (colours flashing,
            // $6B9F4) for a press, then for its release.
            ++wait_frames_;
            if (wait_stage_ == 0 && !left) {
                wait_stage_ = 1;
                wait_frames_ = 0;
                flash_sounds();
            } else if (wait_stage_ == 1 && left) wait_stage_ = 2;
            else if (wait_stage_ == 2 && !left) {
                if (phase_ == Phase::results) next_level();
                else if (phase_ == Phase::notice) finish_notice();
                else start_level(level_index_, true); // $69996 → $686E2
            }
            break;
        case Phase::overview:
            // $6B6D4: the text until the button is let go; a delay ($6B66E);
            // the map, drawn row by row at the original's pace and cut short
            // by a press ($6B7CC); the ball's place; then colour 1 flashes
            // until a press ($6B9F4) and the game goes on after the release.
            ++wait_frames_;
            if (wait_stage_ == 0 && !left) {
                wait_stage_ = 1;
                wait_frames_ = 0;
            } else if (wait_stage_ == 1 && wait_frames_ >= map_delay_frames) {
                wait_stage_ = 2;
                wait_frames_ = 0;
            } else if (wait_stage_ == 2) {
                const int cells = map_.rows * map_.columns;
                overview_.cells_drawn = std::min(cells, int(wait_frames_ * overview_cells_per_frame));
                if (left || overview_.cells_drawn == cells) {
                    overview_.marker = true;
                    wait_stage_ = left ? 4 : 3;
                    wait_frames_ = 0;
                    flash_sounds();
                }
            } else if (wait_stage_ == 3 && left) {
                wait_stage_ = 4;
            } else if (wait_stage_ == 4 && !left) {
                sound_->start_music(song_); // $6B6F0: the song from its start
                phase_ = Phase::playing;
            }
            break;
        case Phase::ending:
            if (left) game_over(); // $6C81E: until the left button; then the game ends as if out of balls
            break;
        }
    }

    platform::Image picture() const override {
        switch (phase_) {
        case Phase::front_end:
            return widened(front_end_->picture());
        case Phase::ending:
            return widened(ending_picture());
        case Phase::continue_offer:
            return widened(continue_picture());
        case Phase::notice:
            if (notice_full_screen_) return widened(hint_picture());
            break;
        default:
            break;
        }
        const auto& level = assets_.levels.at(std::size_t(level_index_));
        screen_->draw(map_, shown_ball(), panel_numbers(), level.name, bar_text_, view_texts(),
                      phase_ == Phase::overview ? &overview_ : nullptr);
        const int width = screen_->picture_width();
        platform::Image image(width, Screen::height);
        const auto& pixels = screen_->pixels();
        // $6AB46: while the results wait, colours 1-7 of the view are $EE0
        // and colour 1 (the new record) flashes.
        auto view_palette = screen_->palette(Screen::bar_lines);
        if (view_flash_) view_palette[0] = amiga::Rgb4(0xFFF); // $6D8EC: a level without a reward
        if ((phase_ == Phase::results || phase_ == Phase::notice) && wait_stage_ >= 1) {
            for (std::size_t i = 1; i <= 7; ++i) view_palette[i] = amiga::Rgb4(0xEE0);
            if (wait_stage_ == 1) view_palette[1] = amiga::Rgb4(results_flash_colour(wait_frames_));
        }
        if (phase_ == Phase::overview && wait_stage_ == 3) view_palette[1] = amiga::Rgb4(results_flash_colour(wait_frames_));
        for (int y = 0; y < Screen::height; ++y) {
            const bool view = y >= Screen::bar_lines && y < Screen::bar_lines + Screen::view_lines;
            const auto& palette = view ? view_palette : screen_->palette(y);
            for (int x = 0; x < width; ++x) {
                const auto i = std::size_t(y) * std::size_t(width) + std::size_t(x);
                image.pixels[i] = pixels[i] < palette.size() ? data::to_color(palette[pixels[i]]) : platform::Color{};
            }
        }
        return image;
    }

    // Smooth motion (an addition): the ball and the view where they are
    // between the last update and the one before, at the display's place
    // in time. Jumps (teleports, a new level, a continue) are shown as they
    // are, and the game's state is never changed.
    Ball shown_ball() const {
        if (!smooth_ || !before_.valid || before_.level != level_index_ || phase_ != Phase::playing) return ball_;
        const auto far = [](Fixed a, Fixed b) { return std::abs(std::int64_t(a) - std::int64_t(b)) > 0x20000; };
        if (far(before_.camera_x, ball_.camera_x) || far(before_.camera_y, ball_.camera_y) || far(before_.view_x, ball_.view_x) ||
            far(before_.view_y, ball_.view_y))
            return ball_;
        // The picture after an update shows the update's result at
        // fraction 1: between the two places for fractions below.
        const double back = 1.0 - frame_fraction_;
        const auto between = [&](Fixed from, Fixed to) { return Fixed(std::llround(double(to) - double(to - from) * back)); };
        Ball b = ball_;
        b.camera_x = between(before_.camera_x, ball_.camera_x);
        b.camera_y = between(before_.camera_y, ball_.camera_y);
        b.view_x = between(before_.view_x, ball_.view_x);
        b.view_y = between(before_.view_y, ball_.view_y);
        return b;
    }

    // A whole-screen picture of the original's (the title, the ending …) in
    // the middle of one of the play screen's widescreen shape.
    platform::Image widened(platform::Image picture) const {
        if (margin_ == 0) return picture;
        const int width = int(double(picture.height) * (Screen::width + 2 * margin_) / Screen::height + 0.5);
        if (width <= picture.width) return picture;
        platform::Image out(width, picture.height);
        for (auto& p : out.pixels) p = platform::Color{0, 0, 0, 255};
        const int left = (width - picture.width) / 2;
        for (int y = 0; y < picture.height; ++y)
            for (int x = 0; x < picture.width; ++x)
                out.pixels[std::size_t(y) * std::size_t(width) + std::size_t(left + x)] =
                    picture.pixels[std::size_t(y) * std::size_t(picture.width) + std::size_t(x)];
        return out;
    }

    bool place(Fixed camera_x, Fixed camera_y, Fixed view_x, Fixed view_y, Fixed speed_x, Fixed speed_y) {
        if (phase_ != Phase::waiting && phase_ != Phase::playing) return false;
        ball_.camera_x = camera_x;
        ball_.camera_y = camera_y;
        ball_.view_x = view_x;
        ball_.view_y = view_y;
        ball_.speed_x = speed_x;
        ball_.speed_y = speed_y;
        phase_ = Phase::playing;
        return true;
    }
    // Quick saves: a level in play or at its start wait (elsewhere the
    // state is empty: not ready). Loading sets the level up as at its
    // start, its music from the beginning, then takes the saved state.
    std::vector<std::uint8_t> write_state() override {
        if (phase_ != Phase::playing && phase_ != Phase::waiting) return {};
        state::Writer ar;
        persist(ar);
        return std::move(ar.bytes);
    }
    void read_state(std::span<const std::uint8_t> bytes) override {
        state::Reader ar(bytes);
        persist(ar);
        ar.finish();
    }
    bool save() override {
        auto bytes = write_state();
        if (bytes.empty()) return false;
        saved_ = std::move(bytes);
        return true;
    }
    bool load() override {
        if (!saved_) return false;
        const auto bytes = *saved_;
        read_state(bytes);
        return true;
    }
    bool has_save() const override { return saved_.has_value(); }

    bool set_money(int money) {
        if (phase_ != Phase::waiting && phase_ != Phase::playing) return false;
        pocket_.money = money;
        return true;
    }
    bool set_energy(int energy) {
        if (phase_ != Phase::waiting && phase_ != Phase::playing) return false;
        pocket_.energy = std::uint16_t(energy);
        return true;
    }
    bool set_clock(int seconds, int hundredths) {
        if (phase_ != Phase::waiting && phase_ != Phase::playing) return false;
        ball_.clock.seconds = seconds;
        ball_.clock.hundredths = hundredths;
        return true;
    }

private:
    template <class A> void persist(A& ar) {
        std::uint32_t version = 1;
        ar(version);
        if (version != 1) A::fail();
        const std::size_t area = load_area_.size();
        ar(level_index_, load_area_, song_, player_name_, from_code_, balls_);
        if constexpr (A::reading) {
            if (level_index_ < 0 || level_index_ >= int(assets_.levels.size()) || load_area_.size() != area) A::fail();
            start_level(level_index_, false);
            set_balls_message();
        }
        ar(panel_seconds_, clock_seen_, zero_key_, mouse_, rest_x_, rest_y_, extra_drawn_, items_, view_flash_);
        ar(left_held_, right_held_, ball_, pocket_, map_);
        bool effects = sound_->effects_on();
        bool playing = phase_ == Phase::playing;
        ar(effects, playing);
        if constexpr (A::reading) {
            sound_->set_effects(effects);
            phase_ = playing ? Phase::playing : Phase::waiting;
        }
    }

    enum class Phase {
        front_end, message, waiting, playing, paused, fall_pause, prompt, results, notice, continue_offer, ending, overview
    };
    enum class AfterMessage { start_wait, game_over, restart };
    enum class AfterNotice { play, full_energy, warp };

    std::vector<ScreenText> view_texts() const {
        switch (phase_) {
        case Phase::prompt: return prompt_;
        case Phase::message: return {message_};
        case Phase::results: return results_;
        case Phase::notice: return notice_full_screen_ ? std::vector<ScreenText>{} : notice_;
        case Phase::overview: return overview_texts_;
        default: return {};
        }
    }

    // The panel's numbers. The clock's digits are drawn when a second ends
    // ($6C888 → $6F270), before the seconds count on, so they show the
    // second before; the level set-up does not draw them, so a new level
    // shows the last level's seconds until its first second ends (the
    // tenths digit follows every update, $6F178).
    PanelNumbers panel_numbers() const {
        PanelNumbers n;
        n.level = level_index_ + 1;
        n.seconds = panel_seconds_;
        n.hundredths = ball_.clock.hundredths;
        n.record = shown_record_;
        n.high_score = shown_high_score_;
        n.score_shown = pocket_.score != 0;
        n.score = pocket_.score;
        n.money = pocket_.money;
        n.keys = pocket_.keys;
        n.speed_bought = ball_.extra_speed >> 8;
        n.shown_energy = pocket_.shown_energy;
        n.kind_drawn = extra_drawn_;
        n.kind_running = ball_.kind_running;
        n.kind_updates = ball_.kind_updates;
        n.balls_left = balls_;
        n.item = items_.selected;
        n.item_count = items_.selected == 6 ? pocket_.bombs : items_.selected == 8 ? pocket_.repair_sets : pocket_.rescues;
        return n;
    }

    const data::World& world() const { return assets_.world(assets_.levels.at(std::size_t(level_index_)).world); }

    // $6867A: a new game from the front end: balls and rescues reset (score
    // and money were cleared when the last game ended), the level the code
    // gave (or the first; the bonus level's index counts as the first,
    // $686C8).
    void start_game(const GameStart& start) {
        ball_.grip = front_end_->read_grip(); // (carried into the first level's start wait)
        ball_.updates_without_read = front_end_->reads_skipped();
        player_name_ = start.name;
        from_code_ = start.from_code;
        pocket_ = Pocket{};
        if (start.magic_map) // $6ADBC: 33 map pieces, the whole overview map
            for (int i = 0; i < 33; ++i) pocket_.add_map_piece();
        balls_ = balls_at_start;
        set_balls_message();
        start_level(start.level_index == bonus_level ? 0 : start.level_index, false);
    }

    void start_level(int index, bool balls_message) {
        level_index_ = index % int(assets_.levels.size());
        const auto& level = assets_.levels.at(std::size_t(level_index_));
        // The animations' counters run on from level to level ($6C106 never
        // resets them); the tile table is rebuilt ($70954).
        auto animation = map_.animation.ready() ? std::move(map_.animation) : TileAnimator(assets_.program);
        map_ = LevelMap::from(level);
        // The file is loaded over whatever the last level files left at
        // $737C0. A map larger than its file (level 28) reads on into that:
        // after level 14 (a 12,288-byte file) its bottom rows are level 14's
        // file bytes, from a fresh start the program's image (abyss).
        std::copy(level.header.begin(), level.header.end(), load_area_.begin());
        std::copy(level.cells.begin(), level.cells.begin() + std::ptrdiff_t(level.stored_cells),
                  load_area_.begin() + std::ptrdiff_t(data::level_cells_at));
        std::copy(level.padding.begin(), level.padding.end(),
                  load_area_.begin() + std::ptrdiff_t(data::level_cells_at + level.stored_cells));
        for (std::size_t i = level.stored_cells; i < map_.cells.size(); ++i)
            map_.cells[i] = load_area_[data::level_cells_at + i];
        map_.animation = std::move(animation);
        map_.animation.set_world(level.world); // $6C0E0
        const MouseCounters keep = ball_.last_mouse;
        const bool grip = ball_.grip; // $70404/$7040A are never reset: the start wait's first read may be skipped
        const int skipped = ball_.updates_without_read;
        ball_ = Ball::at_start(map_);
        ball_.last_mouse = keep;
        ball_.grip = grip;
        ball_.updates_without_read = skipped;
        clock_seen_ = ball_.clock.seconds; // (the panel keeps panel_seconds_)
        screen_ = std::make_unique<Screen>(assets_, world());
        screen_->set_margin(margin_);
        // $686EA/$686F0 (and $6C69E's loads): the world's music, asked for;
        // it starts at the loop's first update after the start wait.
        // $68790: the panel's energy follows the energy (a refilled bar
        // steps once from $800 to $7FF, with sound $11 and its delay).
        settle_energy(ball_, pocket_);
        for (const int n : ball_.sounds) sound_->effect(n);
        ball_.sounds.clear();
        sound_->stop_channels();
        sound_->load_world(level.world);
        sound_->start_music(song_);
        // The panel's record and high score are drawn at set-up only ($6F1A6, $6EDCE).
        shown_record_ = scores_.record(level_index_);
        shown_high_score_ = scores_.high_score();
        // The hourglass text: header +$1C0, or "NO " ($6C78A).
        const auto& h = level.header;
        if (h.at(0x1C0) == 0xFF && h.at(0x1C1) == 0xFF && h.at(0x1C2) == 0xFF && h.at(0x1C3) == 0xFF) {
            bar_text_.clear();
            for (std::uint32_t a = no_limit_text; assets_.program.u8(a) != 0xFF; ++a) bar_text_ += char(assets_.program.u8(a));
        } else {
            bar_text_ = std::string(reinterpret_cast<const char*>(&h.at(0x1C0)), 4);
        }
        // $6882C: after a lost ball the set-up shows "N BALLS REMAINING" (or
        // "LAST BALL") on the cleared view for 8 delays, then the start wait.
        if (balls_message) show_message(balls_message_, balls_message_frames, AfterMessage::start_wait);
        else phase_ = Phase::waiting;
    }

    void show_message(ScreenText text, int frames, AfterMessage then) {
        message_ = std::move(text);
        message_frames_ = frames;
        after_message_ = then;
        phase_ = Phase::message;
    }

    void after_message() {
        if (after_message_ == AfterMessage::game_over) end_game();
        else if (after_message_ == AfterMessage::restart) restart();
        else phase_ = Phase::waiting;
    }

    // $6BC88: the text for the next set-up, from the balls left.
    void set_balls_message() {
        if (balls_ == 0) {
            balls_message_ = read_text(assets_.program, last_ball_text);
        } else {
            balls_message_ = read_text(assets_.program, balls_text);
            balls_message_.text[2] = char('1' + balls_);
        }
    }

    void add_energy() { // $6BDF8
        const int energy = std::int16_t(pocket_.energy) + 0x280;
        pocket_.energy = pocket_.shown_energy = energy > 0x800 ? 0x7FF : energy;
    }

    // $6BB86: the exit leads to the results and the next level, a secret
    // warp to its message and target ($69F8A, $69DC2); out of time shows
    // its text first ($6C94E); a fall waits three delays, then asks with
    // balls left; otherwise the level starts again, and with no balls left
    // the game is over.
    void ball_lost() {
        sound_->stop_all(); // $6BB34: every way the loop ends
        if (ball_.mode == BallMode::warped || ball_.level_done) {
            if (from_code_) { // $6BB48: a game started from a code has no spare balls after its level
                balls_ = 0;
                pocket_.continues = 0;
                set_balls_message();
            }
            add_energy(); // $6BDF8
            if (ball_.mode == BallMode::warped) {
                warp_target_ = secret_warp_target(level_index_, pocket_).value_or(level_index_ + 1);
                // $69F8A writes the target's level number at $67836, inside
                // the list's third text.
                const auto& program = assets_.program;
                std::vector<ScreenText> texts;
                const int number = warp_target_ + 1;
                for (std::uint32_t at = warp_texts; program.u32(at) != 0; at += 4) {
                    const std::uint32_t address = program.u32(at);
                    auto t = read_text(program, address);
                    if (warp_digits >= address + 2 && warp_digits + 1 < address + t.text.size()) {
                        t.text[warp_digits - address] = char('0' + number / 10);
                        t.text[warp_digits - address + 1] = char('0' + number % 10);
                    }
                    texts.push_back(std::move(t));
                }
                show_notice(std::move(texts), AfterNotice::warp);
                return;
            }
            level_finished();
            return;
        }
        if (ball_.clock.out_of_time) {
            show_message(read_text(assets_.program, out_of_time_text), game_over_frames, AfterMessage::restart);
            return;
        }
        if (ball_.mode == BallMode::lost && !ball_.energy_out) { // the fall's state routine ($6BB94)
            message_frames_ = prompt_delay_frames;
            phase_ = Phase::fall_pause;
            return;
        }
        restart(); // $6BC7C: any other state (an empty energy bar): no prompt
    }

    void decide_after_fall() {
        if (!ball_.clock.out_of_time && balls_ != 0) {
            phase_ = Phase::prompt;
            return;
        }
        restart();
    }

    // In-view notices (rewards, the warp) and the hints' own screen, both
    // waiting like the results ($69EF6/$69F1A → $6AB46).
    // $6B9F4: the colours start to flash, with two sounds that $70A32 cuts
    // at once (they still change the player's effect priority and time).
    void flash_sounds() {
        sound_->effect(0x18);
        sound_->effect(0x17);
        sound_->stop_channels();
    }

    void show_notice(std::vector<ScreenText> texts, AfterNotice then, bool full_screen = false) {
        sound_->effect(0x18); // $69F0A/$69F2E, once the button is up
        sound_->stop_channels(); // $6AB46
        notice_ = std::move(texts);
        after_notice_ = then;
        notice_full_screen_ = full_screen;
        wait_stage_ = 0;
        wait_frames_ = 0;
        phase_ = Phase::notice;
    }

    void finish_notice() {
        for (const int n : sounds_after_notice_) sound_->effect(n); // the rest of the update's sounds
        sounds_after_notice_.clear();
        switch (after_notice_) {
        case AfterNotice::play:
            phase_ = Phase::playing;
            break;
        case AfterNotice::full_energy: // $6D940, after the message
            pocket_.shown_energy = 0x800;
            pocket_.energy = 0x7FF;
            phase_ = Phase::playing;
            break;
        case AfterNotice::warp: { // $6C5F4 → $6C68A: the target, the bombs kept ($68B16)
            const int bombs = pocket_.bombs;
            pocket_.clear_for_restart();
            pocket_.bombs = bombs;
            start_level(warp_target_, false);
            break;
        }
        }
    }

    // $6D8CE: the level's reward routine (table $78008). Returns true when a
    // message is shown.
    bool give_reward() {
        const std::uint32_t routine = assets_.program.u32(rewards + std::uint32_t(level_index_) * 4);
        std::uint32_t texts = 0;
        AfterNotice then = AfterNotice::play;
        switch (routine) {
        case 0x6D8F6: // 500 coins
            pocket_.money += 500;
            sound_->effect(5);
            texts = 0x69CA6;
            break;
        case 0x6D914: // 1,500 points, shown as 15.000
            pocket_.score += 1500;
            sound_->effect(5);
            texts = 0x69C8A;
            break;
        case 0x6D932: // full energy, after the message
            sound_->effect(5);
            texts = 0x69C52;
            then = AfterNotice::full_energy;
            break;
        case 0x6D956: // an extra ball
            ++balls_;
            set_balls_message();
            sound_->effect(5);
            texts = 0x69C6E;
            break;
        default: // $6D8EC: colour 0 white for a moment, nothing else
            view_flash_ = true;
            return false;
        }
        show_notice(read_text_list(assets_.program, texts), then);
        return true;
    }

    // $69DFA: the level's hint, on a cleared screen of its own; levels
    // without one show nothing.
    bool show_hint() {
        for (const auto& h : hints)
            if (h.level == level_index_ + 1) {
                show_notice(read_text_list(assets_.program, h.texts), AfterNotice::play, true);
                return true;
            }
        return false;
    }

    // $6BB3C → $6BDD4 → $6C5F4: a level finished.
    void level_finished() {
        if (from_code_) { // $6BB48: a game started from a code has no spare balls after its level
            balls_ = 0;
            pocket_.continues = 0;
            set_balls_message();
        }
        add_energy();
        if (level_index_ == bonus_level) { // no records for the bonus level
            next_level();
            return;
        }
        // $6A344: the time (one second less) into the level's records.
        const int place = scores_.insert(level_index_, player_name_, finished_time(ball_.clock.seconds, ball_.clock.hundredths));
        if (place != 0) save_scores();
        results_ = results_texts(assets_.program, scores_, level_index_, place);
        wait_stage_ = 0;
        wait_frames_ = 0;
        phase_ = Phase::results;
    }

    // $6C644: the next level; after the last one (or with the '0' key held,
    // $6C65A) the ending; after the bonus level the second level.
    void next_level() {
        int next = level_index_ + 1;
        if (next == bonus_level || zero_key_) {
            phase_ = Phase::ending;
            return;
        }
        if (next == bonus_level + 1) next = 1;
        pocket_.clear_for_restart(); // $68B16
        start_level(next, false);
    }

    void restart() {
        --balls_;
        if (balls_ < 0) {
            game_over();
            return;
        }
        set_balls_message();
        pocket_.clear_for_restart();
        pocket_.energy = 0x7FF; // $6D940
        pocket_.shown_energy = 0x800;
        start_level(level_index_, true);
    }

    // $6BCC8: "GAME OVER" in the view for 15 delays, then the end.
    void game_over() {
        show_message(read_text(assets_.program, game_over_text), game_over_frames, AfterMessage::game_over);
    }

    // $6BD24 on: the score into the hall of fame ($6A300), score and money
    // cleared, then a bought extra ball continues the game on this level
    // ($69996) or the title shows the hall of fame with the place.
    void end_game() {
        const int place = scores_.insert(data::ScoreTable::hall_of_fame, player_name_, std::uint32_t(pocket_.score));
        if (place != 0) save_scores();
        const int continues = pocket_.continues;
        pocket_ = Pocket{};
        balls_ = balls_at_start;
        set_balls_message();
        from_code_ = false;
        if (continues > 0) {
            pocket_.continues = continues - 1;
            wait_stage_ = 0;
            wait_frames_ = 0;
            phase_ = Phase::continue_offer;
            return;
        }
        open_front_end(place);
    }

    // $68EA0: the title with its music (song set 8 after a game that left a
    // place in the hall of fame).
    void open_front_end(std::optional<int> place) {
        front_end_->open(place);
        front_end_->set_read_gate(ball_.grip, ball_.updates_without_read);
        sound_->stop_all();
        sound_->load_title(place.has_value());
        song_ = 0; // $68EAA
        sound_->start_music(song_);
        phase_ = Phase::front_end;
    }

    void save_scores() {
        try {
            platform::write_file(scores_path_, scores_.bytes);
        } catch (const std::exception&) {
            // The records stay for this session; nothing else depends on the file.
        }
    }

    // $69F1A: a hint on the cleared bank through the title's copper list
    // (origin $18FFC + $644: x 8 × column, line 8 + 8 × row), in the
    // colours the registers hold (the panel's, as for the continue screen).
    platform::Image hint_picture() const {
        std::vector<std::uint8_t> pixels(std::size_t(FrontEnd::width) * FrontEnd::height, 0);
        const Canvas canvas{pixels.data(), FrontEnd::width, FrontEnd::height};
        for (const auto& t : notice_) print_at(canvas, world().bank, 0, 8, t);
        const auto& palette = screen_->palette(0);
        platform::Image image(FrontEnd::width, FrontEnd::height);
        for (std::size_t i = 0; i < pixels.size(); ++i) image.pixels[i] = data::to_color(palette[pixels[i]]);
        return image;
    }

    // $69996: "CONTINUE GAME" on the cleared bank shown through the title's
    // copper list (320 x 200), in the colours the panel left in the
    // registers.
    platform::Image continue_picture() const {
        std::vector<std::uint8_t> pixels(std::size_t(FrontEnd::width) * FrontEnd::height, 0);
        const Canvas canvas{pixels.data(), FrontEnd::width, FrontEnd::height};
        const auto& font = world().bank;
        for (int i = 0; i < 3; ++i) print_at(canvas, font, 32, 8, read_text(assets_.program, continue_texts[i], i == 0 ? 0 : 2));
        const auto& palette = screen_->palette(0);
        platform::Image image(FrontEnd::width, FrontEnd::height);
        for (std::size_t i = 0; i < pixels.size(); ++i) image.pixels[i] = data::to_color(palette[pixels[i]]);
        return image;
    }

    // $6C7F2: the ending picture (file $41) through the title's copper list.
    platform::Image ending_picture() const {
        const auto& picture = assets_.pictures.at(1).picture;
        platform::Image image(FrontEnd::width, FrontEnd::height);
        for (int y = 0; y < FrontEnd::height; ++y)
            for (int x = 0; x < FrontEnd::width; ++x) {
                const bool inside = x < picture.width && y < picture.height;
                const auto index = inside ? picture.pixels[std::size_t(y) * std::size_t(picture.width) + std::size_t(x)] : 0;
                image.pixels[std::size_t(y) * FrontEnd::width + std::size_t(x)] = data::to_color(picture.palette.at(index));
            }
        return image;
    }

    // The captured mouse's movement as Amiga mouse counts (fractions carry
    // over). The counters are 8 bits and read as differences, so over 127
    // counts in one update would read as the opposite direction; the move
    // is limited to 100.
    // Touch screens (an addition): a finger's movement moves the mouse
    // counters, a short tap is a left click, a tap with two fingers a right
    // click, and a finger held still is the left button held. While a finger
    // touches, the host's mouse (which follows the first finger, its button
    // held) is not used.
    void apply_touch(app::GameInput& input) {
        static constexpr float slop = 4.0f;    // picture pixels a tap may wander
        static constexpr int tap_updates = 15; // longer and still: a held button
        auto& t = touch_;
        if (!input.fingers.empty()) {
            const auto& f = input.fingers.front();
            if (!t.down) t = Touch{true, f.x, f.y, f.x, f.y};
            input.mouse_dx = (f.x - t.x) * touch_gain;
            input.mouse_dy = (f.y - t.y) * touch_gain;
            t.x = f.x;
            t.y = f.y;
            t.moved = t.moved || std::abs(f.x - t.start_x) > slop || std::abs(f.y - t.start_y) > slop;
            t.fingers = std::max(t.fingers, int(input.fingers.size()));
            ++t.updates;
            input.mouse_button = !t.moved && t.fingers == 1 && t.updates >= tap_updates;
            input.mouse_right = false;
        } else if (t.down) {
            if (!t.moved && t.updates < tap_updates) (t.fingers >= 2 ? tap_right_ : tap_left_) = 2;
            t = Touch{};
            input.mouse_dx = input.mouse_dy = 0;
            input.mouse_button = input.mouse_right = false;
        }
        if (tap_left_ > 0) --tap_left_, input.mouse_button = true;
        if (tap_right_ > 0) --tap_right_, input.mouse_right = true;
    }

    std::pair<int, int> mouse_counts(const app::GameInput& input) {
        static constexpr float counts_per_point[3] = {1.0f, 0.5f, 2.0f};
        const float scale = counts_per_point[mouse_speed_];
        rest_x_ += input.mouse_dx * scale;
        rest_y_ += input.mouse_dy * scale;
        const int dx = std::clamp(int(rest_x_), -100, 100), dy = std::clamp(int(rest_y_), -100, 100);
        rest_x_ -= float(int(rest_x_));
        rest_y_ -= float(int(rest_y_));
        return {dx, dy};
    }

    // The original counts the mouse in JOY0DAT; here the counts move the
    // counters, and the cursor keys add two counts an update (a host
    // convenience; the original has no keys for it).
    void move_counters(const app::GameInput& input, int dx, int dy) {
        if (input.left) dx -= 2;
        if (input.right) dx += 2;
        if (input.up) dy -= 2;
        if (input.down) dy += 2;
        mouse_.x = std::uint8_t(mouse_.x + dx);
        mouse_.y = std::uint8_t(mouse_.y + dy);
    }

    // A key pressed this update, as the game's keyboard handler gives it.
    char typed_key(const app::GameInput& input) {
        char key = 0;
        for (const auto& k : typed_keys) {
            const auto i = std::size_t(k.key);
            if (input.c64_keys[i] && !keys_held_[i]) key = k.character;
        }
        // The original's Esc ('%'), F1 ('§') and F2 ('&'): Tab, F1 and F3
        // here (Esc and F2 belong to the host). The digit keys also count as
        // F1/F3 on the host, so those are left out.
        const bool f1 = input.f1 && !input.c64_keys[56], f3 = input.f3 && !input.c64_keys[8];
        if (f1 && !f1_held_) key = '\xA7';
        if (f3 && !f3_held_) key = '&';
        f1_held_ = f1;
        f3_held_ = f3;
        if (input.c64_keys[63] && !keys_held_[63]) key = '%';
        // The space bar (also the fire button) types a space.
        const bool space = input.fire && !input.c64_keys[12];
        if (space && !space_held_) key = '(';
        space_held_ = space;
        keys_held_ = input.c64_keys;
        return key;
    }

    data::Assets assets_;
    std::filesystem::path scores_path_;
    data::ScoreTable scores_;
    std::unique_ptr<FrontEnd> front_end_;
    std::unique_ptr<sound::GameSound> sound_;
    std::vector<ScreenText> prompt_;
    std::string player_name_;
    bool from_code_{};
    int level_index_{};
    LevelMap map_;
    Ball ball_;
    Pocket pocket_;
    std::unique_ptr<Screen> screen_;
    std::string bar_text_;
    Phase phase_{Phase::front_end};
    int balls_{balls_at_start};
    ScreenText balls_message_;
    ScreenText message_;
    int message_frames_{};
    AfterMessage after_message_{};
    std::vector<ScreenText> results_;
    std::vector<ScreenText> notice_;
    AfterNotice after_notice_{};
    std::vector<int> sounds_after_notice_; // the rest of the update that showed the notice
    std::vector<std::uint8_t> load_area_;  // $737C0 on: the level files as loaded one over another
    int song_{};                           // $719A8: the song $70A74 starts (bit 1 flipped by '§')
    int panel_seconds_{}, clock_seen_{};   // the seconds $6F270 last drew, and the clock then
    bool notice_full_screen_{};
    int warp_target_{};
    bool view_flash_{};
    OverviewView overview_;
    std::vector<ScreenText> overview_texts_;
    data::ScoreTable::Time shown_record_{};
    int shown_high_score_{};
    int wait_stage_{}, wait_frames_{};
    bool zero_key_{};
    MouseCounters mouse_;
    float rest_x_{}, rest_y_{}; // movement short of a whole count
    int mouse_speed_{};
    bool extra_drawn_{}; // the panel's extra bar has been drawn (a ball was bought)
    Items items_;
    int pause_stage_{};
    bool left_held_{}, right_held_{};
    std::array<bool, 64> keys_held_{};
    bool space_held_{};
    bool f1_held_{}, f3_held_{};
    std::optional<std::vector<std::uint8_t>> saved_; // the quick save in memory
    bool widescreen_{}, smooth_{};
    int margin_{}, canvas_width_{}, canvas_height_{};
    double frame_fraction_{1.0};
    struct Place {
        Fixed camera_x{}, camera_y{}, view_x{}, view_y{};
        int level{-1};
        bool valid{};
    } before_; // the ball's place before the last update (smooth motion)
    struct Touch {
        bool down{};
        float x{}, y{}, start_x{}, start_y{};
        bool moved{};
        int fingers{}, updates{};
    } touch_;
    int tap_left_{}, tap_right_{};               // updates a tap still holds its button
    static constexpr float touch_gain = 1.0f;    // mouse points per picture pixel of finger travel
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

bool place_ball(app::GameModule& game, std::int32_t camera_x, std::int32_t camera_y, std::int32_t view_x, std::int32_t view_y,
                std::int32_t speed_x, std::int32_t speed_y) {
    auto* module = dynamic_cast<Module*>(&game);
    return module && module->place(camera_x, camera_y, view_x, view_y, speed_x, speed_y);
}

bool set_money(app::GameModule& game, int money) {
    auto* module = dynamic_cast<Module*>(&game);
    return module && module->set_money(money);
}

bool set_energy(app::GameModule& game, int energy) {
    auto* module = dynamic_cast<Module*>(&game);
    return module && module->set_energy(energy);
}

bool set_clock(app::GameModule& game, int seconds, int hundredths) {
    auto* module = dynamic_cast<Module*>(&game);
    return module && module->set_clock(seconds, hundredths);
}

} // namespace rocknroll::game
