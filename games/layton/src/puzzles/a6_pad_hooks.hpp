#pragma once
// The sixth adventure's pad hooks (1110, library 0x182b2c -> 0x258f34): a
// table of up to 32 slots at game +0xc48 (0x3c bytes each: the button
// mask, an event kind at +0x29, a block by name at +0x24 and up to four
// arguments; the owner is the scope depth at game +0x1484 and 0x20af1c
// drops a scope's hooks). Common_PadAssign registers
// Common_PadAssignIntr, which asks a check block (Nazo_AssignCtrl) and then
// calls the handler (__Nazo_ArwTouch for the arrow pads). The dispatch was
// not found in the code; the port calls a hook's block when one of its
// buttons goes down (hypothesis: the arrow pads register kind 2, the
// centre kind 4; their handler loops by itself while the button is held).
#include "system/a5_game.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace picarat::adventure5 {

struct PadHook {
    std::uint32_t mask = 0; // buttons in the game's order (Input::buttons)
    int kind = 0;
    std::string block;
    std::vector<xq::Value> arguments;
};

// The game's hooks (the puzzle loops clear them when a puzzle starts).
std::vector<PadHook>& padHooks(const Game& game);

// Calls the hooks whose buttons went down this frame.
void runPadHooks(Game& game);

} // namespace picarat::adventure5
