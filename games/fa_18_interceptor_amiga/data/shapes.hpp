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
    // The ground shadow ($10, $11392E): polygons of the shape's points laid
    // flat on the ground in colour 0 while the object is below the height.
    std::vector<std::vector<ShapePoint>> shadows;
    double shadow_height{};
    std::vector<std::string> problems; // opcodes the decoder stopped at
    // Op $118 ($1135F0): the lists after it are chosen by a smoke slot's
    // timer (types 33-35, read_smoke_shape); the address after the op.
    std::uint32_t timer_variants{};
};

// A disc ($F8, and $11C for several: $11582A): drawn at a point with a
// radius in pixels of `radius` over the point's distance (see SmokeShape),
// at most 127.
struct ShapeDisc {
    ShapePoint at;
    int colour{};
    int radius{};
};

// The smoke a missile leaves on the ground (types 34 and 35, mirror
// images) or something leaves in the air (33): object types 33-35 at
// `$115F4C`, drawn from the smoke slots (`$13D474`) by `$111D5C`. Near
// (closer than `near_limit`): 34 and 35 a scorched patch with cracks, all
// three a column of discs that rises over the slot's first 11 frames,
// stands, changes its puffs every two frames and goes from the bottom up
// over its last 12 (op $118, `$1135F0`, by the slot's timer, $7F down);
// farther, 34 and 35 a two-pixel dot (`$E8`, `$1238A8`) up to `far_limit`.
// A disc's distance is its point's camera-space length (the camera matrix
// `$13AEC8`, world units) times 2^`distance_shift`, the model's low scale
// nibble ($11582A shifts the point back by the shape's scale shift).
struct SmokeShape {
    int type{};
    double near_limit{}, far_limit{};               // world units
    double scale{};                                 // world units per shape unit
    int distance_shift{};
    std::optional<Shape> patch;                     // 34, 35: the patch and its cracks
    std::array<std::vector<ShapeDisc>, 128> column; // by timer (negative: nothing)
    std::optional<ShapeDot> far_dot;
};
SmokeShape read_smoke_shape(const amiga::LoadedImage& program, int type);

// The explosion an exploding record shows over its last 13 frames
// (`$12185E`, drawn instead of its model while its timer is 12 or less):
// pieces thrown out along directions by the explosion's age (15 - timer),
// each a small triangle turning through eight shapes, or, for the cruise
// missile, a disc. Kinds: 1 an aircraft over land (orange), 2 over water
// (white), 3-5 the cruise missile (grey discs, then two orange bursts a
// frame and two frames younger).
struct ExplosionBurst {
    std::vector<std::array<int, 3>> directions; // per piece (bytes)
    std::vector<int> colours;                   // per piece
};
struct Explosions {
    std::array<ExplosionBurst, 5> bursts;                    // kinds 1-5
    std::array<std::array<std::array<int, 3>, 3>, 8> pieces; // the triangles' corners ($12169A)
    std::array<int, 32> piece_of{};                          // piece number + age -> triangle ($12181A / 4)
};
Explosions read_explosions(const amiga::LoadedImage& program);

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

// A ground shape is drawn only while the camera is lower than this (world
// units): its routine returns at once above $380000 ($112D5C) or $800000
// ($112D6A) in 32-bit units.
double ground_ceiling(const amiga::LoadedImage& program, int type);

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
// A ground shape's lists by distance ($112D76).
std::vector<DetailLevel> read_ground_levels(const amiga::LoadedImage& program, int type);

} // namespace interceptor::data
