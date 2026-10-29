#pragma once
// A software rasterizer for the DS 3D look: convex polygons drawn into a
// 256x192 frame with a depth buffer, translucency, texture lookups,
// edge marking (outlines) and edge anti-aliasing.
//
// Written for this project from general rasterization techniques and the
// DS rendering engine's documented behaviour (GBATEK level): opaque before
// translucent polygons, a "less than" depth test (optionally "equal"),
// polygon IDs that stop a translucent mesh from blending over itself,
// outlines in the edge colour of the polygon-ID group, and anti-aliased
// polygon edges. It does not try to reproduce the hardware pixel for pixel.
//
// How a polygon is drawn: for every pixel row the polygon reaches, its
// edges are intersected with the row; the pixels from the leftmost to the
// rightmost intersection are covered, both ends included (as on the DS,
// so a shape from x = 10 to x = 15 is six pixels wide). Attributes are interpolated in a
// perspective-correct way by interpolating attribute/w and 1/w linearly on
// screen.
#include <array>
#include <cstdint>
#include <vector>

namespace picarat::nds3d {

// A vertex on screen.
struct RasterVertex {
    float x = 0, y = 0;        // screen position in pixels (0..256, 0..192)
    std::uint32_t depth = 0;   // depth-buffer value (24-bit z or w)
    float w = 1;               // clip-space w, for perspective correction (> 0)
    float r = 0, g = 0, b = 0; // vertex colour, 0..31
    float s = 0, t = 0;        // texture coordinates in texels
};

// A texture lookup: colour 0..63 per component, alpha 0..31.
struct Texel {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

struct TextureSampler {
    virtual ~TextureSampler() = default;
    virtual Texel sample(int s, int t) const = 0;
};

// How the texture and vertex colour combine (polygon attribute bits 4-5).
enum class Shading { Modulate, Decal, Toon, Highlight };

struct RasterPolygon {
    std::vector<RasterVertex> vertices; // convex, in drawing order
    int alpha = 31;                     // polygon alpha 0..31 (0: wireframe)
    bool translucent = false;           // drawn after the opaque polygons
    bool wBuffer = false;               // depth is w (perspective) instead of z
    int id = 0;                         // polygon ID 0..63
    bool depthEqual = false;            // depth test "equal" instead of "less"
    bool updateDepthWhenTranslucent = false;
    Shading shading = Shading::Modulate;
    const TextureSampler* texture = nullptr; // null: untextured
};

// Rendering-engine settings for one frame.
struct RasterSettings {
    bool alphaBlend = true;
    bool antialias = false;
    bool edgeMarking = false;
    bool alphaTest = false;
    int alphaTestReference = 0;
    bool manualTranslucentSort = false;
    std::array<std::uint16_t, 8> edgeColors{}; // RGB555 per polygon-ID group (id / 8)
    std::array<std::uint16_t, 32> toonTable{}; // RGB555, indexed by vertex red
    // Clear plane.
    std::uint16_t clearColor = 0; // RGB555
    int clearAlpha = 0;
    int clearId = 0;
    std::uint32_t clearDepth = 0xffffff;
};

// One finished pixel: colour 0..63 per component, alpha 0..31.
struct RasterPixel {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

// Draws a frame's polygons and returns its 256x192 pixels, row by row.
std::vector<RasterPixel> rasterize(const std::vector<RasterPolygon>& polygons,
                                   const RasterSettings& settings);

// 5-bit colour component to the 6-bit form the rendering engine uses.
inline int widen(int c) {
    return c ? c * 2 + 1 : 0;
}

} // namespace picarat::nds3d
