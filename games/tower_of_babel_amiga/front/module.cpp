#include "tower_of_babel_amiga/front/module.hpp"
#include "amiga/paula_voices.hpp"
#include "state/archive.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/front/effects.hpp"
#include "tower_of_babel_amiga/front/front_end.hpp"
#include <algorithm>
#include <cmath>

namespace babel::front {
namespace {

// The keys the host reports (its C64-style matrix, row * 8 + column) as the
// characters the original's keyboard tables give without Shift.
int typed_key(int index) {
    switch (index) {
    case 0: return 8;   // delete
    case 1: return 13;  // return
    case 8: return '3'; case 9: return 'w'; case 10: return 'a'; case 11: return '4';
    case 12: return 'z'; case 13: return 's'; case 14: return 'e'; case 16: return '5';
    case 17: return 'r'; case 18: return 'd'; case 19: return '6'; case 20: return 'c';
    case 21: return 'f'; case 22: return 't'; case 23: return 'x'; case 24: return '7';
    case 25: return 'y'; case 26: return 'g'; case 27: return '8'; case 28: return 'b';
    case 29: return 'h'; case 30: return 'u'; case 31: return 'v'; case 32: return '9';
    case 33: return 'i'; case 34: return 'j'; case 35: return '0'; case 36: return 'm';
    case 37: return 'k'; case 38: return 'o'; case 39: return 'n'; case 41: return 'p';
    case 42: return 'l'; case 43: return '-'; case 44: return '.'; case 47: return ',';
    case 50: return ';'; case 53: return '='; case 55: return '/'; case 56: return '1';
    case 59: return '2'; case 62: return 'q';
    default: return 0;
    }
}

class Module final : public app::GameModule {
public:
    explicit Module(const std::filesystem::path& content)
        : assets_(data::load_assets(content)), front_(std::make_unique<FrontEnd>(assets_, content / "saves")), effects_(assets_) {}

    double step_seconds() const override { return 1.0 / 50; } // a PAL frame; game frames are 14 CIA ticks
    int vhs_level() const override { return vhs_; }

    std::vector<app::GameOption> options() const override {
        return {{"Widescreen", {"Off", "On"}, widescreen_ ? 1 : 0,
                 "The tower's view as wide as the window allows, up to 21:9; the title line and the view's frame "
                 "stretch with it, the panel and the menus stay in the middle. The game plays the same."},
                {"Smooth motion", {"Off", "On"}, smooth_ ? 1 : 0,
                 "Spiders, objects, lifts and the camera glide at the display's own rate instead of stepping "
                 "about 7 times a second. The view is shown one game frame late. The game plays the same."},
                app::screen_filter_option(vhs_)};
    }
    void set_option(std::size_t index, int value) override {
        if (index == 0) {
            widescreen_ = value != 0;
            set_canvas(canvas_width_, canvas_height_);
        }
        if (index == 1) smooth_ = value != 0;
        if (index == 2) vhs_ = std::clamp(value, 0, 4);
    }
    // Widescreen: as many more columns as the window's shape gives, from the
    // original's 320 up to 21:9.
    void set_canvas(int width, int height) override {
        canvas_width_ = width;
        canvas_height_ = height;
        int margin = 0;
        if (widescreen_ && width > 0 && height > 0) {
            // Square pixels: a window of aspect a shows a x 200 columns.
            const double columns = std::clamp(200.0 * width / height, 320.0, 200.0 * 21 / 9);
            margin = std::max(0, int(columns - 320) / 2);
        }
        front_->set_margin(margin);
    }
    void set_frame_fraction(double fraction) override { fraction_ = std::clamp(fraction, 0.0, 1.0); }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        FrontEnd::Input in;
        in.pointer = input.pointer;
        in.x = input.pointer_x;
        in.y = input.pointer_y;
        in.left = input.mouse_button;
        in.right = input.mouse_right;
        // A key typed: its first frame down.
        for (int i = 0; i < 64; ++i) {
            const bool down = input.c64_keys[std::size_t(i)];
            if (down && !keys_[std::size_t(i)] && in.key == 0) in.key = typed_key(i);
            keys_[std::size_t(i)] = down;
        }
        if (input.shift && in.key != 0) in.key = shifted(in.key);
        if (input.space && !space_) in.key = ' ';
        space_ = input.space;
        front_->vblank(in);
        if (const auto piece = front_->take_piece()) {
            const auto& p = assets_.pieces.at(std::size_t(*piece));
            voices_.set_samples(0, p.samples);
            voices_.set_period(0, p.period);
            voices_.set_volume(0, p.volume);
            playing_ = true;
        } else if (playing_ && !front_->piece_playing()) {
            voices_.set_volume(0, 0);
            voices_.set_samples(0, {}); // (silent either way; a quick save need not carry it)
            playing_ = false;
        }
        using Kind = FrontEnd::SoundEvent::Kind;
        for (const auto& e : front_->take_sounds()) {
            switch (e.kind) {
            case Kind::stop: effects_.stop(); break;
            case Kind::pause: effects_.pause(); break;
            case Kind::resume: effects_.resume(); break;
            case Kind::start: effects_.start(e.record); break;
            case Kind::frame:
                effects_.set_volume_offset(e.volume_offset);
                effects_.frame(e.frame);
                break;
            }
        }
    }

    // $80E2: with Shift, the key's place in the unshifted table ($69C(a6))
    // read from the shifted one, $70 further on (capital letters).
    int shifted(int key) const {
        constexpr std::uint32_t keymap = 0x806 + 0x69C;
        for (std::uint32_t i = 0; i < 0x70; ++i)
            if (assets_.program.u8(keymap + i) == key) return assets_.program.u8(keymap + 0x70 + i);
        return key;
    }

    platform::Image picture() const override { return front_->picture(front_->margin(), smooth_, fraction_); }

    // Quick saves: the front end (see FrontEnd::write_state), the sound
    // effects and the sampled piece as they sound, the keys held.
    std::vector<std::uint8_t> write_state() override {
        state::Writer ar;
        std::string tag = "tower of babel 1";
        ar(tag);
        front_->write_state(ar);
        ar(effects_, tick_phase_, voices_, playing_, keys_, space_);
        return std::move(ar.bytes);
    }
    void read_state(std::span<const std::uint8_t> bytes) override {
        state::Reader ar(bytes);
        std::string tag;
        ar(tag);
        if (tag != "tower of babel 1") state::Reader::fail();
        front_->read_state(ar);
        ar(effects_, tick_phase_, voices_, playing_, keys_, space_);
        ar.finish();
    }

    void audio(std::span<float> stereo, int sample_rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        voices_.render(stereo, sample_rate);
        // The effects' envelopes step with the CIA timer (about 99 a
        // second): rendered in stretches between its ticks.
        constexpr double timer_rate = 709379.0 / 7160.0;
        const double ticks_per_frame = timer_rate / double(std::max(sample_rate, 1));
        const std::size_t frames = stereo.size() / 2;
        for (std::size_t done = 0; done < frames;) {
            const auto until_tick = std::size_t(std::ceil((1.0 - tick_phase_) / ticks_per_frame));
            const std::size_t n = std::min(frames - done, std::max<std::size_t>(until_tick, 1));
            effects_.render(stereo.subspan(done * 2, n * 2), sample_rate);
            done += n;
            tick_phase_ += double(n) * ticks_per_frame;
            if (tick_phase_ >= 1.0) {
                tick_phase_ -= 1.0;
                effects_.tick();
            }
        }
    }

private:
    data::Assets assets_;
    std::unique_ptr<FrontEnd> front_;
    amiga::PaulaVoices voices_;
    Effects effects_;
    double tick_phase_{};
    std::array<bool, 64> keys_{};
    bool space_{}, playing_{};
    int vhs_{};
    bool widescreen_{}, smooth_{};
    int canvas_width_{}, canvas_height_{};
    double fraction_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace babel::front
