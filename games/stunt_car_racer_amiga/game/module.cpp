#include "stunt_car_racer_amiga/game/module.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/cockpit.hpp"
#include "stunt_car_racer_amiga/data/scenery.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
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
// The speed bar: the forward speed less $1100, times $B700 >> 23 (0-127).
constexpr int speed_bar_floor = 0x1100, speed_bar_gain = 0xB700;

class Module final : public app::GameModule {
  public:
    explicit Module(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), circuits_(data::load_circuits(assets_.program)),
          scenery_(data::load_scenery(assets_.program)), art_(data::load_cockpit_art(assets_.program)),
          palette_(render::race_palette(assets_.program)) {
        start_race();
    }

    // The game counts 0.13 s of race time a frame (its lap timer adds $13
    // hundredths each frame, $5DF2E); the original draws about 7.7 frames a
    // second on an A500, which agrees.
    double step_seconds() const override { return 0.13; }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        race::Controls controls;
        controls.accelerate = input.up;
        controls.brake = input.down;
        controls.left = input.left;
        controls.right = input.right;
        controls.fire = input.fire;
        // After PRESS FIRE, a new press starts the practice again.
        if (race_->state().phase == race::Phase::over) {
            if (input.fire && !fire_held_) start_race();
            fire_held_ = input.fire;
            return;
        }
        fire_held_ = input.fire;
        race_->step(controls);
        const auto& car = race_->player();
        if (car.recovery) {
            chains_ = chains_down, pull_ = chain_first_pull;
        } else if (chains_ != chains_gone) {
            chains_ -= pull_;
            pull_ += chain_first_pull;
        }
    }

    platform::Image picture() const override {
        const auto& car = race_->player();
        const auto& state = race_->state();
        render::Camera camera;
        camera.x = car.x / 4096.0;
        camera.z = car.z / 4096.0;
        camera.eye = car.eye;
        camera.yaw = std::uint16_t(car.yaw);
        camera.pitch = car.view_pitch;
        camera.roll = car.view_roll;
        const auto view = render::draw_race_view(circuits_, scenery_, race_->track(), car.segment, camera);
        render::Cockpit cockpit;
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
    void start_race() {
        race_.emplace(assets_.program, circuits_, 0);
        chains_ = chains_down, pull_ = chain_first_pull;
        race_->step({}); // the crane lowers the car
    }

    std::optional<race::Race> race_;
    bool fire_held_ = false;
    int chains_ = chains_down, pull_ = chain_first_pull;
    int vhs_ = 0;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) { return std::make_unique<Module>(content); }

} // namespace stuntcar::game
