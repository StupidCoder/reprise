#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// The game from its title: the front end (attract pages, menu, Easy/Hard
// choice, world map) and the worlds' levels (game/play_screen). Not
// rebuilt yet: sound, saved games, the statistics after a world, the
// reserve-fuel offer at a game over, and the moving creatures.
namespace spindizzy::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace spindizzy::game
