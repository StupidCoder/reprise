#pragma once
#include "app/game_entry.hpp"
#include <string_view>
#include <vector>

// The puzzle adventures (games/layton): their entries in the Reprise catalog. The engines live in
// ../src and run through their embedding interface (src/embed/layton_embed.hpp); the two screens
// are shown by reprise/dual_screen.*.
namespace layton_reprise {

// The six catalog entries, in the collection's order (Curious Village, Pandora's Box, Lost
// Future, Spectre's Call, Miracle Mask, Azran Legacy). Card texts, release years and the
// collection come from the catalog.
std::vector<app::GameEntry> entries();

} // namespace layton_reprise
