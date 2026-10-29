#pragma once
// The shell's UI font as bitmap strikes: a TrueType pixel font ("Terminal
// 16 Unicode", made with Bits'n'Picas) and the shell's icons, turned into
// one-bit glyphs at a few pixel sizes by a local tool (not part of the
// repository). The data is generated into ui_font_data.cpp. The font is monospaced; style.cpp
// draws it.
#include <cstdint>

namespace picarat::shell::style {

// One rasterised size of the font. Every glyph is a `width` x `height` cell
// that holds all of its ink; the pen advances by `advance`.
struct FontStrike {
    int size;      // pixels per em
    int advance;   // pen advance per character
    int width;     // cell width
    int height;    // cell height
    int left;      // the cell's left edge relative to the pen (0 or less)
    int baseline;  // cell rows above the baseline
    int capHeight; // rows of a capital letter above the baseline
    int xHeight;   // rows of a small letter without ascender
    int rowBytes;  // bytes per glyph row
    // Glyph i of kFontCodes starts at i * height * rowBytes; rows top to
    // bottom, most significant bit leftmost.
    const std::uint8_t* bits;
};

// The strikes, smallest first.
extern const FontStrike kFontStrikes[];
extern const int kFontStrikeCount;
// The code points with a glyph, ascending; the same order in every strike.
extern const char32_t* const kFontCodes;
extern const int kFontCodeCount;

} // namespace picarat::shell::style
