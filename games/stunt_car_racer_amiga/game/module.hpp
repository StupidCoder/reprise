#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Stunt Car Racer in the shell: for now a practice race on the first circuit
// with the game's own car physics (game.md, "The race"); laps, damage and the
// opponent follow (plan.md).
namespace stuntcar::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace stuntcar::game
