#pragma once
// A small software rasterizer for the 3DS adventures' graphics: textured,
// vertex-coloured triangles in clip space, drawn into one screen's colour
// and depth buffers. 2D layers (animations, texts, pop-ups) and 3D models
// all go through it.
//
// It is not a PICA200 emulation: it implements what the games' draw calls
// ask for (perspective-correct texture coordinates and colours, bilinear or
// nearest texture filtering with repeat / clamp / mirror, depth test and
// write, source-alpha blending, additive blending, alpha test, face
// culling), with OpenGL's conventions: normalised device coordinates in
// [-1, 1], y up, depth from -1 (near) to 1 (far), counter-clockwise front
// faces, pixel centres at half-integers and the top-left fill rule.
#include "platform/image.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace picarat::ctr3d {

struct Texture {
    int width = 0, height = 0;
    std::vector<platform::Color> pixels;
    // Clamp to the edge, repeat, mirrored repeat, or clamp to a transparent
    // border (the GPU's four modes).
    enum class Wrap : std::uint8_t { Clamp, Repeat, Mirror, Border };
    Wrap wrapS = Wrap::Clamp, wrapT = Wrap::Clamp;
    bool linear = true;

    // The colour at normalised coordinates (u, v), v = 0 the top row.
    void sample(float u, float v, float out[4]) const;
};

// One screen's buffers.
struct Target {
    int width = 0, height = 0;
    std::vector<platform::Color> colour;
    std::vector<float> depth;

    Target() = default;

    Target(int w, int h) { resize(w, h); }

    void resize(int w, int h);
    void clear(platform::Color c, float d = 1.0f);
    platform::Image image() const;
};

// A vertex in clip space with its colour (0..1) and texture coordinates.
struct Vertex {
    float x = 0, y = 0, z = 0, w = 1;
    float r = 1, g = 1, b = 1, a = 1;
    float u = 0, v = 0;
    float q = 1;      // the texture coordinates' divisor (projective s, t, q)
    float shade = 0;  // N.H for the fragment lighting's D0 table (interpolated)
    float shade2 = 0; // max(0, N.L) for its diffuse term (interpolated)
};

struct DrawState {
    const Texture* texture = nullptr; // null: vertex colour only
    // Add: source x alpha + destination; AddOne: source + destination (the
    // blend factors one, one); Subtract: destination - source x alpha (the
    // PICA's reverse subtract, equation 2).
    enum class Blend : std::uint8_t {
        None,
        Alpha,
        Add,
        Multiply,
        AddOne,
        Subtract
    } blend = Blend::Alpha;
    enum class Cull : std::uint8_t { None, Back, Front } cull = Cull::None;
    bool depthTest = false;
    bool depthWrite = false;
    float alphaTest = 0.0f;   // fragments with alpha <= this are dropped (0: only alpha 0)
    float depthOffset = 0.0f; // subtracted from every fragment's depth (toward the eye)
    // Multiplied into every fragment (object colour and fade).
    float colour[4] = {1, 1, 1, 1};
    // Fragment lighting as the 3DS GPU combines it for the models (texture
    // combiner stage 0 (fragment primary + fragment secondary) x texture,
    // clamped; stage 1 x the vertex colour): primary = litPrimary +
    // litDiffuse x shade2, secondary = litSpecular x D0, D0 = d0Table at
    // the interpolated shade (the lighting lookup format: 256 values, 256
    // differences; entries 0..127 for 0..1, 128..255 for -1..0) or 1
    // without a table.
    bool fragmentLighting = false;
    const float* d0Table = nullptr;
    float litPrimary[3] = {0, 0, 0}, litDiffuse[3] = {0, 0, 0}, litSpecular[3] = {0, 0, 0};
    // Pixels outside the scissor rectangle are left alone ([x0, x1) x [y0, y1)).
    int scissor[4] = {0, 0, 1 << 30, 1 << 30};
};

// Draws a triangle list (three indices per triangle).
void drawTriangles(Target& target, const DrawState& state, std::span<const Vertex> vertices,
                   std::span<const std::uint16_t> indices);

} // namespace picarat::ctr3d
