#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Marble Madness as a Joyport game module.
namespace marble_madness::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace marble_madness::game
