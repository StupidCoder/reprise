#pragma once
#include "platform/image.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// Game Gear video helpers shared by every Game Gear game in BlueStreak.
//
// The Game Gear VDP ("mode 4") draws 8x8 tiles with 4 bits per pixel. Colour
// indices select one of 32 entries in colour RAM (CRAM): 16 for the background
// and 16 for sprites. Games commonly rewrite CRAM in the middle of a frame (for
// example Labyrinth's underwater palette below the water line), so a rendered
// screen carries a list of mid-frame palette changes.
namespace gg {

using platform::Color;

// The 32 colours of Game Gear CRAM: entries 0-15 background, 16-31 sprites.
using Palette = std::array<Color, 32>;

// A rendered Game Gear frame (or layer of one). Pixels are written by colour
// index so that mid-frame palette changes can be honoured; the result is still
// an ordinary RGBA image for the host.
struct Screen : platform::Image {
    using Image::Image;

    // Raster palette changes, sorted by the first screen row they affect. Rows
    // above the first entry use the base palette passed to put_index().
    std::vector<std::pair<int, Palette>> palette_changes;

    // Writes colour `index` using the palette in effect on row `y`.
    void put_index(int x, int y, int index, const Palette& base);
};

// Decodes 64 bytes of CRAM. Each entry is a little-endian word 0000BBBB GGGGRRRR;
// every 4-bit channel is expanded to 8 bits (0x0..0xF -> 0..255).
Palette decode_palette(std::span<const std::uint8_t> bytes);

// Reads one pixel of a tile in VDP planar format. Each 8-pixel row is stored as
// four bytes (bit planes 0..3); the leftmost pixel is the most significant bit.
std::uint8_t tile_pixel(std::span<const std::uint8_t> patterns, unsigned tile, int x, int y);

// Draws one 8x8 tile at (x, y). palette_base selects the background (0) or
// sprite (16) half of CRAM. With `transparent`, colour index 0 is not drawn.
void draw_tile(Screen& screen, std::span<const std::uint8_t> patterns, unsigned tile, int x, int y,
               const Palette& palette, int palette_base = 0, bool transparent = false);

} // namespace gg
