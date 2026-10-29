#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// The playable part so far: a test drive of one level at a time with the
// explorer's movement, its losses and respawns, and the original's
// camera. The level's objects are drawn as they start and do not act yet;
// there is no panel, energy, sound or front end yet.
namespace spindizzy::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace spindizzy::game
