#pragma once
#include "fort_apocalypse_c64/data/assets.hpp"
#include "platform/image.hpp"

// Pictures of the decoded data, for checking it (launcher asset viewer,
// export tool). These are not the game's screen: nothing is animated and
// the run-time changes the game makes are not applied.
namespace fort_apocalypse::data {

// The colours a picture is shown with, as C64 colour numbers. Multicolour
// cells use background (pair 00), shared1 (01), shared2 (10) and the low
// three bits of the cell colour (11); a cell colour below 8 shows the
// character in single colour instead.
struct Colours {
    int background{0}, shared1{2}, shared2{1}, cell{13};
};

// The colours seen during play on level `level` (playfield rows): black
// background, the level's colour, white, and green cells. Observed in the
// emulator run (see the notes); the game code sets them.
Colours play_colours(const Assets&, int level);

// All 128 characters as a 16x8 grid of 8x8 cells.
platform::Image charset_sheet(const Charset&, const Colours&);
// The helicopter poses, one row per pose (from the tilt table, full left to
// full right), the two rotor frames side by side, with double-width pixels.
platform::Image helicopter_sheet(const Assets&, int colour);
// A level's terrain: 215 columns x 40 rows of characters.
platform::Image terrain_image(const Assets&, int level);
// A level's radar picture: 160 x 40 pixel pairs, double-width pixels.
platform::Image radar_image(const Assets&, int level);
// The HUD rows with the HUD charset and the colours seen during play.
platform::Image hud_image(const Assets&, int level);

} // namespace fort_apocalypse::data
