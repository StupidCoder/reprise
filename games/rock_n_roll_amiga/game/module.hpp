#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Playing Rock 'n Roll in the shell: a level from its start wait through
// rolling, the panel's items, falls with the continue/restart prompt, to
// the exit or a secret warp and on. Steered by the captured mouse's
// movement. Front end, the overview map and music are not rebuilt yet
// (games/rock_n_roll_amiga/plan.md).
namespace rocknroll::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace rocknroll::game
