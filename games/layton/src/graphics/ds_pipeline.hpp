#pragma once
// The DS 3D engine's geometry and polygon rasterization, pixel for pixel,
// for scenes that draw 2D sprites through it (the first adventure's turned
// and scaled sprites and flat triangles).
//
// Geometry: 4.12 fixed-point matrices multiplied the way the geometry
// engine does it (each product summed in 64 bits, then shifted right by
// 12), the clip matrix = position matrix x projection, vertices transformed
// by it and mapped onto the 256 x 192 viewport with the hardware's integer
// division (halving both terms when w does not fit 16 bits).
//
// Rasterization: the polygon's left and right edges are walked from the top
// vertex with 18-bit fractional slopes; a mostly horizontal ("x-major") edge
// covers a run of pixels per row. With anti-aliasing on every edge pixel is
// drawn and carries a coverage (0..31): along an x-major edge it grows over
// the run, for a steep edge it comes from the slope's fraction; vertical and
// horizontal edges are fully covered. Texture coordinates (12.4) are
// interpolated linearly along the edges and then along the row (the linear
// mode the hardware uses when w is equal, as in orthographic scenes).
//
// Sprites: both adventures set up the same camera for the lower screen (an
// orthographic projection of 256 x 192 units, the eye one unit in front)
// and draw a turned sprite chunk by chunk as a unit square, moved to the
// sprite's position, turned, moved to the chunk's place in the frame and
// scaled to the chunk's texture (first adventure 02005e78, second ov2
// 0206ecfc).
//
// Written for this project from the hardware's documented behaviour, checked
// against the reference emulator's output. (graphics/a2_ds3d.* and nds3d/ are
// the approximate rasterizers of the second adventure's shapes and models.)
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace picarat::dspipe {

// A 4x4 matrix in 4.12 fixed point, row-major as the hardware loads it (row
// vectors: v' = v x M).
using Matrix = std::array<std::int32_t, 16>;

Matrix identity();
// The hardware's matrix commands, M = op x M.
void translate(Matrix& m, std::int32_t x, std::int32_t y, std::int32_t z); // MTX_TRANS
void scale(Matrix& m, std::int32_t x, std::int32_t y, std::int32_t z);     // MTX_SCALE
void multiply3x3(Matrix& m, const std::array<std::int32_t, 9>& n);         // MTX_MULT_3x3
// a x b (the clip matrix is position x projection).
Matrix multiply(const Matrix& a, const Matrix& b);
// An orthographic projection with w scaled, as the SDK routine computes it
// (reciprocals from the 64/32-bit hardware divider, rounded to 4.12).
Matrix orthoW(std::int32_t top, std::int32_t bottom, std::int32_t left, std::int32_t right,
              std::int32_t near, std::int32_t far, std::int32_t scaleW);
// A rotation about z for MTX_MULT_3x3 (sine and cosine in 4.12).
std::array<std::int32_t, 9> rotationZ(std::int32_t sine, std::int32_t cosine);

// A vertex on the screen: integer position and texture coordinates (12.4).
struct ScreenVertex {
    int x = 0, y = 0;
    int s = 0, t = 0;
};

// A vertex in clip space with its texture coordinates.
struct ClipVertex {
    std::int32_t x = 0, y = 0, z = 0, w = 0;
    int s = 0, t = 0;
};

// Transforms a vertex (4.12, w = 1) by a clip matrix.
ClipVertex transform(const Matrix& clip, std::int32_t x, std::int32_t y, std::int32_t z);
// Clips a polygon to the view volume (|x|, |y|, |z| <= w; the z planes, then
// y, then x), placing new vertices where its edges leave it with
// interpolated texture coordinates. Returns the new vertex count (0: the
// polygon is dropped: beyond the far plane, or wholly outside).
int clip(std::array<ClipVertex, 10>& polygon, int count);
// Maps a clip-space vertex onto the 256 x 192 viewport.
ScreenVertex viewport(const ClipVertex& v);

// One pixel of a polygon: texture coordinates and the anti-aliasing coverage
// of an edge pixel (31 = fully covered).
struct Fragment {
    int x = 0, y = 0;
    int s = 0, t = 0;
    int coverage = 31;
};

// Rasterizes a convex polygon (3 or 4 vertices in drawing order) as the
// hardware does with anti-aliasing on (every edge pixel drawn).
void rasterize(std::span<const ScreenVertex> polygon,
               const std::function<void(const Fragment&)>& put);

// The sprite camera (first adventure 020040d0, second ov2 0206d8a4):
// orthoW(0, 192, 0, 256, -1024, 1024, w 1024) and a view one unit back.
struct SpriteCamera {
    Matrix view{}, projection{};
    // Sine, cosine pairs (4.12) per 1/4096 turn: both programs' tables
    // (020c9798, 0203e4f4) hold round(4096 sin) and round(4096 cos).
    std::vector<std::int16_t> sine;
};

SpriteCamera spriteCamera();

// Draws a polygon (3 or 4 corners: x, y in 4.12, s, t in 12.4) under the
// position matrix m: transformed, clipped, mapped and rasterized.
void drawPolygon(const SpriteCamera& camera, const Matrix& m,
                 std::span<const std::array<int, 4>> corners,
                 const std::function<void(const Fragment&)>& put);

// One chunk of a sprite turned by `turns` (1/65536 turn, anticlockwise on
// screen as the angle grows) about (x, y): a frame of frameWidth x
// frameHeight pixels whose chunk texture (width x height) lies at (qx, qy).
// put receives each fragment with its texel (u, v) inside the chunk.
void drawTurnedChunk(const SpriteCamera& camera, int x, int y, unsigned turns, int frameWidth,
                     int frameHeight, int qx, int qy, int width, int height,
                     const std::function<void(const Fragment&, int u, int v)>& put);

} // namespace picarat::dspipe
