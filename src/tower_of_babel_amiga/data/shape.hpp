#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include <cstdint>
#include <vector>

// The 3D shapes objects are drawn with (read by $13934). A shape lies in the
// main data; objects point at one, and animations switch between shapes.
//
//   +0     byte: length of the shadow section that follows
//   shadow section: count, then per polygon: point count, points
//   parts: per part: byte offset from the part's body to the next part
//          (0 = last), the part's centre (for depth sorting), then the body:
//          face count, and per face: point count, style, normal, points
//
// Coordinates are signed bytes, doubled, relative to the object's position.
// A face's style picks its pen and fill: bit 7 with bit 6 the object's own
// pen; bit 7 alone a see-through pen; bit 6 alone an outlined fill; bit 5
// keeps the pen in unlit cells (else unlit cells use the pen before it);
// bits 0-4 the pen.
namespace babel::data {

struct Vec3 {
    int x{}, y{}, z{};
};

struct ShapeFace {
    int style{};
    Vec3 normal;
    std::vector<Vec3> points;
};

struct ShapePart {
    Vec3 centre;
    std::vector<ShapeFace> faces;
};

struct Shape {
    std::vector<std::vector<Vec3>> shadows;
    std::vector<ShapePart> parts;
};

// Decodes the shape at `address` (an address in the original's memory that
// lies in `main`). Throws std::runtime_error when it runs out of the data.
Shape decode_shape(const Memory& main, std::uint32_t address);

} // namespace babel::data
