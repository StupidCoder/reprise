#pragma once
// Pictures of the 3DS adventures (IMGC, import/a56_formats.hpp) decoded to
// plain RGBA, one mipmap level at a time.
//
// An IMGC picture is a map of 8x8 cells, each naming one of the picture's
// deduplicated tiles (or none), for every mipmap level in turn. Tiles of the
// uncompressed formats hold 64 pixels in Z order with y on the even bits;
// ETC1 tiles hold four 4x4 blocks (column-major: top-left, bottom-left,
// top-right, bottom-right), each 64-bit block little-endian with its pixel
// index running down the columns, and, in the alpha format, preceded by 64
// bits of 4-bit alpha in the same order. Found by decoding all 8,342
// pictures of both games (.analysis/adventure56/docs/graphics.md).
//
// Formats (header byte 0x0a): 0 RGBA 8888, 1 RGBA 4444, 2 RGBA 5551,
// 3 RGB 888, 4 RGB 565, 12 eight-bit luminance, 27 ETC1, 28 ETC1 + 4-bit
// alpha. 32- and 24-bit pixels store their channels from the lowest byte as
// A, B, G, R (B, G, R).
#include "import/a56_formats.hpp"
#include "platform/image.hpp"

namespace picarat::adventure56 {

// The picture's level `level` (0 = full size) as RGBA; empty cells are
// transparent black.
platform::Image decodePicture(const import::adventure56::Picture& picture, unsigned level = 0);

// The 16-bit value of a pixel of level 0 for the 16-bit formats (fonts
// read their sheets' two 8-bit glyph planes from it).
std::uint16_t rawPixel16(const import::adventure56::Picture& picture, unsigned x, unsigned y);

} // namespace picarat::adventure56
