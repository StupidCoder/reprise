#include "stunt_car_racer_amiga/data/scenery.hpp"
#include <algorithm>
#include <stdexcept>

namespace stuntcar::data {
namespace {

// Scenery sets: a table of pointers; the game always uses the first. A set
// is a count, then (bearing, shape) byte pairs.
constexpr std::uint32_t scenery_sets = 0x69A80;
// Shapes: two pointers each, the point list and a list of shared values.
// A point list is a count and (x, y) words; a negative word stands for the
// next shared value. Then the edges (a count, pairs of point offsets) and the
// polygons (a count; each a colour, an edge count of 3 or 4 and the edges as
// offsets into the edge list, four bytes apiece).
constexpr std::uint32_t scenery_shapes = 0x699B8;

SceneryShape decode_shape(const Program& program, int index) {
    std::uint32_t at = program.u32(scenery_shapes + std::uint32_t(index) * 8);
    std::uint32_t shared = program.u32(scenery_shapes + std::uint32_t(index) * 8 + 4);
    const auto word = [&](std::uint32_t& p) {
        const auto v = std::int16_t(program.u16(p));
        p += 2;
        return int(v);
    };
    SceneryShape shape;
    const int count = word(at);
    for (int i = 0; i < count; ++i) {
        int xy[2];
        for (auto& v : xy) {
            v = word(at);
            if (v < 0) v = word(shared);
        }
        shape.points.push_back({xy[0], xy[1]});
    }
    std::vector<std::pair<int, int>> edges;
    const int edge_count = program.u8(at++);
    for (int i = 0; i < edge_count; ++i, at += 2) edges.emplace_back(program.u8(at) / 2, program.u8(at + 1) / 2);
    const int polygon_count = program.u8(at++);
    for (int i = 0; i < polygon_count; ++i) {
        SceneryShape::Polygon polygon;
        polygon.colour = program.u8(at++);
        const int sides = program.u8(at++);
        std::vector<std::pair<int, int>> own;
        for (int k = 0; k < sides; ++k) own.push_back(edges.at(program.u8(at++) / 4));
        // Walk the edges into a loop of corners.
        polygon.corners = {own[0].first, own[0].second};
        own.erase(own.begin());
        while (!own.empty()) {
            const int last = polygon.corners.back();
            const auto next = std::find_if(own.begin(), own.end(),
                                           [&](const auto& e) { return e.first == last || e.second == last; });
            if (next == own.end()) throw std::runtime_error("A scenery polygon is not closed.");
            const int corner = next->first == last ? next->second : next->first;
            own.erase(next);
            if (own.empty()) {
                if (corner != polygon.corners.front()) throw std::runtime_error("A scenery polygon is not closed.");
            } else {
                polygon.corners.push_back(corner);
            }
        }
        shape.polygons.push_back(std::move(polygon));
    }
    return shape;
}

} // namespace

Scenery load_scenery(const Program& program) {
    Scenery scenery;
    std::uint32_t at = program.u32(scenery_sets);
    const int count = program.u8(at++);
    int shapes = 0;
    for (int i = 0; i < count; ++i, at += 2) {
        scenery.placed.push_back({program.u8(at), program.u8(at + 1)});
        shapes = std::max(shapes, int(program.u8(at + 1)) + 1);
    }
    for (int i = 0; i < shapes; ++i) scenery.shapes.push_back(decode_shape(program, i));
    return scenery;
}

} // namespace stuntcar::data
