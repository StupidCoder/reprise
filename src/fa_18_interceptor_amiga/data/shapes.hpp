#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The 3D shapes of F/A-18 Interceptor (formats.md "3D shapes"), read from the
// program. Each object type ($115F4C) names a draw routine, a shape base and
// a detail-level list; a level picks a point block (the model) and a draw
// list: a tree of facing tests whose leaves are polygon lists. Polygon lists
// hold opcodes ($113470): faces, lines, discs, points, shadows, vertex
// builders and parts shown only in some states or at some distances.
//
// This decoder takes what the lists draw at distance 0, the nearest view,
// with every branch of the facing tree and the first frame of animated
// parts: the full model a viewer (or a renderer with the original's
// simplification of distant objects switched off) shows. The game's own
// drawing order and distance rules are not modelled here yet.
namespace interceptor::data {

struct ShapePoint {
    double x{}, y{}, z{}; // the shape's units: x right, y up, z forward (the nose)
};

struct ShapeFace {
    std::vector<ShapePoint> corners;
    int colour{};             // palette index (flight palette)
    bool two_sided{};
};

struct ShapeLine {
    ShapePoint from, to;
    int colour{};
};

struct Shape {
    int type{};                // index in the object type table
    std::uint32_t model{};     // address of the point block
    int detail_levels{};
    std::vector<ShapeFace> faces;
    std::vector<ShapeLine> lines;
    std::vector<std::string> problems; // opcodes the decoder stopped at
};

// The object types with shapes in the vehicle formats (0-15; types that
// share a shape with an earlier one are left out).
std::vector<Shape> read_vehicle_shapes(const amiga::LoadedImage& program);

// One type's nearest detail level.
Shape read_shape(const amiga::LoadedImage& program, int type);

} // namespace interceptor::data
