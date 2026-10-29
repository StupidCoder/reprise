#pragma once
// The 3D layer the puzzle engines draw flat-coloured shapes into (overlay 2:
// rectangles 02070e8c, line quads 02070fcc and thick lines 020711a0), drawn
// with the rasterizer of nds3d/raster.hpp and composed over the lower
// screen's picture.
#include "graphics/graphics.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace picarat {
struct Ds3dPolygon {
    std::array<std::array<int, 2>, 4> vertices{}; // screen pixels, submission order
    int count = 4;
    std::uint16_t color = 0; // RGB555 vertex colour
    int alpha = 31;          // 1..31; 31 is opaque
    int depth = 0;           // smaller is nearer (strict less-than test)
};

class Ds3dFrame {
public:
    void add(const Ds3dPolygon& polygon) { polygons_.push_back(polygon); }

    bool empty() const { return polygons_.empty(); }

    // DISP3DCNT bit 4 (anti-aliasing) and bit 3 (alpha blending).
    void render(Surface& target, int yOffset, bool antialias, bool alphaBlend = true) const;

private:
    std::vector<Ds3dPolygon> polygons_;
};
} // namespace picarat
