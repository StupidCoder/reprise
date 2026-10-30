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

class Scene {
public:
    Scene(const data::Assets& assets, const data::World& world);

    // Draws into rows [0, bottom] of `image`, the horizontal centre at
    // column `centre` (159 in the original's 320 columns).
    void draw(IndexedImage& image, int centre, int bottom, const Camera& camera,
              const std::vector<Vehicle>& vehicles = {}, const std::vector<Spark>& sparks = {},
              const std::vector<SmokeColumn>& smoke = {}) const;

    // A vehicle type is drawn at this distance (one of its detail levels
    // reaches it).
    bool in_range(int type, double distance) const;

    // Where a world point shows (column, row), or nothing behind the camera.
    static std::optional<std::array<int, 2>> project(const Camera& camera, const std::array<double, 3>& point,
                                                     int centre);

private:
    const data::World& world_;
    const amiga::LoadedImage& program_;
    mutable std::map<int, std::vector<data::DetailLevel>> vehicle_levels_; // read when first drawn
    const std::vector<data::DetailLevel>& vehicle_levels(int type) const;
    std::array<data::SmokeShape, 3> smoke_shapes_; // types 33-35
    data::Explosions explosions_;
    std::map<int, std::vector<data::DetailLevel>> levels_; // vehicle and scenery formats
    std::map<int, std::vector<data::DetailLevel>> ground_; // flat ground shapes by distance ($112D76)
};

} // namespace interceptor::game
