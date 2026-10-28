#include "state/archive.hpp"
#include "marble_madness_amiga/game/module.hpp"
#include "marble_madness_amiga/data/assets.hpp"
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/front_end.hpp"
#include "marble_madness_amiga/game/race.hpp"
#include "marble_madness_amiga/game/view.hpp"
#include "marble_madness_amiga/sound/sound_board.hpp"
#include <chrono>
#include <memory>
#include <optional>

namespace marble_madness::game {
namespace {

// A world update takes two PAL frames ($106EB0 waits for at least two
// vertical blanks per update).
constexpr double update_seconds = 2.0 / 50.0;

enum Option { steering = 0, vhs_look = 1, stop_timer = 2, safe_falls = 3, crowd_race = 4, crowd_close = 5, crowd_skill = 6 };

// The game as the program runs it (front_end.hpp): the options screen, the
// courses with their introductions, finishes and the end. One player.
class Module : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : files_(content), assets_(data::load_assets(content)), flow_(files_, assets_) {}

    double step_seconds() const override { return update_seconds; }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& in) override {
        flow_.cheats = cheats_;
        flow_.tick({in.up, in.down, in.left, in.right, in.fire, next_push_});
        // The controls are read at the end of an update for the next one.
        next_push_ = read_push(in);
        // The options screen's input device sets the steering.
        if (const int device = flow_.settings().device[0]; device != device_) {
            device_ = device;
            steering_ = device == 0 ? 0 : 1;
        }
    }

    platform::Image picture() const override {
        return flow_.picture();
    }
    int vhs_level() const override { return vhs_; }

    void audio(std::span<float> stereo, int sample_rate) override { flow_.audio(stereo, sample_rate); }

    std::vector<app::GameOption> options() const override {
        return {{.name = "Steering",
                 .choices = {"Joystick", "Screen directions"},
                 .value = steering_,
                 .description = "Joystick: the original's joystick setting, where each direction pushes along "
                                "the course's diagonal grid. Screen directions: pushes the way the original's "
                                "mouse setting does, so up is up on the screen. The options screen's input "
                                "device sets it too."},
                {.name = "VHS look",
                 .choices = {"Off", "Light", "Medium", "Heavy", "Broken"},
                 .value = vhs_,
                 .description = "A worn video tape's colour bleed and noise (shared with the other games)."},
                {.name = "Stop the timer",
                 .choices = {"Off", "On"},
                 .value = cheats_.stop_timer ? 1 : 0,
                 .description = "The race clock stands still: there is all the time in the world.",
                 .group = "Cheats"},
                {.name = "Unbreakable marble",
                 .choices = {"Off", "On"},
                 .value = cheats_.safe_falls ? 1 : 0,
                 .description = "Drops from high places never break the marble: it bounces on and rolls away. "
                                "Rolling off the course into nothing still costs a marble.",
                 .group = "Cheats"},
                {.name = "Crowd race",
                 .choices = {"Off", "On"},
                 .value = crowd_ ? 1 : 0,
                 .description = "Not the original game: seven more marbles race you to the goal, steered by the "
                                "computer, each in its own colour. They bump, fall, break and come back by the "
                                "same rules as yours. Turning it on mid-race puts them on the course's start; "
                                "turning it off takes them away.",
                 .group = "Crowd race"},
                {.name = "Keep the pack close",
                 .choices = {"Off", "On"},
                 .value = crowd_close_ ? 1 : 0,
                 .description = "Rubber banding for the crowd race: a computer marble ahead of you rolls slower "
                                "the further ahead it gets, so a lost marble does not leave you behind for good. "
                                "Behind you they race flat out.",
                 .group = "Crowd race"},
                {.name = "Computer skill",
                 .choices = {"Easy", "Normal", "Hard"},
                 .value = crowd_skill_,
                 .description = "How well the computer's marbles race. Easy: slower, no fire button, and now and "
                                "then they roll off an edge or miss a jump. Normal: no fire button and the odd "
                                "mistake. Hard: fire button and no mistakes.",
                 .group = "Crowd race"}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == steering) {
            steering_ = value;
            device_ = value == 0 ? 0 : 3; // Joy Stick or Mouse
            flow_.set_device(device_);
        } else if (index == vhs_look) {
            vhs_ = value;
        } else if (index == stop_timer) {
            cheats_.stop_timer = value != 0;
        } else if (index == safe_falls) {
            cheats_.safe_falls = value != 0;
        } else if (index == crowd_race) {
            crowd_ = value != 0;
            flow_.set_crowd(crowd_);
        } else if (index == crowd_close) {
            crowd_close_ = value != 0;
            flow_.set_crowd_close(crowd_close_);
        } else if (index == crowd_skill) {
            crowd_skill_ = std::clamp(value, 0, 2);
            flow_.set_crowd_skill(crowd_skill_);
        }
    }

    std::string stopped() const override { return {}; }

    // Quick save and load: the whole game where it is (screen, race, sound
    // and random numbers); the options stay as they are now.
    std::vector<std::uint8_t> write_state() override {
        state::Writer a;
        a(flow_, next_push_);
        return std::move(a.bytes);
    }
    void read_state(std::span<const std::uint8_t> bytes) override {
        state::Reader a(bytes);
        a(flow_, next_push_);
        a.finish();
    }
    bool save() override {
        saved_.emplace(flow_);
        saved_push_ = next_push_;
        return true;
    }
    bool load() override {
        if (!saved_) return false;
        flow_ = *saved_;
        next_push_ = saved_push_;
        flow_.set_device(device_);
        flow_.set_crowd(crowd_);
        flow_.set_crowd_close(crowd_close_);
        flow_.set_crowd_skill(crowd_skill_);
        return true;
    }
    bool has_save() const override { return saved_.has_value(); }

private:
    // $119A00: the joystick gives $20 per axis; fire doubles the push
    // ($119944). The mouse setting turns the screen's axes onto the course's
    // diagonal grid ((x - y) / 2, (x + y) / 2, doubled).
    Push read_push(const app::GameInput& in) const {
        std::int16_t x = std::int16_t((in.left ? 0x20 : 0) - (in.right ? 0x20 : 0));
        std::int16_t y = std::int16_t((in.down ? 0x20 : 0) - (in.up ? 0x20 : 0));
        Push p;
        if (steering_ == 0) {
            p.side = x;
            p.forward = y;
        } else {
            p.side = std::int16_t(((x - y) >> 1) << 1);
            p.forward = std::int16_t(((y + x) >> 1) << 1);
        }
        if (in.fire) {
            p.side = std::int16_t(p.side << 1);
            p.forward = std::int16_t(p.forward << 1);
        }
        return p;
    }

    data::GameFiles files_;
    data::Assets assets_;
    GameFlow flow_;
    Push next_push_{};
    int steering_{};
    int device_{};
    int vhs_{};
    Race::Cheats cheats_;
    bool crowd_{};
    bool crowd_close_{true};
    int crowd_skill_{Race::normal};
    std::optional<GameFlow> saved_;
    Push saved_push_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace marble_madness::game
