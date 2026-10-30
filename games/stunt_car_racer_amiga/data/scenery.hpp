#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include <cstdint>
#include <vector>

// The hills around every circuit: flat shapes standing on the horizon, each
// placed at a bearing and drawn in the view's angle space (they never come
// nearer). Decoded from the program's scenery tables.
namespace stuntcar::data {

// Offsets in eighths of the game's angle unit (8192 to the turn); y is
// height above the horizon.
struct SceneryPoint {
    int x{}, y{};
};

struct SceneryShape {
    std::vector<SceneryPoint> points;
    struct Polygon {
        std::uint8_t colour{};
        std::vector<int> corners; // indices into points, in drawing order
    };
    std::vector<Polygon> polygons;
};

struct Scenery {
    struct Placed {
        int bearing{}; // 256ths of a turn; shape x = 0 sits here
        int shape{};
    };
    std::vector<SceneryShape> shapes;
    std::vector<Placed> placed;
};

// Throws std::runtime_error when the tables do not decode as expected.
Scenery load_scenery(const Program&);

} // namespace stuntcar::data
