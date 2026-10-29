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
    std::vector<Runway> runways;

    bool over_water(double x, double z) const;
    bool on_runway(double x, double z) const;
};

World read_world(const amiga::LoadedImage& program);

} // namespace interceptor::data
