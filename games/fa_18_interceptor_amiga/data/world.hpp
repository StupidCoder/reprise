#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <optional>
#include <vector>

// The world of F/A-18 Interceptor (game.md "World"), read from the
// program: three maps of objects and a grid of water polygons. Positions
// are in the scene builder's units (an object position long / 256): a
// coarse map cell is 65536, a fine one 16384, x east and z along the
// map's rows (the builder's $1102C0 uses rows for z). Checked against the
// original's visible-object lists (harness/listcheck.py).
namespace interceptor::data {

struct MapObject {
    int type{};        // index in the object type table ($115F4C)
    double x{}, z{};
};

struct ObjectMap {
    int cells{};              // 32 coarse (maps A, B) or 128 fine (map C) per side
    double cell_size{};       // in world units
    std::vector<MapObject> objects;
};

// The water ($11F27C): polygons in colour 6 over a grid of cells, each
// with an optional simpler version used for cells far from the camera.
// A threshold makes the rest of the cell's polygons appear only from that
// height up (`min_height`, in the drawer's height units: threshold x 4).
struct WaterPolygon {
    std::optional<int> min_height;
    std::vector<std::array<double, 2>> points; // x, z in world units
};

struct WaterGrid {
    int cells{};            // per side
    double cell_size{};     // in world units
    std::vector<WaterPolygon> polygons;     // near versions, all cells
    std::vector<WaterPolygon> far_polygons; // simpler versions where present
    // The cell (x, z) of each of `polygons`.
    std::vector<std::array<int, 2>> cell_of;
};

// The coarse water grid ($137F9C): 8 x 8 cells of 2^20 world units, the
// world's corner at cell (4, 4), polygons in 1/4096 of a cell (they may
// reach into the next cell). Drawn under the fine grid's polygons whenever
// the camera is above height $3F8 ($11F27C), the cells of a window around
// the camera's cell ($11E712: by the camera's quarter of its cell).
struct CoarseWater {
    static constexpr int cells = 8, origin = 4;
    static constexpr double cell_size = 1048576;
    std::vector<WaterPolygon> polygons;
    std::vector<std::array<int, 2>> cell_of;
    // Per quarter (3 - x quarter) + 4 (3 - z quarter): cell offsets (x, z).
    std::array<std::vector<std::array<int, 2>>, 16> windows;
};



// A hard surface ($137D8A): touching down inside one of its quads is on a
// runway ($11F83A); elsewhere on land the wheels roll on grass, on water
// the aircraft is lost.
struct Runway {
    int type{};      // the runway's object type
    double x{}, z{}; // world units
    std::vector<std::array<std::array<double, 2>, 4>> quads; // x, z around (x, z)
};

struct World {
    ObjectMap pass_a, pass_b, near;  // $137584, $137684, $137784
    WaterGrid water;                 // the fine grid, $138160
    CoarseWater coarse_water;        // $137F9C
    // The fine grid's window around the camera's cell by height ($11F3FA):
    // above $10000 (32-bit units) 5 x 5 cells ($11E8A2), above $9000 3 x 3
    // ($11E852); lower it follows the heading (not rebuilt: all cells).
    std::vector<std::array<int, 2>> window_5x5, window_3x3;
    std::vector<Runway> runways;

    bool over_water(double x, double z) const;
    bool on_runway(double x, double z) const;
};

World read_world(const amiga::LoadedImage& program);

} // namespace interceptor::data
