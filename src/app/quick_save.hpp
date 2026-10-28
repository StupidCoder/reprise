#pragma once
#include "app/game_entry.hpp"
#include <optional>

namespace app {
// Saves live outside imported files, so removing/reimporting a game keeps
// progress. Only matching imported content and schema can continue it.
bool persistent_saves(std::string_view game);
struct SavePreview {
    std::optional<platform::Image> image;
    std::string date;
    std::string error;
    bool available{};
};
SavePreview save_preview(const std::filesystem::path &content, std::string_view game);
std::unique_ptr<GameModule> with_quick_save(const GameEntry &, const std::filesystem::path &content);
} // namespace app
