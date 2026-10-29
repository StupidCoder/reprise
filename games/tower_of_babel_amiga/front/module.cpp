#include "tower_of_babel_amiga/front/module.hpp"
#include "amiga/paula_voices.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/front/front_end.hpp"
#include <algorithm>

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
        : assets_(data::load_assets(content)), front_(std::make_unique<FrontEnd>(assets_)) {}

    double step_seconds() const override { return 1.0 / 50; } // a PAL frame; game frames are 14 CIA ticks
    double pixel_aspect() const override { return 1.0; }
    int vhs_level() const override { return vhs_; }

    std::vector<app::GameOption> options() const override { return {app::screen_filter_option(vhs_)}; }
    void set_option(std::size_t index, int value) override {
        if (index == 0) vhs_ = std::clamp(value, 0, 4);
    }

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
            playing_ = false;
        }
        (void)front_->take_sounds(); // effect sounds: not rebuilt yet (plan.md)
    }

    platform::Image picture() const override { return front_->screen().image(); }

    void audio(std::span<float> stereo, int sample_rate) override {
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        voices_.render(stereo, sample_rate);
    }

private:
    data::Assets assets_;
    std::unique_ptr<FrontEnd> front_;
    amiga::PaulaVoices voices_;
    std::array<bool, 64> keys_{};
    bool space_{}, playing_{};
    int vhs_{};
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(content);
}

} // namespace babel::front
