#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>
#include <string>

// The playable game for the launcher: a mission's phase from its map and
// placements, run on the rebuilt rules (.analysis/cannon_fodder_amiga/
// game.md), one frame at a time like the original (a game tick every third
// frame), with its view, panel and sound.
namespace fodder::game {

// `first_phase` < 0: a new game with the presentation and the screens
// between phases; else straight into that phase (tools).
std::unique_ptr<app::GameModule> start_game(const std::filesystem::path& content, int first_phase = -1);
// For tools: the phase, the outcome and the counts, in one line.
std::string describe(const app::GameModule& game);

} // namespace fodder::game
