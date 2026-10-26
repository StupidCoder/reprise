#include "bruce_lee_c64/game/module.hpp"
#include "bruce_lee_c64/enhanced/effects.hpp"
#include "bruce_lee_c64/game/game.hpp"
#include "ui/vhs.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace bruce_lee::game {
namespace {

class Module : public app::GameModule {
public:
    explicit Module(data::Assets assets) : assets_(std::move(assets)) {
        game_.emplace(assets_);
        game_->start();
    }

    double step_seconds() const override { return 1.0 / 50.0; } // PAL frames
    double pixel_aspect() const override { return 0.9365; }
    void tick(const app::GameInput& in) override {
        game_->tick(in);
        ++frames_;
        if (settings_.smooth || settings_.particles) {
            effects_.update(*game_, settings_);
        }
    }
    platform::Image picture() const override {
        const auto& shown = settings_.smooth || settings_.particles ? effects_.picture() : game_->picture();
        auto image = shown.to_image(32, 35);
        return vhs_ ? ui::vhs(image, frames_, vhs_) : image;
    }

    void audio(std::span<float> stereo, int sample_rate) override {
        mono_.resize(stereo.size() / 2);
        game_->sid().render(mono_, sample_rate);
        for (std::size_t i = 0; i < mono_.size(); ++i) {
            stereo[2 * i] = stereo[2 * i + 1] = mono_[i];
        }
    }

    bool save() override {
        saved_.emplace(*game_);
        return true;
    }
    bool load() override {
        if (!saved_) {
            return false;
        }
        game_.emplace(*saved_);
        return true;
    }
    bool has_save() const override { return saved_.has_value(); }

    // The original's own choices that it sets with keys the launcher does
    // not pass on: L / R (shift for joystick 2) on the title, S in play.
    std::vector<app::GameOption> options() const override {
        const auto& o = game_->options;
        return {
            {"Joystick 1", {"Right-handed", "Left-handed"}, o.left_handed[0] ? 1 : 0,
             "Left-handed turns the stick a quarter, as the title's L key does (R sets it back)."},
            {"Joystick 2", {"Right-handed", "Left-handed"}, o.left_handed[1] ? 1 : 0,
             "The same for the second joystick (the Yamo's player)."},
            {"Title music", {"On", "Off"}, o.title_music ? 0 : 1,
             "The title plays its tune every fifth time it comes up; the S key in play turns that off."},
            {"Smooth motion", {"Off", "On"}, settings_.smooth ? 1 : 0,
             "Bruce, the Ninja, the Yamo and the fireballs glide over the three frames between the game's "
             "steps instead of jumping. The game's own places and collisions are unchanged."},
            {"Particle effects", {"Off", "On"}, settings_.particles ? 1 : 0,
             "Sparks where a blow lands, a burst of light from a lantern taken, the pieces of a crumbling "
             "floor and embers behind the fireballs, on the C64's pixel grid and in its colours."},
            {"VHS look", {"Off", "Light", "Medium", "Heavy", "Broken"}, vhs_,
             "A worn video tape's colour bleed and noise (shared with the other games)."},
        };
    }
    void set_option(std::size_t index, int value) override {
        auto& o = game_->options;
        if (index < 2) {
            o.left_handed[index] = value != 0;
        } else if (index == 2) {
            o.title_music = value == 0;
        } else if (index == 3) {
            settings_.smooth = value != 0;
        } else if (index == 4) {
            settings_.particles = value != 0;
        } else if (index == 5) {
            vhs_ = value;
        }
    }

private:
    data::Assets assets_;
    std::optional<Game> game_;
    std::optional<Game> saved_;
    std::vector<float> mono_;
    enhanced::Effects effects_;
    enhanced::Settings settings_;
    int vhs_{};
    long frames_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(data::load_assets(content));
}

} // namespace bruce_lee::game
