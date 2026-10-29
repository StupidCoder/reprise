#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

// The 3D shapes of F/A-18 Interceptor (game.md "3D shapes"), read from the
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

// How a face shows from its two sides (the colour word, $113836): always
// in one colour (a negative word), one-sided (seen from behind it is not
// drawn: $112B1C's test), or two-sided with a colour for each side.
enum class Facing { always, one_sided, two_sided };

struct ShapeFace {
    std::vector<ShapePoint> corners;
    int colour{};             // palette index (flight palette); the front for two-sided faces
    bool two_sided{};
    Facing facing{Facing::always};
    int back_colour{};        // two-sided faces
};

struct ShapeLine {
    ShapePoint from, to;
    int colour{};
};

struct ShapeDot {
    ShapePoint at;
    int colour{};
};

struct Shape {
    int type{};                // index in the object type table
    std::uint32_t model{};     // address of the point block
    int detail_levels{};
    // Bit 13 of the model word ($111DD2): at normal zoom such a model is
    // drawn only by the coarse maps' passes, one without it only by the fine
    // map's (near) pass.
    bool coarse_pass{};
    // World units per shape unit: points are shifted right by the low
    // nibble of model byte +6 at close range ($111E68, $111EAA; at other
    // ranges both points and positions shift so the ratio stays).
    double scale{1};
    std::vector<ShapeFace> faces;
    std::vector<ShapeLine> lines;
    std::vector<ShapeDot> dots;          // single pixels ($04)
    std::vector<std::string> problems; // opcodes the decoder stopped at
};

// The object types with shapes in the vehicle formats (0-15; types that
// share a shape with an earlier one are left out).
std::vector<Shape> read_vehicle_shapes(const amiga::LoadedImage& program);

// The three shape families, by the type's draw routine: vehicles ($111C94,
// $1169C4, $116A1E, $116ABE), scenery ($111C90, $111D5C, $111C84: the same
// lists with flat and generated points and more opcodes) and flat ground
// shapes ($112D5C, $112D6A).
enum class ShapeFamily { none, vehicle, scenery, ground };
ShapeFamily shape_family(const amiga::LoadedImage& program, int type);

// Every type of a family that draws something (shared shapes once).
std::vector<Shape> read_shapes(const amiga::LoadedImage& program, ShapeFamily family);

// A ground shape (flat, on the ground).
Shape read_ground_shape(const amiga::LoadedImage& program, int type);

// One type's nearest detail level.
Shape read_shape(const amiga::LoadedImage& program, int type);

// The levels of detail of a type (the vehicle and scenery formats): each
// is used while the object is nearer than `nearer_than` (world units, at
// the normal zoom; the list's distances and thresholds count 16 units), the list walked as $111D9C does; past the last limit
// the object is not drawn. A level without a shape draws nothing.
struct DetailLevel {
    double nearer_than{};
    std::optional<Shape> shape;
};
std::vector<DetailLevel> read_detail_levels(const amiga::LoadedImage& program, int type);

} // namespace interceptor::data
