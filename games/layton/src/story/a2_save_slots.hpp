// Saving to and loading from the three save slots of the cartridge image
// (system/a2_save_file.hpp): the game state side of ov2 02083638 / 020837fc.
#pragma once
#include "story/a2_game_state.hpp"
#include "system/a2_save_file.hpp"
#include <string>

namespace picarat {
// 02083638: writes the progress block to `slot`. With `update` (every save
// from the file dialog) the slot's summary is refreshed: used, the player's
// name, place, play time, puzzles found and solved; the finished-game level
// is always refreshed. The caller has already folded the play time into the
// progress block when `update` is set (02083ff4).
void saveToSlot(SaveFile& file, GameState& state, int slot, bool update, const std::string& name,
                std::uint32_t seconds);

// 020837fc: reads the slot's progress block into the state, the player's
// name from its summary and the finished-game flag (+0x821e).
void loadFromSlot(const SaveFile& file, GameState& state, int slot, std::string& name);
} // namespace picarat
