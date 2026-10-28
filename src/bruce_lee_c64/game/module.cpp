#include "state/archive.hpp"
#include "bruce_lee_c64/game/module.hpp"
#include "bruce_lee_c64/enhanced/effects.hpp"
#include "bruce_lee_c64/enhanced/wide_view.hpp"
#include "bruce_lee_c64/game/game.hpp"
#include <algorithm>
#include <cmath>
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
        game_->world.carnage.on = carnage_;
        ++frames_;
        shake_ *= 0.8;
        if (hit_stop_ > 0) {
            --hit_stop_; // carnage mode: the picture holds on a big hit
            shaken_ = shake(shown());
            return;
        }
        game_->tick(in);
        if (enhanced()) {
            effects_.update(*game_, settings_);
        }
        if (widescreen_) {
            wide_view_.update(*game_, enhanced() ? effects_.picture() : game_->picture(), wide_.width);
        }
        carnage_feel();
        shaken_ = shake(shown());
    }
    void set_canvas(int width, int height) override {
        canvas_ = {width, height};
        wide_ = enhanced::wide_size(width, height, pixel_aspect());
    }
    platform::Image picture() const override {
        // Widescreen: the picture to the window's edges (some border above
        // and below only in windows narrower than the display window).
        const auto& shown = shaken_.pixels.empty() ? this->shown() : shaken_;
        auto image = widescreen_ ? shown.to_image(0, wide_.margin_y) : shown.to_image(32, 35);
        return image;
    }
    int vhs_level() const override { return vhs_; }

    void audio(std::span<float> stereo, int sample_rate) override {
        mono_.resize(stereo.size() / 2);
        game_->sid().render(mono_, sample_rate);
        for (std::size_t i = 0; i < mono_.size(); ++i) {
            stereo[2 * i] = stereo[2 * i + 1] = mono_[i];
        }
    }

    std::vector<std::uint8_t> write_state() override {
        state::Writer a;
        a(*game_, frames_, carnage_seen_, hit_stop_, shake_, shaken_, effects_, wide_view_);
        return std::move(a.bytes);
    }
    void read_state(std::span<const std::uint8_t> bytes) override {
        state::Reader a(bytes);
        a(*game_, frames_, carnage_seen_, hit_stop_, shake_, shaken_, effects_, wide_view_);
        a.finish();
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

    // The upgrades, carnage mode and cheats. (The original's own choices,
    // set with keys the launcher does not pass on, L / R on the title and S
    // in play, stay at their defaults.)
    std::vector<app::GameOption> options() const override {
        return {
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
            {"Carnage mode", {"Off", "On"}, carnage_ ? 1 : 0,
             "Not the original game: Bruce's blows send the Ninja and the Yamo flying (and the Yamo's "
             "send Bruce); they bounce off the walls and floors, smash the decoration (statues topple), "
             "knock each other over and die on the room's hazards; enemies come back sooner in waves; "
             "in the wizard's room a blow sends a fireball back at his face; hits in quick succession "
             "build a combo that scores.",
             "Carnage"},
            {"Infinite lives", {"Off", "On"}, cheats_.infinite_lives ? 1 : 0,
             "A lost fall is not counted: the game never ends.", "Cheats"},
            {"Enemy hits don't count", {"Off", "On"}, cheats_.harmless_enemies ? 1 : 0,
             "The Ninja and the Yamo still strike and land on Bruce, but their hits do him no harm (falls, "
             "flames and other hazards still do).", "Cheats"},
        };
    }
    void set_option(std::size_t index, int value) override {
        // (In the order options() lists them. The original's own settings,
        // the joysticks' handedness and the title tune, keep its defaults.)
        switch (index) {
        case 0: settings_.smooth = value != 0; break;
        case 1: settings_.particles = value != 0; break;
        case 2: settings_.lights = value != 0; break;
        case 3: settings_.gibs = value != 0; break;
        case 4:
            widescreen_ = value != 0;
            if (widescreen_) {
                wide_view_ = {};
                wide_view_.update(*game_, enhanced() ? effects_.picture() : game_->picture(), wide_.width);
            }
            break;
        case 5: vhs_ = value; break;
        case 6: carnage_ = value != 0; break;
        case 7: cheats_.infinite_lives = value != 0; break;
        case 8: cheats_.harmless_enemies = value != 0; break;
        default: break;
        }
    }

private:
    // The picture as the options make it (before any shake).
    const c64::Picture& shown() const {
        return widescreen_ ? wide_view_.picture() : enhanced() ? effects_.picture() : game_->picture();
    }
    // Carnage mode's feel: new big hits stop the picture for a few frames
    // and shake it.
    void carnage_feel() {
        for (const auto& e : game_->world.carnage.events) {
            if (e.id <= carnage_seen_) {
                continue;
            }
            carnage_seen_ = e.id;
            using Kind = World::Carnage::Event::Kind;
            if (e.kind == Kind::launch || e.kind == Kind::impact || e.kind == Kind::knock || e.kind == Kind::face) {
                constexpr int stops[] = {0, 0, 2, 4};
                constexpr double shakes[] = {0, 0.8, 2.0, 4.0};
                const int s = std::clamp(e.strength, 0, 3);
                hit_stop_ = std::max(hit_stop_, stops[s]);
                shake_ = std::min(6.0, shake_ + shakes[s]);
            }
        }
    }
    c64::Picture shake(const c64::Picture& p) const {
        const int dx = int(std::lround(shake_ * std::sin(frames_ * 2.3))) & ~1; // whole pixel pairs
        const int dy = int(std::lround(shake_ * std::cos(frames_ * 1.7)));
        if (dx == 0 && dy == 0) {
            return p;
        }
        c64::Picture out(p.width);
        out.border = p.border;
        std::fill(out.pixels.begin(), out.pixels.end(), p.border);
        for (int y = 0; y < c64::Picture::height; ++y) {
            const int sy = y - dy;
            if (sy < 0 || sy >= c64::Picture::height) {
                continue;
            }
            for (int x = 0; x < p.width; ++x) {
                const int sx = x - dx;
                if (sx >= 0 && sx < p.width) {
                    out.pixels[std::size_t(y * p.width + x)] = p.at(sx, sy);
                }
            }
        }
        return out;
    }
    bool carnage_{};
    std::uint32_t carnage_seen_{};
    int hit_stop_{};
    double shake_{};
    c64::Picture shaken_ = c64::Picture(0);
    data::Assets assets_;
    std::optional<Game> game_;
    std::optional<Game> saved_;
    bool enhanced() const {
        return settings_.smooth || settings_.particles || settings_.lights || settings_.gibs || carnage_;
    }
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
