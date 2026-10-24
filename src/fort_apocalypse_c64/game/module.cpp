#include "fort_apocalypse_c64/game/module.hpp"
#include "c64/program.hpp"
#include "platform/files.hpp"
#include <algorithm>

namespace fort_apocalypse::game {

namespace {
// The border as a PAL screen shows it around the display window.
constexpr int margin_x = 32, margin_y = 36;
} // namespace

Module::Module(data::Assets assets, data::GameTables tables)
    : assets_(std::make_unique<const data::Assets>(std::move(assets))),
      tables_(std::make_unique<const data::GameTables>(std::move(tables))),
      game_(std::make_unique<Game>(*assets_, *tables_)), enhanced_(*assets_, *tables_) {
    shown_ = game_->picture();
}

void Module::tick(const app::GameInput& input) {
    game_->tick(read_controls(input), options_);
    draw();
}

void Module::set_canvas(int width, int height) {
    wide_ = widescreen_ ? enhanced::wide_size(width, height, pixel_aspect()) : enhanced::WideSize{};
}

void Module::draw() {
    if (particles_ || smooth_) {
        enhanced_.update(*game_, {particles_, smooth_, wide_.columns});
        shown_ = enhanced_.picture();
    } else if (wide_.columns > 0) {
        shown_ = enhanced::wide_frame(*game_, wide_.columns).rasterise();
    } else {
        shown_ = game_->picture();
    }
}

platform::Image Module::picture() const {
    if (!widescreen_ || wide_.width == 0) {
        return shown_.to_image(margin_x, margin_y);
    }
    // Widescreen: the picture to the window's edges, cut to its shape about
    // the middle (no side border; some border above and below only in
    // windows narrower than the display window's own shape).
    const int width = std::min(shown_.width, wide_.width);
    return shown_.to_image(0, wide_.margin_y, (shown_.width - width) / 2, width);
}

void Module::audio(std::span<float> stereo, int sample_rate) {
    mono_.resize(stereo.size() / 2);
    game_->sid().render(mono_, sample_rate);
    for (std::size_t i = 0; i < mono_.size(); ++i) {
        stereo[2 * i] = stereo[2 * i + 1] = mono_[i];
    }
}

bool Module::save() {
    saved_ = std::make_unique<Game>(*game_);
    return true;
}

bool Module::load() {
    if (!saved_) {
        return false;
    }
    game_ = std::make_unique<Game>(*saved_);
    enhanced_.reset();
    shown_ = game_->picture();
    return true;
}

std::vector<app::GameOption> Module::options() const {
    return {{"Enemy pilot",
             {"Original", "Fixed"},
             options_.fixed_enemy_climb ? 1 : 0,
             "Original: the enemy helicopter checks the wrong spot before it climbs (a corrupt "
             "byte in the game) and often flies into the rock. Fixed: it checks its own path."},
            {"Particle effects",
             {"Off", "On"},
             particles_ ? 1 : 0,
             "Debris and fire from hits, tossed tank turrets, and shot-down mines and craft "
             "tumbling to the ground. The game plays exactly the same."},
            {"Smooth motion",
             {"Off", "On"},
             smooth_ ? 1 : 0,
             "Smooth banking; tanks, mines and shells glide instead of jumping a character at "
             "a time, and tanks rock over rough ground. The game plays the same."},
            {"Widescreen",
             {"Off", "On"},
             widescreen_ ? 1 : 0,
             "Fills the window to its edges and shows more of the map left and right, up to "
             "21:9. The game plays exactly the same."}};
}

void Module::set_option(std::size_t index, int value) {
    if (index == 0) {
        options_.fixed_enemy_climb = value == 1;
    } else if (index == 1) {
        particles_ = value == 1;
    } else if (index == 2) {
        smooth_ = value == 1;
    } else if (index == 3) {
        widescreen_ = value == 1;
        if (!widescreen_) {
            wide_ = {};
        }
    }
    if (!particles_ && !smooth_) {
        draw(); // the effects' picture follows with the next frame
    }
}

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    const auto program = c64::Program::from_prg(platform::read_file(content / "game.prg"));
    return std::make_unique<Module>(data::decode_assets(program), data::decode_tables(program));
}

} // namespace fort_apocalypse::game
