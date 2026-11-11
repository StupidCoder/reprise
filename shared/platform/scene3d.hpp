#pragma once
#include "platform/image.hpp"
#include <array>
#include <vector>
#include <string>

namespace platform::scene3d {
// Portable render data, independent of game state and any graphics API.
// Coordinates: +Y up. Camera basis explicitly supplies forward/right/up.
struct Vec3 { double x{}, y{}, z{}; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, double b) { return {a.x*b,a.y*b,a.z*b}; }
inline double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
struct Vertex { Vec3 position; double u{}, v{}; };
struct Triangle { std::array<unsigned,3> vertices; unsigned material{}; };
struct Material { Color colour; std::string texture; };
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;
};
struct View {
    Vec3 eye, right{1,0,0}, up{0,1,0}, forward{0,0,1};
    // Tangents at the near plane, permit asymmetric (off-axis) stereo.
    double left=-1, right_edge=1, bottom=-1, top=1, near_z=.01, far_z=1000;
};
struct PixelPoint { double x{}, y{}, z{}; };
PixelPoint project(Vec3 point, const View&, int width, int height);
// Slow deterministic reference/diagnostic renderer. Two-sided geometry, depth
// test and near/far clipping; not the proposed real-time headset backend.
Image reference_image(const Mesh&, const View&, int width, int height, Color background);
} // namespace platform::scene3d
