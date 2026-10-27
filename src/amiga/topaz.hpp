#pragma once

// The Kickstart's topaz 9, the font the Amiga games of the collection draw
// their texts with (see topaz.cpp for its source and licence; it is not
// covered by Joyport's MIT licence).
namespace amiga {

struct Topaz9 {
    // A fixed-width font: every character takes a cell this size. Text()
    // puts the baseline at the pen's y, so a cell's top is y - baseline.
    static constexpr int width = 10, height = 9, baseline = 6;
    // Whether pixel (x, y) of the character's cell is set; characters
    // without a glyph of their own (below $20, $7F-$9F) take the default.
    static bool pixel(unsigned char c, int x, int y);
};

} // namespace amiga
