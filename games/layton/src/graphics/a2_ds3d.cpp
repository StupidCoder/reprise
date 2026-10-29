// The flat 3D layer of the puzzles. See a2_ds3d.hpp.
#include "graphics/a2_ds3d.hpp"
#include "graphics/nds3d/raster.hpp"

namespace picarat {

void Ds3dFrame::render(Surface& target, int yOffset, bool antialias, bool alphaBlend) const {
    using namespace nds3d;
    // Every polygon becomes an untextured polygon of one colour at a fixed
    // depth; all share polygon ID 0, so overlapping translucent shapes do
    // not darken each other (as on the DS).
    std::vector<RasterPolygon> polygons;
    for (const auto& p : polygons_) {
        RasterPolygon out;
        out.alpha = p.alpha;
        out.translucent = p.alpha < 31;
        for (int i = 0; i < p.count; ++i) {
            RasterVertex v;
            v.x = float(p.vertices[std::size_t(i)][0]);
            v.y = float(p.vertices[std::size_t(i)][1]);
            v.depth = std::uint32_t(p.depth);
            v.r = float(p.color & 31);
            v.g = float((p.color >> 5) & 31);
            v.b = float((p.color >> 10) & 31);
            out.vertices.push_back(v);
        }
        polygons.push_back(std::move(out));
    }
    RasterSettings settings;
    settings.antialias = antialias;
    settings.alphaBlend = alphaBlend;
    settings.clearDepth = 0xffffffffu; // puzzles use depths up to 0x7fff0000
    const auto pixels = rasterize(polygons, settings);

    // Compose the 3D layer over the 2D picture below it, using its alpha.
    for (int y = 0; y < 192; ++y) {
        for (int x = 0; x < 256; ++x) {
            const auto& p = pixels[std::size_t(y * 256 + x)];
            if (p.a == 0) {
                continue;
            }
            const std::uint16_t color =
                std::uint16_t((p.r >> 1) | ((p.g >> 1) << 5) | ((p.b >> 1) << 10));
            auto& out = target.pixels[std::size_t((y + yOffset) * 256 + x)];
            out = blend3D(color, p.a, out);
        }
    }
}

} // namespace picarat
