#include "rock_n_roll_amiga/game/module.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"
#include "rock_n_roll_amiga/game/ball.hpp"
#include "rock_n_roll_amiga/game/level_map.hpp"
#include "rock_n_roll_amiga/game/screen.hpp"
#include <algorithm>
#include <string>

namespace rocknroll::game {
namespace {

constexpr std::uint32_t prompt_texts = 0x6816E;  // four lines for the prompt after a fall ($6BBA4)
constexpr std::uint32_t no_limit_text = 0x6B10C; // "NO " beside the hourglass when there is no time limit
constexpr int balls_at_start = 2;               // $699F8: "3 BALLS REMAINING"

// The original's texts: column, row, characters up to $FF.
std::vector<std::string> read_texts(const data::Program& program, std::uint32_t at, int count) {
    std::vector<std::string> texts;
    for (int i = 0; i < count; ++i) {
        std::string text;
        while (program.u8(at) != 0xFF) text += char(program.u8(at++));
        ++at;
        texts.push_back(text);
    }
    return texts;
}

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content) : assets_(data::load_assets(content)) {
        prompt_ = read_texts(assets_.program, prompt_texts, 4);
        start_level(0);
    }

    double step_seconds() const override { return 1.0 / 50; } // one update per PAL frame (game.md)
    double pixel_aspect() const override { return 1.0; }
    bool captures_mouse() const override { return true; } // steered by the mouse counters' movement

    std::vector<app::GameOption> options() const override {
        return {{"Mouse speed", {"Normal", "Slow", "Fast"}, mouse_speed_,
                 "Mouse counts per point the mouse moves: 1, 1/2 or 2. The ball pushes hardest from two "
                 "counts an update on."}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) mouse_speed_ = std::clamp(value, 0, 2);
    }

    void tick(const app::GameInput& input) override {
        move_mouse(input);
        const bool left = input.mouse_button || input.fire, right = input.mouse_right || input.space;
        const bool left_pressed = left && !left_held_, right_pressed = right && !right_held_;
        left_held_ = left;
        right_held_ = right;

        switch (phase_) {
        case Phase::waiting:
            if (wait_for_start(ball_, mouse_)) phase_ = Phase::playing;
            break;
        case Phase::playing:
            update_ball(ball_, pocket_, map_, world(), mouse_);
            extra_drawn_ = extra_drawn_ || ball_.kind_running;
            if (ball_.mode == BallMode::lost || ball_.mode == BallMode::warped) {
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
            case ItemUse::overview: // $6B67E: the overview map (not rebuilt yet)
            case ItemUse::none:
                break;
            }
            break;
        case Phase::paused:
            if (pause_stage_ == 0 && !left) pause_stage_ = 1;
            else if (pause_stage_ == 1 && left) pause_stage_ = 2;
            else if (pause_stage_ == 2 && !left) phase_ = Phase::playing;
            break;
        case Phase::prompt:
            if (left_pressed) { // continue from this point ($6BBC4)
                continue_after_fall(ball_);
                // $6BDF8: the energy gains $280 (over $800: $7FF).
                const int energy = std::int16_t(pocket_.energy) + 0x280;
                pocket_.energy = pocket_.shown_energy = energy > 0x800 ? 0x7FF : energy;
                --balls_;
                phase_ = Phase::playing;
            } else if (right_pressed) {
                restart();
            }
            break;
        }
    }

    platform::Image picture() const override {
        const auto& level = assets_.levels.at(std::size_t(level_index_));
        std::vector<std::string_view> texts;
        if (phase_ == Phase::prompt)
            for (const auto& t : prompt_) texts.push_back(t);
        screen_->draw(map_, ball_, panel_numbers(), level.name, bar_text_, texts);
        platform::Image image(Screen::width, Screen::height);
        const auto& pixels = screen_->pixels();
        for (int y = 0; y < Screen::height; ++y) {
            const auto& palette = screen_->palette(y);
            for (int x = 0; x < Screen::width; ++x) {
                const auto i = std::size_t(y) * Screen::width + std::size_t(x);
                image.pixels[i] = pixels[i] < palette.size() ? data::to_color(palette[pixels[i]]) : platform::Color{};
            }
        }
        return image;
    }

private:
    enum class Phase { waiting, playing, paused, prompt };

    // The panel's numbers. The clock's digits are drawn when a second ends
    // ($6C888 → $6F270), before the seconds count on, so they show the
    // second before (the tenths digit follows every update, $6F178).
    PanelNumbers panel_numbers() const {
        PanelNumbers n;
        n.level = level_index_ + 1;
        n.seconds = std::max(ball_.clock.seconds - 1, 0);
        n.hundredths = ball_.clock.hundredths;
        n.record = assets_.scores.record(level_index_);
        n.high_score = assets_.scores.high_score();
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

    void start_level(int index) {
        level_index_ = index % int(assets_.levels.size());
        const auto& level = assets_.levels.at(std::size_t(level_index_));
        map_ = LevelMap::from(level);
        const MouseCounters keep = ball_.last_mouse;
        ball_ = Ball::at_start(map_);
        ball_.last_mouse = keep;
        screen_ = std::make_unique<Screen>(assets_, world());
        // The hourglass text: header +$1C0, or "NO " ($6C78A).
        const auto& h = level.header;
        if (h.at(0x1C0) == 0xFF && h.at(0x1C1) == 0xFF && h.at(0x1C2) == 0xFF && h.at(0x1C3) == 0xFF) {
            bar_text_.clear();
            for (std::uint32_t a = no_limit_text; assets_.program.u8(a) != 0xFF; ++a) bar_text_ += char(assets_.program.u8(a));
        } else {
            bar_text_ = std::string(reinterpret_cast<const char*>(&h.at(0x1C0)), 4);
        }
        phase_ = Phase::waiting;
    }

    // $6BB86: the exit leads to the next level, a secret warp to its target
    // ($69F8A); a fall with balls left asks; otherwise the level starts
    // again, and with no balls left the game starts over.
    void ball_lost() {
        if (ball_.mode == BallMode::warped) {
            start_level(secret_warp_target(level_index_, pocket_).value_or(level_index_ + 1));
            return;
        }
        if (ball_.level_done) {
            start_level(level_index_ + 1);
            return;
        }
        const bool fell = !ball_.clock.out_of_time;
        if (fell && balls_ != 0) {
            phase_ = Phase::prompt;
            return;
        }
        restart();
    }

    void restart() {
        --balls_;
        if (balls_ < 0) { // game over: back to the first level with nothing ($6BCC8)
            balls_ = balls_at_start;
            pocket_ = Pocket{};
            start_level(0);
            return;
        }
        pocket_.clear_for_restart();
        pocket_.energy = 0x7FF; // $6D940
        pocket_.shown_energy = 0x800;
        start_level(level_index_);
    }

    // The original counts the mouse in JOY0DAT; here the captured mouse's
    // movement moves the counters (fractions carry over), and the cursor
    // keys add two counts an update (a host convenience; the original has
    // no keys for it). The counters are 8 bits and read as differences, so
    // over 127 counts in one update would read as the opposite direction;
    // the move is limited to 100 (no such wrap from a fast flick).
    void move_mouse(const app::GameInput& input) {
        static constexpr float counts_per_point[3] = {1.0f, 0.5f, 2.0f};
        const float scale = counts_per_point[mouse_speed_];
        rest_x_ += input.mouse_dx * scale;
        rest_y_ += input.mouse_dy * scale;
        int dx = std::clamp(int(rest_x_), -100, 100), dy = std::clamp(int(rest_y_), -100, 100);
        rest_x_ -= float(int(rest_x_));
        rest_y_ -= float(int(rest_y_));
        if (input.left) dx -= 2;
        if (input.right) dx += 2;
        if (input.up) dy -= 2;
        if (input.down) dy += 2;
        mouse_.x = std::uint8_t(mouse_.x + dx);
        mouse_.y = std::uint8_t(mouse_.y + dy);
    }

    data::Assets assets_;
    std::vector<std::string> prompt_;
    int level_index_{};
    LevelMap map_;
    Ball ball_;
    Pocket pocket_;
    std::unique_ptr<Screen> screen_;
    std::string bar_text_;
    Phase phase_{Phase::waiting};
    int balls_{balls_at_start};
    MouseCounters mouse_;
    float rest_x_{}, rest_y_{}; // movement short of a whole count
    int mouse_speed_{};
    bool extra_drawn_{}; // the panel's extra bar has been drawn (a ball was bought)
    Items items_;
    int pause_stage_{};
    bool left_held_{}, right_held_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace rocknroll::game
