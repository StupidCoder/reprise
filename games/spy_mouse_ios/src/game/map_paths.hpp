// The roads of a world map and how the mouse finds its way along them (the original's
// MenuMapPathFinder, world/pathinfo/world_pathinfo_NN.xml.brd).
//
// The file is a map-editor drawing: "stops" (vertices with property 136, one per level node
// with the node's level uid as tag, or 139 for a plain junction) and polylines (property 138
// lines; all lines with one tag form one road). A road's points are its line ends; roads meet
// at stops whose position equals one of their points. A walk to a node is routed stop to stop
// over the roads (searchGraph) and becomes the mouse's path: from where the mouse stands onto
// its road, then each road's points in walking order.
#pragma once

#include "data/game_files.hpp"
#include "game/drawn_path.hpp"
#include "game/geometry.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace doc::game {

class MapPaths {
public:
    // `world` counts from 0. The file's y axis points down; the map's points up (y = 320 - y).
    static std::optional<MapPaths> load(const data::GameFiles& files, int world, std::string& error);

    // updatePathsStatus @0x1e7a40: which stops a walk may end at, and which gated roads are
    // shut. `locked(tag)` answers for level uids (999: the boss of this world not beaten).
    void updateStatus(const std::function<bool(int tag)>& locked, const std::function<bool(int tag)>& completed);

    // createPath @0x1e76e0: a walk from `from` to the stop nearest `target` (which must lie
    // within 30 of it and be open unless `evenLocked`). The points replace `path`'s; false if
    // there is no stop there or no way onto the roads.
    bool createPath(Vec2 target, Vec2 from, DrawnPath& path, bool evenLocked) const;

    // findClosestTouchNodeToPoint @0x1e6e88: the stop nearest `p` (junctions excluded), -1.
    int closestStop(Vec2 p) const;
    Vec2 stopPosition(int stop) const { return stops_[std::size_t(stop)].position; }
    int stopTag(int stop) const { return stops_[std::size_t(stop)].tag; }

private:
    static constexpr int kStop = 136, kJunction = 139, kGatedRoad = 137;
    struct Stop {
        Vec2 position;
        int property = 0;
        int tag = 0;
        bool locked = false;           // +0x55
        std::vector<int> roads;        // +0x2c roads that start or end here
        std::vector<int> neighbours;   // +0x38 stops sharing a road
        // searchGraph scratch (+0x50 previous, +0x54 visited, +0x58 cost)
        mutable int previous = -1;
        mutable bool visited = false;
        mutable float cost = 0.0f;
    };
    struct Road {
        std::vector<Vec2> points;  // ordered from `start` once the stops are linked
        int tag = 0;
        int start = -1, end = -1;  // stops at its ends
        int length = 0;            // whole units, summed per segment
        bool gated = false;        // drawn with property 137: shut until its level is won
        bool shut = false;
        bool ordered = false;
        mutable bool used = false;  // already walked in the walk being built
    };
    void linkStops();  // processPathLines @0x1e7b40
    int sharedRoad(int a, int b) const;
    // searchGraph @0x1e7238: cheapest stop-to-stop route by straight-line distance. With
    // `route`, the stops from `to` back to `from`; returns the route's summed cost.
    int search(int from, int to, std::vector<int>* route) const;
    int closestStopForStart(Vec2 from, int end) const;
    int roadUnder(Vec2 p, float radius) const;  // getPathPointIsOn @0x1e6f1c
    void appendStart(Vec2 from, int start, int end, std::vector<Vec2>& out) const;  // setupPathStart

    std::vector<Stop> stops_;
    std::vector<Road> roads_;
};

}  // namespace doc::game
