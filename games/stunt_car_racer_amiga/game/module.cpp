#include "stunt_car_racer_amiga/game/module.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/cockpit.hpp"
#include "stunt_car_racer_amiga/data/scenery.hpp"
#include "stunt_car_racer_amiga/data/front_end.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/game/front_end.hpp"
#include "stunt_car_racer_amiga/race/race.hpp"
#include "stunt_car_racer_amiga/render/race_screen.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"
#include <algorithm>
#include <optional>

namespace stuntcar::game {
namespace {

// The crane's chains: down at $B0 while it holds the car, then pulled up
// faster each frame (8, 16, 24, 32) and gone at $60.
constexpr int chains_down = 0xB0, chains_gone = 0x60, chain_first_pull = 8;
// The opponent is drawn while 10 <= distance < $C80 and ahead ($67AA6); its
// rear axle $40 behind its place.
constexpr int opponent_near = 10, opponent_far = 0xC80, rear_axle = 0x40;
// Tab (the key matrix's RUN/STOP) leaves a league race.
constexpr std::size_t quit_key = 63;
// The PAL vertical blank, which turns the wheels.
constexpr double vertical_blanks_per_second = 50;
// The speed bar: the forward speed less $1100, times $B700 >> 23 (0-127).
constexpr int speed_bar_floor = 0x1100, speed_bar_gain = 0xB700;

class Module final : public app::GameModule {
  public:
    explicit Module(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), circuits_(data::load_circuits(assets_.program)),
          scenery_(data::load_scenery(assets_.program)), art_(data::load_cockpit_art(assets_.program)),
          palette_(render::race_palette(assets_.program)), front_art_(data::load_front_end_art(assets_.program)),
          front_(assets_.program, front_art_, random_, [this](render::FrontEndCanvas& c, int track, int view) {
              // The circuit drawn into the stadium picture's window, where
              // the race view goes ($6A590: x 32, y 16).
              const auto v = render::draw_track_picture(assets_.program, circuits_,
                                                        circuits_.tracks.at(std::size_t(track)), view);
              c.overlay(v.pixels.data(), render::view_width, render::view_height, 32, 16, render::View::untouched);
          }) {}

    // The game counts 0.13 s of race time a frame (its lap timer adds $13
    // hundredths each frame, $5DF2E); the original draws about 7.7 frames a
    // second on an A500, which agrees. The menus run at the 50 Hz frame.
    double step_seconds() const override { return race_ ? 0.13 : 1.0 / vertical_blanks_per_second; }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        if (!race_) {
            front_.tick(input);
            if (const auto request = front_.take_race()) start_race(*request);
            return;
        }
        ++ticks_;
        race::Controls controls;
        controls.accelerate = input.up;
        controls.brake = input.down;
        controls.left = input.left;
        controls.right = input.right;
        controls.fire = input.fire;
        // Touch: steering bottom left, the accelerator bottom right. While
        // the game waits for fire (PRESS FIRE after the crane, or at the
        // end) the accelerator gives it, as there is no fire button.
        bool touch_accelerate = false;
        for (const auto& v : input.touch_controls) {
            if (v.id == "left") controls.left = controls.left || v.held;
            if (v.id == "right") controls.right = controls.right || v.held;
            if (v.id == "accelerate") touch_accelerate = v.held;
        }
        if (touch_accelerate) {
            const auto& c = race_->player();
            const bool waiting = race_->state().phase == race::Phase::over || (c.recovery && c.launched);
            (waiting ? controls.fire : controls.accelerate) = true;
        }
        // Esc in the original leaves the race at once, lost, the opponent
        // holding the fastest lap ($5D62C); here it is Tab (Esc opens the
        // pause menu).
        if (input.c64_keys[quit_key] && race_->state().league && race_->state().phase != race::Phase::over) {
            finish_race(true);
            return;
        }
        // After PRESS FIRE, a press goes back to the front end.
        if (race_->state().phase == race::Phase::over) {
            if (controls.fire && !fire_held_) finish_race();
            fire_held_ = controls.fire;
            return;
        }
        fire_held_ = controls.fire;
        race_->step(controls);
        spin_wheels(0);
        // The player's completed laps, for the track records.
        const auto laps = race_->state().cars[0].laps;
        if (laps > laps_seen_) {
            if (laps >= 2) lap_times_.push_back(race_->state().last_lap);
            laps_seen_ = laps;
        }
        const auto& car = race_->player();
        if (car.recovery) {
            chains_ = chains_down, pull_ = chain_first_pull;
        } else if (chains_ != chains_gone) {
            chains_ -= pull_;
            pull_ += chain_first_pull;
        }
    }

    platform::Image picture() const override {
        if (!race_) return front_.picture();
        const auto& car = race_->player();
        const auto& state = race_->state();
        render::Camera camera;
        camera.x = car.x / 4096.0;
        camera.z = car.z / 4096.0;
        camera.eye = car.eye;
        camera.yaw = std::uint16_t(car.yaw);
        camera.pitch = car.view_pitch;
        camera.roll = car.view_roll;
        std::optional<render::Opponent> opponent;
        if (state.league) {
            const auto& o = state.opponent;
            if (o.distance >= opponent_near && o.distance < opponent_far && std::int8_t(car.opponent_flags) >= 0) {
                // Corner lifts above the road under each wheel.
                const int left = o.road[0], right = o.road[1], middle = o.road[2], half = (right - left) / 2;
                opponent = render::Opponent{o.segment, (int(o.progress) - rear_axle) / 256.0, double(o.lane),
                                            {double(o.lift[0] - left), double(o.lift[1] - right),
                                             double(o.lift[2] - (middle - half)), double(o.lift[3] - (middle + half))}};
            }
        }
        const auto beside = !car.off_track          ? render::Beside::no
                            : car.across_centre < 0 ? render::Beside::left
                                                    : render::Beside::right;
        const auto view =
            render::draw_race_view(circuits_, scenery_, race_->track(), car.segment, camera, opponent, beside);
        render::Cockpit cockpit;
        cockpit.wheel_frame = spin_.frame;
        cockpit.left_wheel_y = state.wheel_rows[0];
        cockpit.right_wheel_y = state.wheel_rows[1];
        cockpit.distance = state.distance_shown;
        if (state.league) cockpit.boost_left = race::decimal(car.boost_left);
        if (chains_ != chains_gone) cockpit.chain_bottom = chains_;
        // As the race ends only PRESS FIRE shows, flashing white with the
        // countdown ($5E508).
        const auto countdown = state.finish_countdown;
        const bool quiet = countdown && (std::int8_t(countdown) < 0 || countdown < 3) && car.launch_message != 0x3C;
        if (car.launch_flag && !quiet)
            cockpit.message = data::decode_message(assets_.program, car.launch_message, car.launch_message_length);
        cockpit.message_white = state.flashing;
        cockpit.lap = state.cars[0].laps;
        const auto shown = [](const race::Time& t, bool hundredths) {
            return render::Cockpit::Time{race::decimal(t.minutes), race::decimal(t.seconds), race::decimal(t.hundredths), hundredths};
        };
        if (state.cars[0].laps) cockpit.lap_time = state.lap_time_hold ? shown(state.last_lap, true) : shown(state.cars[0].lap_time, false);
        if (state.best_shown) cockpit.lower_time = shown(state.best_lap, true);
        cockpit.flag_lamp = state.flag_lamp;
        cockpit.right_lamp = state.right_lamp;
        cockpit.intact_marks = state.intact_marks;
        cockpit.cracking = state.cracking;
        for (const auto& p : state.crack.pixels) cockpit.crack.push_back({p.x, p.y, p.colour});
        for (const auto& s : state.smoke.sparks) cockpit.sparks.push_back({s.x, s.y});
        for (const auto& p : state.smoke.puffs) cockpit.smoke.push_back({p.x, p.y, p.size});
        int bar = car.body_velocity[2] - speed_bar_floor;
        bar = bar < 0 ? 0 : int((std::uint32_t(bar) * speed_bar_gain) >> 23);
        cockpit.speed_bar = std::min(bar, 127);
        cockpit.boost = car.boosting;
        return render::draw_race_screen(art_, view, palette_, cockpit);
    }

    // Touch buttons on a phone or tablet without a gamepad: left and right
    // at the bottom left, the accelerator at the bottom right.
    void set_input_capabilities(platform::InputCapabilities caps) override { input_caps_ = caps; }
    std::vector<platform::TouchControl> touch_controls() const override {
        if (!input_caps_.touchscreen || !input_caps_.mobile || input_caps_.gamepad) return {};
        using Icon = platform::ControlIcon;
        const auto button = [](const char* id, const char* label, Icon icon, float x, float y, float width, float height) {
            platform::TouchControl c;
            c.id = id, c.label = label, c.kind = platform::TouchControl::Kind::button, c.icon = icon;
            c.x = x, c.y = y, c.width = width, c.height = height;
            return c;
        };
        return {button("left", "LEFT", Icon::arrow_left, .02f, .70f, .15f, .26f),
                button("right", "RIGHT", Icon::arrow_right, .19f, .70f, .15f, .26f),
                button("accelerate", "GAS", Icon::accelerator, .81f, .62f, .17f, .34f)};
    }

    // The wheels turn on the 50 Hz vertical blank, between the race's frames.
    void set_frame_fraction(double fraction) override {
        if (race_) spin_wheels(fraction);
    }

    std::vector<app::GameOption> options() const override { return {app::screen_filter_option(vhs_)}; }
    void set_option(std::size_t index, int value) override {
        if (index == 0) vhs_ = value;
    }
    int vhs_level() const override { return vhs_; }

  private:
    data::Assets assets_;
    data::Circuits circuits_;
    data::Scenery scenery_;
    data::CockpitArt art_;
    render::Palette palette_;
    data::FrontEndArt front_art_;
    race::Random random_; // shared by the menus and the races, as in the game
    FrontEnd front_;

    void start_race(const RaceRequest& r) {
        race_.emplace(assets_.program, circuits_, r.track, r.super_league, r.opponent, random_, r.roll_bar_marks);
        chains_ = chains_down, pull_ = chain_first_pull;
        race_->step({}); // the crane lowers the car
        ticks_ = blanks_ = 0;
        fire_held_ = true;
        lap_times_.clear();
        laps_seen_ = 0;
    }

    // Back to the front end with how the race went.
    void finish_race(bool quit = false) {
        const auto& state = race_->state();
        RaceResult result;
        result.won = state.flag_lamp && !quit;
        result.fastest_lap = state.right_lamp && !quit;
        result.quit = quit;
        result.wrecked = state.wrecked;
        result.roll_bar_marks = state.intact_marks;
        result.laps = lap_times_;
        result.finished = state.cars[0].laps >= state.laps_to_finish && !quit;
        random_ = state.random;
        race_.reset();
        front_.race_over(result);
    }

    // Runs the vertical blanks up to the time shown (6.5 to a race frame).
    void spin_wheels(double fraction) {
        const auto due = std::int64_t((double(ticks_) + fraction) * step_seconds() * vertical_blanks_per_second);
        for (; blanks_ < due; ++blanks_) spin_.vertical_blank(race_->player());
    }

    std::optional<race::Race> race_;
    bool fire_held_ = false;
    platform::InputCapabilities input_caps_{};
    int chains_ = chains_down, pull_ = chain_first_pull;
    int vhs_ = 0;
    race::WheelSpin spin_;
    std::vector<race::Time> lap_times_;
    int laps_seen_ = 0;
    std::int64_t ticks_ = 0, blanks_ = 0;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) { return std::make_unique<Module>(content); }

} // namespace stuntcar::game
