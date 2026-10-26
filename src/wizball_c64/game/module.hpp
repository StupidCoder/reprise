#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Wizball as a Joyport game module.
namespace wizball::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace wizball::game
