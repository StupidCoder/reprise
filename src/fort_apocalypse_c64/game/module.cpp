#include "fort_apocalypse_c64/game/module.hpp"
#include "c64/program.hpp"
#include "platform/files.hpp"

namespace fort_apocalypse::game {

Module::Module(data::Assets assets, data::GameTables tables)
    : assets_(std::make_unique<const data::Assets>(std::move(assets))),
      tables_(std::make_unique<const data::GameTables>(std::move(tables))),
      game_(std::make_unique<Game>(*assets_, *tables_)), enhanced_(*assets_, *tables_) {}

void Module::tick(const app::GameInput& input) {
    game_->tick(read_controls(input), options_);
    if (enhanced_mode_) {
        enhanced_.update(*game_);
    }
}

platform::Image Module::picture() const {
    // The border as a PAL screen shows it around the display window.
    return (enhanced_mode_ ? enhanced_.picture() : game_->picture()).to_image(32, 36);
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
    return true;
}

std::vector<app::GameOption> Module::options() const {
    return {{"Enemy pilot",
             {"Original", "Fixed"},
             options_.fixed_enemy_climb ? 1 : 0,
             "Original: the enemy helicopter checks the wrong spot before it climbs (a corrupt "
             "byte in the game) and often flies into the rock. Fixed: it checks its own path."},
            {"Enhanced mode",
             {"Off", "On"},
             enhanced_mode_ ? 1 : 0,
             "Smooth banking, pitching tanks, flying debris and tumbling turrets, drawn on the "
             "C64's own pixels and colours. The game plays exactly the same."}};
}

void Module::set_option(std::size_t index, int value) {
    if (index == 0) {
        options_.fixed_enemy_climb = value == 1;
    } else if (index == 1) {
        if (!enhanced_mode_ && value == 1) {
            enhanced_.reset();
        }
        enhanced_mode_ = value == 1;
    }
}

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    const auto program = c64::Program::from_prg(platform::read_file(content / "game.prg"));
    return std::make_unique<Module>(data::decode_assets(program), data::decode_tables(program));
}

} // namespace fort_apocalypse::game
