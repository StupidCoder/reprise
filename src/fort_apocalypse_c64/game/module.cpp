#include "fort_apocalypse_c64/game/module.hpp"

namespace fort_apocalypse::game {

Module::Module(data::Assets assets)
    : assets_(std::make_unique<const data::Assets>(std::move(assets))), front_end_(*assets_) {}

void Module::tick(const app::GameInput& input) {
    if (!stopped_.empty()) {
        return;
    }
    switch (front_end_.tick(read_controls(input))) {
    case FrontEnd::Outcome::new_game:
        stopped_ = "Fort Apocalypse: starting a game is not rebuilt yet.";
        break;
    case FrontEnd::Outcome::attract_game:
        stopped_ = "Fort Apocalypse: the attract game is not rebuilt yet.";
        break;
    case FrontEnd::Outcome::none:
        break;
    }
}

c64::Picture Module::display() const {
    c64::TextFrame frame;
    front_end_.draw(frame);
    return frame.rasterise();
}

platform::Image Module::picture() const {
    // The border as a PAL screen shows it around the display window.
    return display().to_image(32, 36);
}

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content) {
    return std::make_unique<Module>(data::load_assets(content));
}

} // namespace fort_apocalypse::game
