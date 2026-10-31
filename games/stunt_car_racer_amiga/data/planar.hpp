#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The program's 320 x 200 four-plane pictures, decoded to colour indices
// (game.md, "Race screen" and "Front end").
namespace stuntcar::data {

constexpr int screen_width = 320, screen_height = 200;

// PackBits rows ($593F8): each screen row holds the four planes' 40 bytes in
// turn.
std::vector<std::uint8_t> unpack_rows(const Program&, std::uint32_t at);

// A picture stored plainly with the planes interleaved a word at a time
// (one word of planes 0-3 per 16 pixels, 160 bytes a row; $59390).
std::vector<std::uint8_t> read_interleaved(const Program&, std::uint32_t at);

// A colour word with three bits a gun, widened to 2c + 1 as the game loads
// its palettes ($1BA82).
Colour widen3(std::uint16_t word);

// Sixteen colour words, widened.
std::vector<Colour> read_palette(const Program&, std::uint32_t at);

// The dashboard font (8 bytes a character from ' ') as a new game leaves it
// ($5AA02): '-' gets a longer bar, '.' becomes a single pixel on row 6 and
// '_' moves to row 6. The menus and the dashboard both print with it.
using Font = std::array<std::array<std::uint8_t, 8>, 96>;
Font read_font(const Program&);

} // namespace stuntcar::data
