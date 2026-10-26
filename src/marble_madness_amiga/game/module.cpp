#include "marble_madness_amiga/game/module.hpp"
#include "marble_madness_amiga/data/assets.hpp"
#include "marble_madness_amiga/data/game_files.hpp"
#include "marble_madness_amiga/game/race.hpp"
#include "marble_madness_amiga/game/view.hpp"
#include "marble_madness_amiga/sound/sound_board.hpp"
#include "ui/vhs.hpp"
#include <memory>
#include <optional>

namespace marble_madness::game {
namespace {

// A world update takes two PAL frames ($106EB0 waits for at least two
// vertical blanks per update).
constexpr double update_seconds = 2.0 / 50.0;

// Sound entries the between-race code plays ($10BC28 after a finish:
// longs per course; $10BE7C when the time runs out).
constexpr std::uint32_t finish_sounds = 0x102124;
constexpr int out_of_time_sound = 0x19;

enum Option { steering = 0, vhs_look = 1, stop_timer = 2, safe_falls = 3 };

// One player racing the courses in order. The front end (title, options,
// score screens, two players) is not rebuilt yet: a finished course starts
// the next one, running out of time starts Practice again.
class Module : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : files_(content), assets_(data::load_assets(content)) {
        start_course(data::Course::practice);
    }

    double step_seconds() const override { return update_seconds; }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& in) override {
        race_->cheats = cheats_;
        race_->update({next_push_, Push{}});
        view_->update(*race_);
        // The controls are read at the end of an update for the next one.
        next_push_ = read_push(in);
        ++frames_;
        for (const auto& e : race_->take_sounds()) {
            sounds_->play_after_blank(e.id, e.right);
        }
        after_update();
    }

    platform::Image picture() const override {
        auto image = view_->draw(*race_);
        return vhs_ ? ui::vhs(image, frames_, vhs_) : image;
    }

    void audio(std::span<float> stereo, int sample_rate) override { sounds_->render(stereo, sample_rate); }

    std::vector<app::GameOption> options() const override {
        return {{.name = "Steering",
                 .choices = {"Joystick", "Screen directions"},
                 .value = steering_,
                 .description = "Joystick: the original's joystick setting, where each direction pushes along "
                                "the course's diagonal grid. Screen directions: pushes the way the original's "
                                "mouse setting does, so up is up on the screen."},
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
                 .group = "Cheats"}};
    }
    void set_option(std::size_t index, int value) override {
        if (index == steering) {
            steering_ = value;
        } else if (index == vhs_look) {
            vhs_ = value;
        } else if (index == stop_timer) {
            cheats_.stop_timer = value != 0;
        } else if (index == safe_falls) {
            cheats_.safe_falls = value != 0;
        }
    }

    std::string stopped() const override { return {}; }

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

    void start_course(data::Course course) {
        course_ = course;
        race_ = std::make_unique<Race>(files_, course);
        view_ = std::make_unique<View>(files_, assets_, *race_);
        view_->update(*race_);
        sounds_ = std::make_unique<sound::SoundBoard>(files_, course);
        play_sounds();
        next_push_ = {};
        wait_ = 0;
    }

    void play_sounds() {
        for (const auto& e : race_->take_sounds()) {
            sounds_->play(e.id, e.right);
        }
    }

    void after_update() {
        const auto& m = race_->marbles[0];
        if (wait_ == 0 && m.status == 3) {
            sounds_->play(int(files_.program().u32(finish_sounds + std::uint32_t(course_) * 4)), false);
        } else if (wait_ == 0 && m.status == 2) {
            // Out of time: its tune, and the rolling sounds end ($12250A).
            sounds_->play(out_of_time_sound, false);
            sounds_->play(0xF, false);
            if (course_ != data::Course::intermediate) {
                sounds_->play(0x10, false);
            }
        }
        if (m.status == 3 || m.status == 2) {
            if (++wait_ < 75) {
                return;
            }
            if (m.status == 3 && int(course_) + 1 < data::course_count) {
                start_course(data::Course(int(course_) + 1));
            } else {
                start_course(data::Course::practice);
            }
        }
    }

    data::GameFiles files_;
    data::Assets assets_;
    std::unique_ptr<Race> race_;
    std::unique_ptr<View> view_;
    std::unique_ptr<sound::SoundBoard> sounds_;
    data::Course course_{};
    Push next_push_{};
    int steering_{};
    int vhs_{};
    Race::Cheats cheats_;
    long frames_{};
    int wait_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace marble_madness::game
