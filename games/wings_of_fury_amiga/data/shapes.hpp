#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Shape libraries ("PPkc", the *.shp files, usually inside "Rpck").
//
// Library: "PPkc", u16 count n, n four-character names, n u32 offsets of the
// shapes counted from the end of the offset table (6 + 8n). The game finds a
// shape by name ($110560) and keeps pointers to the ones it needs, in the
// order of its own name lists (`wings` data around $113B60).
//
// Shape (read by the blitter routine $110B0C and its mask maker $110CE2):
//   +0  u16  bytes per row (a whole number of 16-pixel words)
//   +2  u16  rows
//   +4  s16  offset x  } the shape's handle: callers draw it at position
//   +6  s16  offset y  } minus these (scenery drawer $1038A0)
//   +8  u16  x  } where the shape was cut from the artist's sheet
//   +10 u16  y  } (hypothesis: not read by the game)
//   +12 u8   planes cleared under the mask
//   +13 u8   planes set under the mask
//   +14 u8[] stored planes, each a mask of the bitplanes it is copied into,
//            ended by 0 (at most 6; the header is 20 bytes either way)
//   +20      the stored planes, bytes per row x rows each
// The blitter first clears and sets the planes of bytes 12 and 13 where the
// mask is set, then copies each stored plane into all of its bitplanes there.
// The mask is the OR of the stored planes (the plane itself when there is only
// one); a shape without stored planes is drawn as a solid rectangle.
namespace wings::data {

struct Shape {
    struct Plane {
        std::uint8_t bitplanes{}; // bit p: copied into bitplane p
        std::vector<std::uint8_t> bits;
    };

    std::string name;
    int bytes_per_row{};
    int height{};
    int offset_x{}, offset_y{};
    int sheet_x{}, sheet_y{};
    std::uint8_t cleared_planes{};
    std::uint8_t set_planes{};
    std::vector<Plane> planes;

    int width() const { return bytes_per_row * 8; }
    // Whether the blitter writes this pixel.
    bool covers(int x, int y) const;
    // The colour index the shape leaves at a pixel it covers, drawn over
    // `under` (bitplanes the shape does not touch keep their value).
    int colour(int x, int y, int under = 0) const;
};

struct ShapeLibrary {
    std::vector<Shape> shapes;

    // The shape with this name, or nullptr.
    const Shape* find(std::string_view name) const;
};

// Decodes a library from the file's contents (unpacked when it is "Rpck").
// Throws std::runtime_error when it is malformed.
ShapeLibrary read_shape_library(std::span<const std::uint8_t> file);

} // namespace wings::data
