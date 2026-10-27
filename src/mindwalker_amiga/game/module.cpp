#include "mindwalker_amiga/game/module.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/title/title_stage.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include "mindwalker_amiga/landscape/landscape_stage.hpp"
#include "mindwalker_amiga/data/assets.hpp"
#include <optional>
#include "platform/files.hpp"
#include <array>
#include <chrono>
#include <cstdio>

namespace mindwalker::game {
namespace {

// Intuition's mouse pointer, from Workbench's preferences on the game disk
// (struct Preferences: PointerMatrix at +28, two control words then 16 rows of
// two plane words; XOffset/YOffset at +100/+101; colours 17-19 at +102).
struct MousePointer {
    std::array<std::array<std::uint8_t, 16>, 16> pixels{};
    int x_offset{}, y_offset{};
    std::array<screen::Rgb4, 3> colours{};

    explicit MousePointer(const std::vector<std::uint8_t>& prefs) {
        const auto word = [&](std::size_t at) { return std::uint16_t(prefs.at(at) << 8 | prefs.at(at + 1)); };
        for (int y = 0; y < 16; ++y) {
            const auto a = word(28 + 4 + std::size_t(y) * 4), b = word(28 + 6 + std::size_t(y) * 4);
            for (int x = 0; x < 16; ++x) {
                pixels[std::size_t(y)][std::size_t(x)] = std::uint8_t((a >> (15 - x) & 1) | (b >> (15 - x) & 1) << 1);
            }
        }
        x_offset = std::int8_t(prefs.at(100));
        y_offset = std::int8_t(prefs.at(101));
        for (std::size_t c = 0; c < 3; ++c) colours[c] = word(102 + c * 2) & 0xFFF;
    }

    void draw(platform::Image& image, int x0, int y0) const {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const auto v = pixels[std::size_t(y)][std::size_t(x)];
                if (v) image.put(x0 + x_offset + x, y0 + y_offset + y, screen::to_color(colours[v - 1u]));
            }
        }
    }
};

// What T mixes into its seed from CurrentTime ($600142): seconds since 1
// January 1978 shifted left by 3, XOR the microseconds.
std::uint32_t clock_value() {
    using namespace std::chrono;
    constexpr seconds amiga_epoch{252460800}; // 1978-01-01 in Unix time
    const auto now = duration_cast<microseconds>(system_clock::now().time_since_epoch()) - amiga_epoch;
    const auto secs = std::uint32_t(duration_cast<seconds>(now).count());
    const auto micros = std::uint32_t(now.count() % 1000000);
    return secs << 3 ^ micros;
}

class MindWalker final : public app::GameModule {
public:
    explicit MindWalker(const std::filesystem::path& content)
        : files_(content), picture_(platform::read_file(content / "MindB")),
          pointer_(platform::read_file(content / "system-configuration")), assets_(data::load_assets(content)),
          title_(files_, picture_, clock_value()) {}

    // The stages wait for each vertical blank (PAL, 50 Hz, like the reference
    // machine; hypothesis until the release's video standard is settled).
    double step_seconds() const override { return 1.0 / 50.0; }
    double pixel_aspect() const override { return 1.0; }

    void tick(const app::GameInput& input) override {
        if (input.pointer) {
            mouse_x_ = input.pointer_x;
            mouse_y_ = input.pointer_y;
        }
        if (landscape_) {
            const bool press = input.mouse_button && !button_was_down_;
            button_was_down_ = input.mouse_button;
            int gadget = press ? landscape_->gadget_at(mouse_x_, mouse_y_) : -1;
            if (gadget == -2) {
                leave_ = true; // Intuition's close gadget: CLOSEWINDOW quits the stage
                return;
            }
            landscape::LandscapeInput in{input.up, input.down, input.left, input.right, input.fire,
                                         input.mouse_button, mouse_x_, mouse_y_};
            landscape_->frame([&](landscape::InputRead) { return in; }, gadget);
            if (landscape_->over()) leave_ = true;
            return;
        }
        title_.frame({mouse_x_, mouse_y_, input.mouse_button});
        button_was_down_ = input.mouse_button;
        if (title_.phase() == title::TitleStage::Phase::loading && ++loading_frames_ == 100) open_landscape();
    }

    // The landscape stage (A) with the new game's world.
    void open_landscape() {
        const auto& game = *title_.new_game();
        landscape_.emplace(files_, assets_, landscape::LandscapeStage::Start{game.seed, game.stage, game.lives, game.score});
    }

    platform::Image picture() const override {
        auto image = landscape_ ? landscape_->picture() : title_.picture();
        pointer_.draw(image, mouse_x_, mouse_y_);
        return image;
    }

    std::string stopped() const override {
        if (title_.phase() == title::TitleStage::Phase::quit) return "Mind Walker: QUIT.";
        if (leave_) {
            char text[200];
            std::snprintf(text, sizeof text,
                          landscape_->finished()       ? "The way on from the landscape is found (score %u). The tunnel is not rebuilt yet."
                          : landscape_->out_of_lives() ? "The creatures took every life (score %u). What follows is not rebuilt yet."
                                                       : "Left the landscape (score %u). Finds are not rebuilt yet.",
                          landscape_->score());
            return text;
        }
        return {};
    }

private:
    data::GameFiles files_;
    std::vector<std::uint8_t> picture_;
    MousePointer pointer_;
    data::Assets assets_;
    title::TitleStage title_;
    int mouse_x_{160}, mouse_y_{100};
    int loading_frames_{};
    bool button_was_down_{}, leave_{};
    std::optional<landscape::LandscapeStage> landscape_;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<MindWalker>(content);
}

} // namespace mindwalker::game
