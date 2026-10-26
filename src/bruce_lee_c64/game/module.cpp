#include "bruce_lee_c64/game/module.hpp"
#include "bruce_lee_c64/enhanced/effects.hpp"
#include "bruce_lee_c64/enhanced/wide_view.hpp"
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
        game_->world.cheats = cheats_; // also after a quick load
        game_->tick(in);
        ++frames_;
        if (enhanced()) {
            effects_.update(*game_, settings_);
        }
        if (widescreen_) {
            wide_view_.update(*game_, enhanced() ? effects_.picture() : game_->picture(), wide_.width);
        }
    }
    void set_canvas(int width, int height) override {
        canvas_ = {width, height};
        wide_ = enhanced::wide_size(width, height, pixel_aspect());
    }
    platform::Image picture() const override {
        // Widescreen: the picture to the window's edges (some border above
        // and below only in windows narrower than the display window).
        auto image = widescreen_ ? wide_view_.picture().to_image(0, wide_.margin_y)
                                 : (enhanced() ? effects_.picture() : game_->picture()).to_image(32, 35);
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
        wide_view_ = {}; // (not an exit: no scroll)
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
             "steps instead of jumping, and the lift scrolls a row at a time. The game's own places and "
             "collisions are unchanged."},
            {"Particle effects", {"Off", "On"}, settings_.particles ? 1 : 0,
             "Sparks where a blow lands, a burst of light from a lantern taken, cracks crawling across a "
             "crumbling floor and its falling pieces, and embers behind the fireballs, on the C64's pixel "
             "grid and in its colours."},
            {"Light effects", {"Off", "On"}, settings_.lights ? 1 : 0,
             "A soft light around each hanging lantern that flickers with its flame, flames that lick up "
             "instead of blinking, and a white flash on whoever a blow lands on; in the C64's colours."},
            {"Gore", {"Off", "On"}, settings_.gibs ? 1 : 0,
             "A defeated Ninja or Yamo, and anyone caught by spikes, a laser or a fireball, bursts into "
             "bloody pieces that bounce and lie on the floor for a while (not in the original)."},
            {"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
             "The picture fills the window, and where the C64 has its border the rooms beside the one you "
             "are in show through, dimmed and still; taking an exit scrolls over to the next room."},
            {"VHS look", {"Off", "Light", "Medium", "Heavy", "Broken"}, vhs_,
             "A worn video tape's colour bleed and noise (shared with the other games)."},
            {"Infinite lives", {"Off", "On"}, cheats_.infinite_lives ? 1 : 0,
             "A lost fall is not counted: the game never ends.", "Cheats"},
            {"Enemy hits don't count", {"Off", "On"}, cheats_.harmless_enemies ? 1 : 0,
             "The Ninja and the Yamo still strike and land on Bruce, but their hits do him no harm (falls, "
             "flames and other hazards still do).", "Cheats"},
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
            settings_.lights = value != 0;
        } else if (index == 6) {
            settings_.gibs = value != 0;
        } else if (index == 7) {
            widescreen_ = value != 0;
            if (widescreen_) {
                wide_view_ = {};
                wide_view_.update(*game_, enhanced() ? effects_.picture() : game_->picture(), wide_.width);
            }
        } else if (index == 8) {
            vhs_ = value;
        } else if (index == 9) {
            cheats_.infinite_lives = value != 0;
        } else if (index == 10) {
            cheats_.harmless_enemies = value != 0;
        }
    }

private:
    data::Assets assets_;
    std::optional<Game> game_;
    std::optional<Game> saved_;
    bool enhanced() const { return settings_.smooth || settings_.particles || settings_.lights || settings_.gibs; }
    std::vector<float> mono_;
    enhanced::Effects effects_;
    enhanced::Settings settings_;
    int vhs_{};
    bool widescreen_{};
    std::array<int, 2> canvas_{};
    enhanced::WideSize wide_ = enhanced::wide_size(0, 0, 0.9365);
    enhanced::WideView wide_view_;
    game::World::Cheats cheats_;
    long frames_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(data::load_assets(content));
}

} // namespace bruce_lee::game
