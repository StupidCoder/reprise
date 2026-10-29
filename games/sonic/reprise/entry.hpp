#pragma once
#include "app/game_entry.hpp"
#include <vector>

// BlueStreak's games in the Reprise catalog: Sonic the Hedgehog, Sonic the Hedgehog 2 and
// Sonic Triple Trouble (Game Gear) and Sonic Advance (Game Boy Advance). The engines live in
// ../src and run through their embedding interface (src/embed/bluestreak_embed.hpp).
namespace sonic_reprise {

// The four entries, in the collection's order: sonic_gg, sonic2_gg, triple_trouble_gg,
// sonic_advance_gba.
std::vector<app::GameEntry> entries();

} // namespace sonic_reprise
