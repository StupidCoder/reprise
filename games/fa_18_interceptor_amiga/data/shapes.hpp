#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <map>
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
// with the first frame of animated parts. It runs every polygon list once
// and keeps the facing tree (`DrawNode`), so a renderer can walk the tree
// from its camera and paint the lists in the original's order, as $112650
// does; each face, line and dot names the list it came from.
namespace interceptor::data {

struct ShapePoint {
    double x{}, y{}, z{}; // the shape's units: x right, y up, z forward (the nose)
};

// How a face shows from its two sides (the colour word, $113836): always
// in one colour (a negative word), one-sided (seen from behind it is not
// drawn: $112B1C's test), or two-sided with a colour for each side.
enum class Facing { always, one_sided, two_sided };

// What a drawn part belongs to: its polygon list (an index into the shape's
// lists, -1 for none) and its place in that list.
struct ListPlace {
    int list = -1;
    int order{};
};

// A face's paint other than a palette index: the filler can change only
// some of a pixel's bitplanes (`$13A9D6`: the planes, then a word per
// mode; the blitter's minterms at `$125066`): `invert_planes | m` flips
// the planes in m (the runway's threshold blocks and stripes, ops $50,
// $54, $5C, $6C: plane 1), `set_planes | m` sets them (ops $44, $48, $78:
// plane 3). The ops that do not set the planes paint in the mode the last
// one left (an object starts with plain colours, `$112640`).
constexpr int invert_planes = 0x100, set_planes = 0x200;

struct ShapeFace : ListPlace {
    std::vector<ShapePoint> corners;
    int colour{};             // palette index (flight palette) or a plane change; the front for two-sided faces
    bool two_sided{};
    Facing facing{Facing::always};
    int back_colour{};        // two-sided faces
    // Ops $9C and $A0 ($113B58, $113D1A: a box or a pyramid drawn as one
    // face across it): shown only with the camera in some quadrants about
    // the object (the signs of its x and z from the object's place the
    // same or opposite), in `z_colour` while it is farther from the object
    // along z than along x; the box's top only while the view looks
    // steeply down (`$13AA76`); all only while the model's point 1 lies in
    // the view's pyramid.
    enum class Quadrant { any, same_signs, opposite_signs };
    Quadrant quadrant{Quadrant::any};
    int z_colour = -1;
    bool steep_only{};
    std::optional<ShapePoint> anchor;
    // Drawn only with the camera at most this high (world units; op $24:
    // 128, `$114C14`).
    double up_to = -1;
};

struct ShapeLine : ListPlace {
    ShapePoint from, to;
    int colour{};
    // Drawn only with the camera higher than this (world units; ops $128
    // and $12C: above 192, `$11372C`, `$114A20`).
    double above = -1;
};

struct ShapeDot : ListPlace {
    ShapePoint at;
    int colour{};
};

// A node of the draw list's tree ($112650). A test picks one of two
// branches; a leaf names polygon lists to paint, then goes on to `next`
// unless it stops the walk.
//  - facing: three points; n = (P1 - P0) x (P2 - P0) in camera space, the
//    first branch when n . P0 < 0 (the side facing the camera, $112B1C);
//  - plane: a point and a normal (world units about the object, not
//    turned with it); the first branch when (point - camera) . normal < 0
//    ($112AE4);
//  - axis: the camera's coordinate about the object on one axis against a
//    model point's; the second branch when the camera's is smaller,
//    the other way round with bit 12 ($112B8A; x and z: hypothesis).
struct DrawNode {
    enum class Kind { facing, plane, axis, leaf };
    Kind kind{Kind::leaf};
    std::array<ShapePoint, 3> points{}; // facing: P0-P2; plane: point, normal; axis: points[0]
    int axis{};                          // 0 x, 1 y, 2 z
    bool invert{};
    int first = -1, second = -1;         // branches (node indices; -1: nothing)
    std::vector<int> lists;              // leaf: polygon lists in order
    bool stop{};                         // leaf: the walk ends after it
    int next = -1;                       // leaf: the node after it
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
    // The facing tree (node 0 the root; empty: paint the lists in order)
    // and how many polygon lists the parts name.
    std::vector<DrawNode> tree;
    int lists{};
    // Flat (flag 2: points on the object's level): drawn on the ground.
    bool flat{};
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
// How high the camera must be for a standing object, by its type record's
// routine: `$111C90` objects are left out above 14336 by the coarse passes
// (`$10FB0E`) and above 16384 by the near one (`$10FDCC`); `$111C84` ones
// show only above 10240, and the coarse passes leave their distance at 0
// (`$10FB36`: the nearest level of detail).
enum class Altitude { any, low, high };
Altitude altitude(const amiga::LoadedImage& program, int type);

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
// Points the game builds into the ground shapes' point blocks when it
// starts (game::road_points, `$1197E0`): by the address of a point's x
// word, its x and z. The program on the disk holds zeros there.
using BuiltPoints = std::map<std::uint32_t, std::array<int, 2>>;

// A ground shape's lists by distance ($112D76).
std::vector<DetailLevel> read_ground_levels(const amiga::LoadedImage& program, int type,
                                            const BuiltPoints& built = {});

} // namespace interceptor::data
