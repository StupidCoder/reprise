#include "wizball_c64/game/module.hpp"
#include "ui/vhs.hpp"
#include "wizball_c64/enhanced/effects.hpp"
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/game/front_end.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace wizball::game {
namespace {

constexpr int space_key = 7 * 8 + 4; // the C64 space bar in the key matrix
constexpr double aspect = 0.9365;    // PAL C64 pixels

// Widescreen: for a window of this shape, the picture's width and the side
// columns to render (one more than shows, so a column scrolling in is
// drawn already). Up to 21:9; the original's frame with its border is the
// narrowest.
struct WideSize {
    int width{};     // 0: the original frame
    int extension{};
};
WideSize wide_size(int canvas_width, int canvas_height) {
    constexpr int frame = Display::width, height = Display::line_count, window = 320;
    if (canvas_width <= 0 || canvas_height <= 0) return {};
    const double shape = std::min(double(canvas_width) / canvas_height, 21.0 / 9.0);
    const int width = int(std::lround(shape * height / aspect));
    if (width <= frame) return {};
    return {width, int(std::ceil((width - window) / 16.0)) + 1};
}

// The whole game from the title.
class Module : public app::GameModule {
public:
    explicit Module(data::GameImage image)
        : image_(std::move(image)), assets_(data::decode_assets(image_)), random_(image_),
          front_(std::make_unique<FrontEnd>(assets_, image_, random_)) {}

    double step_seconds() const override { return 1.0 / 50.0; } // PAL frames
    double pixel_aspect() const override { return aspect; }

    void tick(const app::GameInput& in) override {
        auto port = [](bool up, bool down, bool left, bool right, bool fire) {
            std::uint8_t bits = 0xFF;
            if (up) bits &= ~1;
            if (down) bits &= ~2;
            if (left) bits &= ~4;
            if (right) bits &= ~8;
            if (fire) bits &= ~16;
            return bits;
        };
        Controls c;
        c.ports.port2 = port(in.up, in.down, in.left, in.right, in.fire);
        c.ports.port1 = port(in.up2, in.down2, in.left2, in.right2, in.fire2);
        c.keys = in.c64_keys;
        c.keys[space_key] = c.keys[space_key] || in.space;
        front_->tick(c);
        ++frames_;
        effects_.update(shown_play(), effects_settings_);
        effects_.update_front(*front_, effects_settings_);
    }

    void set_canvas(int width, int height) override { canvas_ = {width, height}; }

    platform::Image picture() const override {
        const auto wide = widescreen_ ? wide_size(canvas_[0], canvas_[1]) : WideSize{};
        const auto display = front_->picture(wide.extension, effects_.blackout_fade());
        auto image = display.render();
        if (const auto* play = shown_play(); play && effects_settings_.any())
            effects_.draw(image, display, *play, effects_settings_);
        if (effects_settings_.front) effects_.draw_front(image, display, *front_);
        if (wide.width > 0) {
            // Centred on the C64's window (24 pixels in from the frame's left,
            // 40 from its right).
            platform::Image cut(wide.width, image.height);
            const int left = 24 + 160 + 8 * wide.extension - wide.width / 2;
            for (int y = 0; y < image.height; ++y)
                for (int x = 0; x < wide.width; ++x)
                    cut.pixels[std::size_t(y * wide.width + x)] = image.pixels[std::size_t(y * image.width + left + x)];
            image = std::move(cut);
        }
        return vhs_ ? ui::vhs(image, frames_, vhs_) : image;
    }

    void audio(std::span<float> stereo, int sample_rate) override {
        mono_.resize(stereo.size() / 2);
        front_->sound().sid().render(mono_, sample_rate);
        for (std::size_t i = 0; i < mono_.size(); ++i) stereo[2 * i] = stereo[2 * i + 1] = mono_[i];
    }

    // Quick save and load: the whole game, the random numbers' state with it.
    bool save() override {
        saved_ = {random_, front_->copy()};
        return true;
    }
    bool load() override {
        if (!saved_.front) return false;
        random_ = saved_.random;
        front_ = saved_.front->copy();
        effects_.update(shown_play(), effects_settings_); // starts afresh on the loaded game
        front_->set_cheats(cheats_); // the options as they are now
        return true;
    }
    bool has_save() const override { return saved_.front != nullptr; }

    std::vector<app::GameOption> options() const override {
        return {
            {"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
             "The picture fills the window: while a place scrolls, its landscape and sky run on to the "
             "window's sides (as far as the game's own view goes at the place's ends), with the aliens "
             "already there. The game plays as the original; the other screens keep their border."},
            {"Explosions", {"Off", "On"}, effects_settings_.explosions ? 1 : 0,
             "An alien's explosion throws debris in the explosion's own colours, on the C64's double-wide "
             "pixels, fading down through darker C64 colours, with a short ring of light."},
            {"Paint drops", {"Off", "On"}, effects_settings_.splashes ? 1 : 0,
             "A falling paint droplet leaves a short drip trail, and bursting on the ground it throws drops "
             "of its paint, which land again."},
            {"Shot streaks", {"Off", "On"}, effects_settings_.shots ? 1 : 0,
             "The wiz's and the catellite's shots leave a short fading streak of their own pixels and "
             "spark where they hit; the aliens' shots (and the wiz's pieces when it bursts) trail their dot "
             "along their flight."},
            {"Smooth star drift", {"Off", "On"}, effects_settings_.stars ? 1 : 0,
             "The sky's drifting stars move a pixel at a time instead of in the multicolour pixel pairs."},
            {"Pickup sparkles", {"Off", "On"}, effects_settings_.pickups ? 1 : 0,
             "A pearl, extra life or token the wiz takes, or a droplet the catellite catches, sparkles where "
             "it goes."},
            {"Catellite effects", {"Off", "On"}, effects_settings_.catellite ? 1 : 0,
             "A hit on the catellite throws sparks of its colours, and when it is lost it bursts like an "
             "alien."},
            {"Tunnel sparkles", {"Off", "On"}, effects_settings_.tunnels ? 1 : 0,
             "Sparkles swirl up as the wiz sinks into a tunnel, and round the exit as it rises in the next "
             "place and the wiz slides out."},
            {"Cauldron bubbles", {"Off", "On"}, effects_settings_.cauldrons ? 1 : 0,
             "Paint poured into a cauldron sends bubbles up through it that pop at the surface."},
            {"Wiz effects", {"Off", "On"}, effects_settings_.wiz ? 1 : 0,
             "The smart bomb sends a ring of light out over the field with its flashing; the shield shimmers "
             "round the wiz (and a shielded catellite) and the wizspray's rings glow; the wiz's rings "
             "shed sparkles as it fades out at a place's end and in at a life's start."},
            {"Blackout fade", {"Off", "On"}, effects_settings_.blackout ? 1 : 0,
             "When a black droplet has blacked out the landscape, it fades out over a few frames after each "
             "bump instead of vanishing at once."},
            {"Flight glow", {"Off", "On"}, effects_settings_.flight ? 1 : 0,
             "The level-complete flight's words glow in their changing colour."},
            {"Wizlab sparkles", {"Off", "On"}, effects_settings_.lab ? 1 : 0,
             "The weapon you take in the Wizlab sparkles on its token, and the paint used rises out of the "
             "colour cauldron."},
            {"Menu star trails", {"Off", "On"}, effects_settings_.front ? 1 : 0,
             "The flying stars of the menu and the player screen leave short fading trails."},
            {"VHS look", {"Off", "Light", "Medium", "Heavy", "Broken"}, vhs_,
             "A worn video tape's colour bleed and noise (shared with the other games)."},
            {"Invincible", {"Off", "On"}, cheats_.invincible ? 1 : 0,
             "The game's own cheat (a seven-key sequence on the title) without typing it: nothing harms the "
             "wiz while a place is being played; here the catellite takes no hits either.",
             "Cheats"},
            {"Infinite lives", {"Off", "On"}, cheats_.infinite_lives ? 1 : 0,
             "A lost life is not counted: the game never ends.", "Cheats"},
            {"All start places", {"Off", "On"}, cheats_.any_start_place ? 1 : 0,
             "The title's keys 0-5 choose any of the first six places, not only those up to two before the "
             "furthest reached last game.",
             "Cheats"},
        };
    }
    void set_option(std::size_t index, int value) override {
        const bool on = value != 0;
        auto& e = effects_settings_;
        bool* const effects[] = {&e.explosions, &e.splashes, &e.shots, &e.stars, &e.pickups, &e.catellite, &e.tunnels,
                                 &e.cauldrons, &e.wiz, &e.blackout, &e.flight, &e.lab, &e.front};
        constexpr std::size_t first_effect = 1, effect_count = std::size(effects), vhs = first_effect + effect_count;
        if (index == 0) {
            widescreen_ = on;
        } else if (index < vhs) {
            *effects[index - first_effect] = on;
        } else if (index == vhs) {
            vhs_ = value;
        } else {
            if (index == vhs + 1) cheats_.invincible = on;
            else if (index == vhs + 2) cheats_.infinite_lives = on;
            else if (index == vhs + 3) cheats_.any_start_place = on;
            front_->set_cheats(cheats_);
        }
    }

private:
    // The game being played, while its picture is shown.
    const Play* shown_play() const {
        const auto screen = front_->screen();
        return screen == FrontEnd::Screen::play || screen == FrontEnd::Screen::game_over ? front_->play() : nullptr;
    }

    data::GameImage image_;
    data::Assets assets_;
    GameRandom random_;
    std::unique_ptr<FrontEnd> front_;
    struct Saved {
        GameRandom random;
        std::unique_ptr<FrontEnd> front;
    } saved_{random_, nullptr};
    std::vector<float> mono_;
    bool widescreen_{};
    enhanced::Effects effects_;
    enhanced::Settings effects_settings_;
    int vhs_{};
    FrontEnd::Cheats cheats_{};
    std::array<int, 2> canvas_{};
    long frames_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(data::load_game_image(content));
}

} // namespace wizball::game
