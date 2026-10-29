#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// F/A-18 Interceptor as a Reprise game module: so far free flight from the
// main menu's option 2 - the F/A-18 on one of the bay's runways, the
// original's flight model tick for tick, the cockpit view of the world with
// the instrument panel's numbers and message line, the original's sounds,
// its crash (the flash, five aircraft). No menus, weapons or other aircraft
// yet.
namespace interceptor::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace interceptor::game
