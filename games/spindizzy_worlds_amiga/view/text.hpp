#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/playfield.hpp"
#include <cstdint>

// The game's text printer ($18518): every text on screen is a byte string
// in main with control codes, printed through one routine, so the port
// prints the game's own strings with the same rules.
//
// The cursor is a byte column (8 pixels) and a pixel row; glyphs are 8
// pixels wide in four colours planes, 16 rows ($37A6 + 1) or, in the
// small mode, 8 of their rows plus the last one. Codes below $20:
//   $00, $10  normal size (16 rows, lines of 16)   $11  small (9 rows, lines of 9)
//   $01-$07   the glyph bank: $1B400 + (code − 1) × $1A00 ($01 the usual one, $02 orange)
//   $08       back one column                     $09, $0C  skip a column
//   $0A       down a line                         $0B  up a line
//   $0D       to column 0 of the next line        $0E n  right n − 1 columns
//   $0F n     down n rows                         $12 / $13  transparent on / off
//   $14 n     line height n                       $1B n  column offset n until the string ends
//   $1C / $1D draw to the shown / hidden buffer   $1E s x y w h  a box (style s)
//   $1F x y   cursor to column x, row y
// Space draws the blank glyph unless transparent. A string ends at 0.
namespace spindizzy::view {

class TextPrinter {
public:
    explicit TextPrinter(const data::Memory& main) : main_(main) { reset(); }

    // $184E2: the printer's state at boot (normal size, transparent, the
    // usual bank, cursor 0, 0).
    void reset();

    // Prints a string starting at `address` in main ($18518).
    void print(Canvas&, std::uint32_t address);
    // Prints bytes (the same rules) from a buffer.
    void print(Canvas&, const std::uint8_t* text);
    // One byte ($1856C: the cursor kept).
    void put(Canvas&, std::uint8_t byte);
    // A number in decimal ($18826: without leading zeros) at the cursor.
    void number(Canvas&, int value);

    // $18582: a box (s: fill colour in the low nibble, border colour in
    // the high one, 0 = none) at column x, row y, w columns, h rows.
    static void box(Canvas&, int style, int x, int y, int w, int h);

    int column{}, row{};          // $37A0, $37A2
    int height() const { return height_; } // $37A6
    bool transparent{true};       // $37AC
    bool to_hidden{true};         // $379C: the hidden buffer ($4(a5)) or the shown one

private:
    void glyph(Canvas&, int code, int column, int row) const;

    const data::Memory& main_;
    std::uint32_t bank_{};        // $3794
    int height_{};                // $37A6: rows − 1
    int line_{};                  // $37A8
    int skip_{};                  // $37A4: extra bytes per glyph row (small: every other row)
    int column_offset_{};         // $3792 (its low byte $3793)
};

} // namespace spindizzy::view
