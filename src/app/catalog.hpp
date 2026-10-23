#pragma once
#include "app/game_entry.hpp"
#include <vector>

namespace app {

// Every game in the collection, in launcher order: playable and importable
// games first within each machine, then planned ones.
std::vector<GameEntry> catalog();

const char* machine_name(Machine);

} // namespace app
