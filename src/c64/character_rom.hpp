#pragma once
#include "c64/text_frame.hpp"
#include <array>

// Glyphs of the C64's own character ROM that games copy into their memory.
// See character_rom.cpp for the source and licence of this data (it is not
// covered by Joyport's MIT licence).
namespace c64 {

// Screen codes $00-$3F of the upper-case set (ROM $D000-$D1FF), 8 rows each.
extern const std::array<Glyph, 64> character_rom_upper;

} // namespace c64
