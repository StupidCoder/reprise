#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Wings of Fury as a Joyport game module: so far free flight on the first
// mission's map - the Hellcat starts on the carrier's deck, takes off, flies,
// turns, climbs into the 1/8-scale view and lands on the carrier again.
namespace wings::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace wings::game
