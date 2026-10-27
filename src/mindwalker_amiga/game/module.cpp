#include "mindwalker_amiga/game/module.hpp"
#include "mindwalker_amiga/data/game_files.hpp"
#include "mindwalker_amiga/screen/canvas.hpp"
#include "mindwalker_amiga/title/title_stage.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include "mindwalker_amiga/landscape/view_scene.hpp"
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
          pointer_(platform::read_file(content / "system-configuration")),
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
            // Walking is not rebuilt yet: a click or fire ends the visit.
            if ((input.mouse_button && !button_was_down_) || input.fire) leave_ = true;
            button_was_down_ = input.mouse_button;
            return;
        }
        title_.frame({mouse_x_, mouse_y_, input.mouse_button});
        button_was_down_ = input.mouse_button;
        if (title_.phase() == title::TitleStage::Phase::loading && ++loading_frames_ == 100) open_landscape();
    }

    // The landscape's opening view (A's set-up: the view for the cell left of
    // the start, built twice; facing north; drawn from row 10).
    void open_landscape() {
        const auto& game = *title_.new_game();
        const auto& a = files_.program(data::Stage::landscape);
        world_.emplace(game.seed, a);
        landscape::ViewInputs in;
        in.stage = game.stage;
        in.home_x = world_->start_x();
        in.home_y = world_->start_y();
        in.colour17 = 0xF60; // hypothesis: as found at the start in the reference runs
        in.colour18 = 0xF00;
        const auto x = std::uint16_t((world_->start_x() - 1) & 0xFFF), y = world_->start_y();
        const auto first = landscape::build_view(*world_, a, x, y, in, landscape::stored_colours(a));
        in.previous_x = x;
        in.previous_y = y;
        const auto view = landscape::build_view(*world_, a, x, y, in, first.colours);
        landscape_.emplace();
        landscape::ViewPainter(a).paint(*landscape_, view, 0, world_->floor_level(), world_->ceiling_level(), 10);
        for (const auto c : view.colours) landscape_colours_.push_back(c & 0xFFF);
    }

    platform::Image picture() const override {
        auto image = landscape_ ? landscape_->picture(landscape_colours_) : title_.picture();
        pointer_.draw(image, mouse_x_, mouse_y_);
        return image;
    }

    std::string stopped() const override {
        if (title_.phase() == title::TitleStage::Phase::quit) return "Mind Walker: QUIT.";
        // The landscape is shown until a click; then explain.
        if (leave_) {
            const auto& game = *title_.new_game();
            const world::World world(game.seed, files_.program(data::Stage::landscape));
            char text[200];
            std::snprintf(text, sizeof text,
                          "World %08X (start %03X, %03X): the landscape's view is rebuilt; walking in it is not yet.",
                          game.seed,
                          world.start_x(), world.start_y());
            return text;
        }
        return {};
    }

private:
    data::GameFiles files_;
    std::vector<std::uint8_t> picture_;
    MousePointer pointer_;
    title::TitleStage title_;
    int mouse_x_{160}, mouse_y_{100};
    int loading_frames_{};
    bool button_was_down_{}, leave_{};
    std::optional<world::World> world_;
    std::optional<screen::Canvas> landscape_;
    std::vector<screen::Rgb4> landscape_colours_;
};

} // namespace

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<MindWalker>(content);
}

} // namespace mindwalker::game
