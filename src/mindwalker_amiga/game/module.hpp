#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Mind Walker as a Joyport game module: so far the title stage (title
// picture, world menu, new game) up to the loading picture of the landscape.
namespace mindwalker::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace mindwalker::game
