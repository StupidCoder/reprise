// Intuition's screen depth-arrangement gadgets, which every Intuition screen
// with a title bar shows at its right end (Mind Walker's loading picture
// shows them on the set-up stage's screen).
//
// NOT COVERED BY JOYPORT'S MIT LICENCE. This is imagery of Commodore's
// Kickstart ROM (rights now held by others; Cloanto licenses the Amiga
// ROMs). It is included here at the project owner's decision (2026-10-27,
// on the same terms as the topaz fonts in topaz.cpp) because the game shows
// it and it is not on the game's disk; only these two images are included.
// Source: Kickstart 1.3 (34.5; SHA-256 of the 256 KiB ROM
// ee05862d8102a08436ac4056da7d549db31625c7d47b24dfb7b3c9a5c113ca53):
// Intuition's screen gadgets at $FD61EE / $FD61C2 (GRELRIGHT, left -33 and
// -16, 17 x 10), their Images at $FD6026 / $FD6012 (depth 2, PlanePick 3)
// with data at $FD5E32 / $FD5E82 (two planes of ten rows of two words).
// Identified from the pixels of the original's screen
// (.analysis/mindwalker_amiga/game.md, "Loading picture").

#include "amiga/intuition_imagery.hpp"

namespace amiga {

const std::array<ScreenDepthGadget, 2>& screen_depth_gadgets() {
    static const std::array<ScreenDepthGadget, 2> gadgets = {{
    // The depth gadget at -33 (image $FD5E32).
    {-33, {{
        {0x3FFF8000, 0x301F8000, 0x339F8000, 0x33018000, 0x33018000, 0x33018000, 0x30018000, 0x3F018000, 0x3F018000, 0x3FFF8000},
        {0x00000000, 0x00000000, 0x00000000, 0x00FE0000, 0x00FE0000, 0x00FE0000, 0x00FE0000, 0x00FE0000, 0x00FE0000, 0x00000000},
    }}},
    // The depth gadget at -16 (image $FD5E82).
    {-16, {{
        {0x3FFF8000, 0x301F8000, 0x30018000, 0x30398000, 0x30398000, 0x30398000, 0x30398000, 0x3F398000, 0x3F018000, 0x3FFF8000},
        {0x00000000, 0x0FE00000, 0x0F000000, 0x0F000000, 0x0F000000, 0x0F000000, 0x0F000000, 0x00000000, 0x00000000, 0x00000000},
    }}},    }};
    return gadgets;
}

} // namespace amiga
