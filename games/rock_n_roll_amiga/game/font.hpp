#pragma once
#include "rock_n_roll_amiga/data/assets.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The game's text: the print routine ($6AE16/$6AE8A) and its font in the
// graphics bank (rows 600 and 608, in every world's bank since the program
// holds them). Glyph bytes replace one bitplane of eight pixels at a time,
// so a text in plane 2 adds 4 to the colour index of what lies below it
// and a text in plane 0 adds 1.
namespace rocknroll::game {

// A text in the original's format: a column byte (8 pixels), a row byte
// (8 lines), then the characters up to $FF; and the plane its glyphs go
// into ($6AE12: $50 plane 2, 0 plane 0).
struct ScreenText {
    std::string text;
    int plane{2};
};

// A picture of colour indices that texts are printed into.
struct Canvas {
    std::uint8_t* pixels;
    int width, height;
};

// One glyph (font index: character − $27) at x, y, `lines` high, into `plane`.
void draw_glyph(Canvas, const data::GraphicsBank& font, int x, int y, int index, int lines, int plane);
// The characters of a text from x, y: a space shows glyph $28; characters
// above $4E come from the second font row.
void print(Canvas, const data::GraphicsBank& font, int x, int y, std::string_view characters, int plane);
// A text in the original's format, with its column and row counted from
// the origin (the destination address + $644 in the original).
void print_at(Canvas, const data::GraphicsBank& font, int origin_x, int origin_y, const ScreenText&);

// Builds a text in the original's format.
ScreenText text_at(int column, int row, std::string_view characters, int plane = 2);
// A text of the program's: column, row and characters up to $FF.
ScreenText read_text(const data::Program&, std::uint32_t address, int plane = 2);

} // namespace rocknroll::game
