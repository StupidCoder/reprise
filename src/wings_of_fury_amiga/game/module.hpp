#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Wings of Fury as a Joyport game module: so far free flight on the first
// mission's map - the Hellcat comes up on the carrier's elevator, takes off,
// flies, turns, climbs into the 1/8-scale view and lands on the wires again;
// crashes blow up, burn, smoke or splash, and the next plane comes up.
namespace wings::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace wings::game
