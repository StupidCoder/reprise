#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Playing Tower of Babel in the shell: the front end from the title to the
// towers (front/front_end.hpp), one PAL frame a tick, with the mouse.
namespace babel::front {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace babel::front
