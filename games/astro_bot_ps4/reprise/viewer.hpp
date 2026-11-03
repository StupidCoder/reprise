#pragma once
#include "app/game_entry.hpp"
namespace beacon::reprise {
std::vector<std::filesystem::path> texture_dependencies(const std::filesystem::path& game);
std::vector<app::Preview> previews(const std::filesystem::path& content);
}
