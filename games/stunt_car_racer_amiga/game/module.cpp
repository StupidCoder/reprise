#include "stunt_car_racer_amiga/game/module.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/cockpit.hpp"
#include "stunt_car_racer_amiga/data/scenery.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include "stunt_car_racer_amiga/race/race.hpp"
#include "stunt_car_racer_amiga/render/race_screen.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"
#include <algorithm>

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
          palette_(render::race_palette(assets_.program)), race_(assets_.program, circuits_, 0) {}

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
        race_.step(controls);
        const auto& car = race_.player();
        if (car.recovery) {
            chains_ = chains_down, pull_ = chain_first_pull;
        } else if (chains_ != chains_gone) {
            chains_ -= pull_;
            pull_ += chain_first_pull;
        }
    }

    platform::Image picture() const override {
        const auto& car = race_.player();
        render::Camera camera;
        camera.x = car.x / 4096.0;
        camera.z = car.z / 4096.0;
        camera.eye = car.eye;
        camera.yaw = std::uint16_t(car.yaw);
        camera.pitch = car.view_pitch;
        camera.roll = car.view_roll;
        const auto view = render::draw_race_view(circuits_, scenery_, race_.track(), car.segment, camera);
        render::Cockpit cockpit;
        if (chains_ != chains_gone) cockpit.chain_bottom = chains_;
        if (car.launch_flag) cockpit.message = data::decode_message(assets_.program, car.launch_message, car.launch_message_length);
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
    race::Race race_;
    int chains_ = chains_down, pull_ = chain_first_pull;
    int vhs_ = 0;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) { return std::make_unique<Module>(content); }

} // namespace stuntcar::game
