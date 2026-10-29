#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/screen.hpp"
#include "spindizzy_worlds_amiga/view/text.hpp"
#include <cstdint>

// The screen around the playfield: the panel below it (rows 168-199) and
// the two pillars beside it with the fuel gauge.
namespace spindizzy::view {

// $14980 at a world's start: the panel picture (SELECT.SWP + 4), its
// labels (the string at $149A8: " Spindizzy" and "Clue" in the small size)
// and the world's name ($119D8, glyph bank 2, normal size, at column 18).
void draw_panel(Screen&, const data::Assets&, int world);

// $149C6: the compass (32 × 10 at byte column 32, row 179: + $1C18) shows the
// level's rotation; frames at $41670, $A0 each.
void draw_compass(Screen&, const data::Memory& main, int rotation);

// $163BE: the clue boxes for a condition word: the big box (column 4,
// row 180) shows a push button ($2C000, pressed unless "NOT pushed") for
// bit 13, a jewel ($25AD4) for bit 15; the four small ones (from row 173,
// byte 10; table $164AE) the binary mask bits, lit for the value bits.
void draw_clue(Screen&, const data::Memory& main, std::uint16_t word);

// The pillars ($10FC6): two pairs of attached hardware sprites, 16 × 168,
// built from the 16-colour rows at $1E800 (8 top rows, the gauge's 128,
// then 20 and 12 more); a pixel of colour 0 becomes sprite colour 24,
// which the level sets to black ($189D6). The gauge shows `fuel_shown`
// rows from the bottom of the left pillar's gauge, then the right's
// ($10E96: $2072, 0-$100). `right_shift` moves the right pillar further
// right (widescreen: to the wider picture's edge).
void draw_pillars(Screen&, const data::Memory& main, int fuel_shown, int right_shift = 0);

// Sprite colours 16-31 as a level sets them ($189CA): palette 0 again,
// colour 24 black.
void set_level_palette(Screen&, const data::Graphics&, int palette = 0);

} // namespace spindizzy::view
