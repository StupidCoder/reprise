#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
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
    // By cell (z x cells + x): its polygons (a range of `polygons`) and its
    // simpler version (a range of `far_polygons`; without one, none).
    struct Cell {
        bool water{};
        std::size_t first{}, count{}, far_first{}, far_count{};
    };
    std::vector<Cell> by_cell;
};

// Which of the fine grid's cells `$11F27C` draws, and in which version.
// A window is a list of cell offsets (x, z) from the camera's cell; the
// camera's own cell is (0, 0).
//  - Above height $10000 (world units, the zoom applied) the 5 x 5 cells
//    around the camera (`$11E8A2`), above $9000 3 x 3 (`$11E852`).
//  - Lower, the camera's quarter of its cell (`$13AB40`: (3 - x quarter) +
//    4 (3 - z quarter), quarters of 2^14 units) and the heading's eighth
//    (`$13AB44`, 45 degrees each from north, clockwise) pick a list:
//    `$11EFBC` up to $3000, `$11ECBC` up to $4800, else `$11E8BC`. Up to
//    $3A0 a table (`$11F1BC`, up to $160 `$11F1FC`, up to $10 `$11F23C`)
//    names eighths that see only the camera's own cell (a negative first
//    entry: all of them) - looking that way only it is drawn.
//  - Up to $C80 a cell other than the camera's is drawn in its simpler
//    version when its centre is farther along x or z than `far_reach`
//    (by height / 256, at most 17; 2^16 32-bit units each); the camera's
//    own cell in its simpler version where `own_far` says so (`$113138`).
struct WaterWindows {
    using Window = std::vector<std::array<int, 2>>;
    Window wide, middle;                                            // 5 x 5, 3 x 3
    std::array<std::array<std::array<Window, 8>, 16>, 3> low;       // band, quarter, eighth
    std::array<std::array<std::array<int, 4>, 16>, 3> own_only{};   // $11F1BC, $11F1FC, $11F23C
    std::array<int, 18> far_reach{};                                // $11F5D8
    // $113418: per cell (x, z) rules - a height limit (at least, or at
    // most), then quarters, each with the eighths for which the camera's
    // own cell shows its simpler version.
    struct OwnFar {
        int x{}, z{};
        struct Rule {
            bool at_least{};
            std::int32_t height{};
            std::vector<std::pair<int, std::vector<int>>> quarters;
        };
        std::vector<Rule> rules;
    };
    std::vector<OwnFar> own_far;
    bool own_cell_far(int x, int z, std::int32_t height, int quarter, int eighth) const;
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
    // Per quarter (3 - x quarter) + 4 (3 - z quarter): cell offsets (x, z);
    // up to height $5000 four cells instead (`$11E812`).
    std::array<std::vector<std::array<int, 2>>, 16> windows, low_windows;
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
    WaterWindows water_windows;      // which fine cells show ($11F3FA)
    std::vector<Runway> runways;

    bool over_water(double x, double z) const;
    bool on_runway(double x, double z) const;
};

World read_world(const amiga::LoadedImage& program);

} // namespace interceptor::data
