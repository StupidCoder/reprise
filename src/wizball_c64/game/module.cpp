#include "state/archive.hpp"
#include "wizball_c64/game/module.hpp"
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
        ++frames_;
        shake_ *= 0.82;
        if (hit_stop_ > 0) { // carnage mode's hit-stop: the game holds a moment
            --hit_stop_;
            return;
        }
        front_->tick(c);
        // Carnage mode's hard hits: a stop of two or four frames and a shake.
        if (auto* play = front_->play(); play && play->carnage()) {
            constexpr int stops[] = {0, 0, 2, 4};
            constexpr double shakes[] = {0, 1.0, 2.5, 5.0};
            const int impact = play->take_carnage_impact();
            hit_stop_ = stops[impact];
            shake_ = std::min(6.0, shake_ + shakes[impact]);
        }
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
        if (shake_ > 0.5) {
            // The shake: the whole picture a few pixels off, the edge black.
            const int dx = int(std::lround(std::sin(double(frames_) * 1.7) * shake_));
            const int dy = int(std::lround(std::cos(double(frames_) * 2.3) * shake_ * 0.6));
            platform::Image moved(image.width, image.height);
            for (int y = 0; y < image.height; ++y)
                for (int x = 0; x < image.width; ++x) {
                    const int sx = x - dx, sy = y - dy;
                    if (sx >= 0 && sy >= 0 && sx < image.width && sy < image.height)
                        moved.pixels[std::size_t(y * image.width + x)] = image.pixels[std::size_t(sy * image.width + sx)];
                }
            image = std::move(moved);
        }
        return image;
    }
    int vhs_level() const override { return vhs_; }

    void audio(std::span<float> stereo, int sample_rate) override {
        mono_.resize(stereo.size() / 2);
        front_->sound().sid().render(mono_, sample_rate);
        for (std::size_t i = 0; i < mono_.size(); ++i) stereo[2 * i] = stereo[2 * i + 1] = mono_[i];
    }

    // Quick save and load: the whole game, the random numbers' state with it.
    std::vector<std::uint8_t> write_state() override {
        state::Writer a;
        a(random_, *front_, frames_, hit_stop_, shake_, effects_);
        return std::move(a.bytes);
    }
    void read_state(std::span<const std::uint8_t> bytes) override {
        state::Reader a(bytes);
        a(random_, *front_, frames_, hit_stop_, shake_, effects_);
        a.finish();
        effects_.rebind(shown_play());
    }
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
             "The landscape, sky and aliens fill the window past the C64's border."},
            {"Particle effects", {"Off", "On"}, particles_ ? 1 : 0,
             "Debris, paint drops, shot streaks, sparks, sparkles and bubbles in C64 pixels."},
            {"Light effects", {"Off", "On"}, effects_settings_.lights ? 1 : 0,
             "Light rings, the smart bomb's wave, a shimmering shield and glowing rings."},
            {"Smooth motion", {"Off", "On"}, smooth_ ? 1 : 0,
             "Stars drift a pixel at a time, menu stars trail, a blackout fades out."},
            {"VHS look", {"Off", "Light", "Medium", "Heavy", "Broken"}, vhs_,
             "A worn video tape's colour bleed and noise."},
            {"Invincible", {"Off", "On"}, cheats_.invincible ? 1 : 0,
             "The game's own cheat without its seven-key sequence: nothing harms the wiz, here not even the "
             "catellite.", "Cheats"},
            {"Infinite lives", {"Off", "On"}, cheats_.infinite_lives ? 1 : 0,
             "A lost life is not counted: the game never ends.", "Cheats"},
            {"All start places", {"Off", "On"}, cheats_.any_start_place ? 1 : 0,
             "The title's keys 0-5 choose any of the first six places, not only those reached before.", "Cheats"},
            {"Carnage mode", {"Off", "On"}, cheats_.carnage ? 1 : 0,
             "Not the original game: fire is a rapid paint gun that bursts aliens into paint and shoots the "
             "landscape away.", "Carnage"},
            {"Automatic catellite", {"Off", "On"}, cheats_.catellite_ai ? 1 : 0,
             "The catellite plays itself, like a second player: it catches paint, keeps clear of aliens and "
             "shoots; holding fire keeps the wiz shooting.",
             "Cheats"},
        };
    }
    void set_option(std::size_t index, int value) override {
        const bool on = value != 0;
        auto& e = effects_settings_;
        switch (index) {
        case 0: widescreen_ = on; break;
        case 1: // the particle effects together
            particles_ = on;
            e.explosions = e.splashes = e.shots = e.pickups = e.catellite = e.tunnels = e.cauldrons = e.wiz = e.lab = on;
            break;
        case 2: e.lights = e.flight = on; break;
        case 3:
            smooth_ = on;
            e.stars = e.front = e.blackout = on;
            break;
        case 4: vhs_ = value; break;
        default:
            if (index == 5) cheats_.invincible = on;
            else if (index == 6) cheats_.infinite_lives = on;
            else if (index == 7) cheats_.any_start_place = on;
            else if (index == 8) cheats_.carnage = on;
            else if (index == 9) cheats_.catellite_ai = on;
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
    bool widescreen_{}, particles_{}, smooth_{};
    int hit_stop_{};
    double shake_{};
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
