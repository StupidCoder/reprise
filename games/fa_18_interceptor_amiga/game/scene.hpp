#pragma once
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/shapes.hpp"
#include "fa_18_interceptor_amiga/data/world.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
#include <vector>

// The outside view: sky, land, the bay's water and the scenery of the three
// object maps with their shapes, drawn from a camera with the original's
// projection ($123894: x = centre - 160 x / z, y = 90 - 90 y / z, camera
// space = the body matrix with its rows scaled by $A8, $FC and the zoom
// $80, over 256). Drawn from game state at any moment, so it can show
// positions between ticks, and as wide as asked (widescreen shows more of
// the world left and right at the same scale).
namespace interceptor::game {

// A picture in palette indices (the flight palette), as the Amiga's
// bitplanes hold it.
struct IndexedImage {
    int width{}, height{};
    std::vector<std::uint8_t> pixels;
    IndexedImage(int w, int h) : width(w), height(h), pixels(std::size_t(w * h), 0) {}
    void put(int x, int y, int colour) {
        if (x >= 0 && y >= 0 && x < width && y < height) pixels[std::size_t(y * width + x)] = std::uint8_t(colour);
    }
};

struct Camera {
    std::array<double, 3> position{}; // world units
    std::array<double, 9> matrix{};   // rows x, y, z of camera space (scaled)
    bool from_above = false;          // the view from above ($13AA9D)
    // Where the water is measured from ($11F35C): from the cockpit the
    // viewed aircraft's own place (its record, not the pilot's eye),
    // otherwise the camera's.
    std::optional<std::array<double, 3>> water_from;
};

// A moving object to draw (the player's aircraft from outside): its type
// (the shape table's index), position and body matrix ($4000 = 1).
struct Vehicle {
    int type{};
    std::array<double, 3> position{};
    std::array<std::int16_t, 9> body{};
    bool shadow = true;
    // Blowing up, with its timer from 15 down to 0: its explosion (15 -
    // timer frames old, kind 1 land, 2 water, 3 the cruise missile;
    // `$12185E`, data::Explosions) after its shape (`$11278C`), and from
    // timer 12 on instead of it (`$111FBC`).
    int explosion_age = -1;
    int explosion_kind = 1;
    bool model = true;
    // The smoke trail behind it when it is the last aircraft hit
    // (`$11587C`): its places (world units) from the oldest drawn to the
    // newest with their ring slots, its length (+$3D), whether it spins,
    // and its move.
    struct Trail {
        std::vector<std::array<double, 3>> places;
        std::vector<int> slots;
        int length{};
        bool spinning{};
        std::array<double, 3> move{};
    };
    std::optional<Trail> trail;
};

// A smoke slot to draw (object types 33-35, data::SmokeShape) at its place
// (world units) with its timer.
struct SmokeColumn {
    int type{};
    int timer{};
    std::array<double, 3> position{};
};

// A projectile to draw over the scene ($1238AC: one pixel, colour 9 for
// rounds), or the sparks where it hit and of chaff ($121570: three short
// lines, +-3 world units about it, `$12163A`).
struct Spark {
    std::array<double, 3> position{};
    int colour = 9;
    bool burst = false;
};

// The ground's specks ($11C1B4): a pattern of marks repeated every $800
// units over the ground near a low camera, one of three by height
// (`$11C908`, `$11CB4C`, `$11CD30`: a count, a distance limit, then x, z
// and kind per mark: 0-2 a dot, two pixels for 2; -12, -16, -20 a
// triangle), kept while the 32 x 32 table `$11C508` (by |x| and |z| in
// 256ths) gives at most the limit. Read from the program as it starts.
struct GroundSpecks {
    struct Mark {
        int x{}, z{}, kind{};
    };
    struct Pattern {
        int limit{};
        std::vector<Mark> marks;
    };
    std::array<Pattern, 3> patterns;
    std::array<std::uint8_t, 1024> reach{};
    // The triangles' corners (x, z) for kinds -12, -16, -20 (`$11CEB4`,
    // `$11CECC`, `$11CEE4`), and for the nearest pattern, which works
    // eight times larger (`$11CEC0`, `$11CED8`, `$11CEF0`).
    std::array<std::array<std::array<int, 6>, 3>, 2> triangles{};
};
GroundSpecks read_ground_specks(const amiga::LoadedImage& program);

// The scene builder's way round the camera ($11005C): for each of the
// three object maps an ordered list of cells about the camera's cell (the
// coarse maps' cells for the two coarse passes, the fine map's for the
// near one) and of slots in each cell, picked by the camera's quarter of
// its cell (`$13AB40` coarse, `$13AB41` near), its heading's eighth
// (`$13AB44`) and a steep look down (`$13AA76`); the lists themselves are
// read from the program each frame (`$1367A4`, `$136542` looking down,
// `$137386` for the first coarse pass with the camera below 160).
struct VisitTables {
    std::array<int, 16> variant{};                 // $136464: by quarter
    std::array<std::array<int, 8>, 4> by_eighth{};  // $136474: list by eighth
    std::array<int, 16> list{};                    // $136494: by quarter
    std::array<std::array<int, 16>, 4> slot{};     // $1364A4: slot codes
    std::array<std::array<int, 21>, 4> cell{};     // $1364E4: cell codes
    std::array<std::array<int, 2>, 21> neighbour{}; // $1106B4: cells about the camera's
    // Where each neighbour's objects are placed, in quarter world units
    // (`$1106DE` coarse, `$110772` near). The coarse table's +2 cells
    // overflow the word (+32768 reads -32768): the original places the
    // objects of the cells two ahead two cells behind.
    std::array<std::array<std::array<int, 2>, 21>, 2> offset{};
    std::array<int, 8> cap{}; // $11106A: entries kept by height, looking down
};
VisitTables read_visit_tables(const amiga::LoadedImage& program);

// The road strips' corners the game builds into the ground shapes' point
// blocks as it starts ($1197E0).
data::BuiltPoints road_points(const amiga::LoadedImage& program);

class Scene {
public:
    Scene(const data::Assets& assets, const data::World& world);

    // Draws into rows [0, bottom] of `image`, the horizontal centre at
    // column `centre` (159 in the original's 320 columns).
    void draw(IndexedImage& image, int centre, int bottom, const Camera& camera,
              const std::vector<Vehicle>& vehicles = {}, const std::vector<Spark>& sparks = {},
              const std::vector<SmokeColumn>& smoke = {}) const;

    // High detail (an enhancement, off in the original): every object takes
    // its nearest level of detail at any distance (still the level its pass
    // draws), and the ground shapes show from any height.
    void set_high_detail(bool on) { high_detail_ = on; }

    // A vehicle type is drawn at this distance (one of its detail levels
    // reaches it).
    bool in_range(int type, double distance) const;

    // Where a world point shows (column, row), or nothing behind the camera.
    static std::optional<std::array<int, 2>> project(const Camera& camera, const std::array<double, 3>& point,
                                                     int centre);

private:
    const data::World& world_;
    bool high_detail_ = false;
    const amiga::LoadedImage& program_;
    mutable std::map<int, std::vector<data::DetailLevel>> vehicle_levels_; // read when first drawn
    const std::vector<data::DetailLevel>& vehicle_levels(int type) const;
    std::array<data::SmokeShape, 3> smoke_shapes_; // types 33-35
    GroundSpecks specks_;
    VisitTables visits_;
    data::Explosions explosions_;
    std::map<int, std::vector<data::DetailLevel>> levels_; // vehicle and scenery formats
    std::map<int, std::vector<data::DetailLevel>> ground_; // flat ground shapes by distance ($112D76)
};

} // namespace interceptor::game
