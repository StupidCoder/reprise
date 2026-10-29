#pragma once
#include "app/game_entry.hpp"

// Spy Mouse (iOS): the game's entry in the Reprise catalog. The engine lives in ../src and
// runs through its embedding interface (src/embed/spy_mouse_embed.hpp).
namespace spy_mouse_reprise {

app::GameEntry entry();

} // namespace spy_mouse_reprise
