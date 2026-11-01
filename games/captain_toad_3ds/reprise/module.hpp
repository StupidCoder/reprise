// Campaign factory: starts at the opening unless restored from a checkpoint.
// Reprise owns presentation and input.
#pragma once
#include "app/game_module.hpp"
#include <filesystem>
namespace captain_toad::reprise {
std::unique_ptr<::app::GameModule> opening(const std::filesystem::path& content);
}
