#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Bruce Lee as a Joyport game module.
namespace bruce_lee::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace bruce_lee::game
