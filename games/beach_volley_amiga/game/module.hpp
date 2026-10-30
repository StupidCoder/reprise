#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <memory>

// Beach Volley in the shell: the intro with its music and voices, and the
// players menu. The match that follows the menu is not rebuilt yet
// (games/beach_volley_amiga/plan.md): choosing stops the game with a note.
namespace beach_volley::game {

std::unique_ptr<app::GameModule> start(const std::filesystem::path& content);

} // namespace beach_volley::game
