#pragma once
#include "app/game_module.hpp"
#include <filesystem>
#include <string>

// The options the player chose for a game, kept from one launch to the next
// in <data>/options/<game id>.txt as "name = choice" lines. Options are
// matched by name and choice text, not position, so a later version that
// adds, reorders or renames options never applies a value to the wrong one
// (an unknown line is skipped and that option keeps its default).
namespace app {

// The file's text for the module's current choices.
std::string options_text(const GameModule& module);
// Sets the module's options from such a file (nothing when it is missing).
void restore_options(GameModule& module, const std::filesystem::path& file);

} // namespace app
