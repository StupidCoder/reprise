#include "spy_mouse_ios/reprise/entry.hpp"
#include "spy_mouse_embed.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace spy_mouse_reprise {
namespace {
namespace fs = std::filesystem;

// The campaign progress lives next to the quick saves, outside the imported files, so a new
// import keeps it (<data>/progress/spy_mouse_ios/progress.txt).
fs::path progress_file(const fs::path& data) { return data / "progress" / "spy_mouse_ios" / "progress.txt"; }

// The game drawn with Reprise's renderer at the window's resolution (its 480 x 320 play area
// scaled), ticking at the original's fixed 17 ms. Pointer events reach it in order.
class Module final : public app::GameModule {
public:
    explicit Module(std::unique_ptr<spy_mouse::Game> game) : game_(std::move(game)) {}

    double step_seconds() const override { return spy_mouse::kTickSeconds; }

    void tick(const app::GameInput& input) override {
        for (const auto& e : input.pointer_events) {
            const auto kind = e.kind == platform::PointerEvent::Kind::down ? spy_mouse::Game::Touch::Down
                              : e.kind == platform::PointerEvent::Kind::move ? spy_mouse::Game::Touch::Move
                                                                               : spy_mouse::Game::Touch::Up;
            game_->touch(kind, e.x, e.y);
        }
        game_->tick();
    }

    std::optional<std::array<int, 2>> gpu_frame_size() const override {
        return std::array<int, 2>{spy_mouse::kPlayWidth, spy_mouse::kPlayHeight};
    }
    void draw_gpu(const platform::GpuFrame& frame) override {
        game_->draw(frame.renderer, frame.target, frame.width, frame.height);
    }
    void attach_gpu(platform::GpuSurface* surface) override { surface_ = surface; }

    // A one-off picture (save previews): drawn into an offscreen target and read back. Drawing
    // changes nothing in the game.
    platform::Image picture() const override {
        if (!surface_) {
            throw std::runtime_error("The game's picture can only be taken while it is shown.");
        }
        return surface_->capture(spy_mouse::kPlayWidth, spy_mouse::kPlayHeight, [&](const platform::GpuFrame& frame) {
            game_->draw(frame.renderer, frame.target, frame.width, frame.height);
        });
    }
    double pixel_aspect() const override { return 1.0; }

    void audio(std::span<float> stereo, int sample_rate) override {
        game_->audio(stereo.data(), int(stereo.size() / 2), sample_rate);
    }

    std::vector<std::uint8_t> write_state() override { return game_->saveState(); }
    void read_state(std::span<const std::uint8_t> state) override {
        std::string error;
        if (!game_->restore(state, error)) {
            throw std::runtime_error(error);
        }
    }

    std::string stopped() const override { return game_->failure(); }
    bool finished() const override { return game_->left(); }

    // The framework's screen filter; Reprise draws it over the game's frame (presentation only:
    // the game, its sound and saves never see it).
    std::vector<app::GameOption> options() const override { return {app::screen_filter_option(vhs_)}; }
    void set_option(std::size_t index, int value) override {
        if (index == 0) vhs_ = std::clamp(value, 0, ui::vhs_levels - 1);
    }
    int vhs_level() const override { return vhs_; }

private:
    std::unique_ptr<spy_mouse::Game> game_;
    platform::GpuSurface* surface_{};
    int vhs_{}; // the VHS look (0: off)
};

app::ImportResult import_ipa(std::span<const std::uint8_t> image) {
    if (!spy_mouse::looksLikeIpa(image)) {
        throw app::NotThisGame("not an iPhone app archive");
    }
    app::ImportResult result;
    try {
        for (auto& file : spy_mouse::importIpa(image, result.release)) {
            result.files.push_back({std::move(file.name), std::move(file.bytes)});
        }
    } catch (const spy_mouse::NotSpyMouse& e) {
        throw app::NotThisGame(e.what());
    }
    return result;
}

std::vector<app::Preview> previews(const fs::path& content) {
    std::vector<app::Preview> out;
    for (auto& picture : spy_mouse::previews(content)) {
        platform::Image image(picture.width, picture.height);
        std::memcpy(image.pixels.data(), picture.rgba.data(), std::min(picture.rgba.size(), image.pixels.size() * 4));
        out.push_back({picture.title, std::move(image)});
    }
    return out;
}

// The standalone DoubleOCheese app's import and progress, taken over once.
std::optional<app::LegacyCopy> legacy(const fs::path& data) {
    const fs::path old = spy_mouse::legacyDataDirectory();
    std::error_code error;
    const bool has_progress = fs::exists(old / "progress.txt", error);
    const bool has_import = fs::exists(old / "game" / "imported.txt", error);
    if (!has_progress && !has_import) {
        return std::nullopt;
    }
    app::LegacyCopy copy;
    if (has_progress) {
        copy.copies.emplace_back(old / "progress.txt", progress_file(data));
    }
    if (has_import) {
        try {
            app::ImportResult result;
            for (auto& file : spy_mouse::readImportedFolder(old / "game", result.release)) {
                result.files.push_back({std::move(file.name), std::move(file.bytes)});
            }
            copy.import = std::move(result);
        } catch (const std::exception& e) {
            copy.notes.push_back(std::string("Spy Mouse: DoubleOCheese's imported copy cannot be used here (") + e.what() +
                                 "). Import the Spy Mouse 1.1.1 IPA; your DoubleOCheese files were left as they are.");
        }
    }
    return copy;
}
} // namespace

app::GameEntry entry() {
    return {
        .id = "spy_mouse_ios",
        .title = "Spy Mouse",
        .machine = app::Machine::ios,
        .media = "the app archive (IPA) of version 1.1.1",
        .import = import_ipa,
        .largest_image = 512u << 20,
        .previews = previews,
        .start =
            [](const fs::path& content) -> std::unique_ptr<app::GameModule> {
                return std::make_unique<Module>(
                    spy_mouse::Game::start(content, progress_file(content.parent_path().parent_path())));
            },
        .legacy = legacy,
        .progress = "Worlds 1 to 3 and most of world 4 (all but the secret level 4-S2) from the world map: drawing "
                    "paths, cheese, cats, holes, floors, switches, the boss chases and fights, results and goals, "
                    "with music and sound. The rules were reconstructed and checked on version 1.0.5; version 1.1.1's "
                    "changes are still being compared. Options: VHS look. Quick save.",
    };
}

} // namespace spy_mouse_reprise
