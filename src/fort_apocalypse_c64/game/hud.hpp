#pragma once
#include "c64/text_frame.hpp"
#include "fort_apocalypse_c64/data/assets.hpp"

namespace fort_apocalypse::game {

// Drawing helpers shared by the game's screens. Screen rows count from the
// top of the 40x25 character screen. They set characters only; each screen
// sets its cell colours separately (colour RAM is independent of the
// characters on the C64, and the game sets the two at different times).

// A glyph of the HUD character set. Codes $80 and up fall in the part of
// the set's memory the init clears ($5400-$57FF), so they are empty.
const c64::Glyph* hud_glyph(const data::Assets&, std::uint8_t code);

// A text in the double-width font ($9B56): each code is drawn as itself and
// the code $20 higher in the next cell; $00 is one empty cell. Returns the
// column after the text.
int draw_text(c64::TextFrame&, const data::Assets&, const data::Text&, int column, int row);

// The score, fuel and bonus counters (decimal, as the original's BCD bytes).
struct Counters {
    int score{}; // 6 digits
    int fuel{};  // 4 digits
    int bonus{}; // 4 digits
};

// The HUD rows 0-5: the HUD picture with the counters drawn into it
// ($9BC6, $A92F). `with_counters` is false on the first frame after the HUD
// appears, before the interrupt first draws the numbers.
void draw_hud(c64::TextFrame&, const data::Assets&, const Counters&, bool with_counters);

} // namespace fort_apocalypse::game
