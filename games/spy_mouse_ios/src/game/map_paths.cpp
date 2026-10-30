#include "game/map_paths.hpp"

#include "data/brd.hpp"
#include "game/level_geometry.hpp"

#include <cstdio>

namespace doc::game {

std::optional<MapPaths> MapPaths::load(const data::GameFiles& files, int world, std::string& error) {
    char name[64];
    std::snprintf(name, sizeof name, "world/pathinfo/world_pathinfo_%02d.xml.brd", world + 1);
    auto root = data::loadChunkTree(files, name);
    const data::BrdNode* level = !root ? nullptr : root->name == "Level" ? &*root : root->child("Level");
    if (!level) {
        error = std::string("missing or damaged ") + name;
        return std::nullopt;
    }
    std::vector<Vec2> vertices;
    MapPaths paths;
    for (const data::BrdNode* v : level->childrenNamed("Vertex")) {
        const Vec2 p{v->getFloat("x").value_or(0.0f), 320.0f - v->getFloat("y").value_or(0.0f)};
        vertices.push_back(p);
        const int property = v->getInt("Property").value_or(0);
        if (property == kStop || property == kJunction) {
            Stop s;
            s.position = p;
            s.property = property;
            s.tag = v->getInt("Tag").value_or(0);
            paths.stops_.push_back(s);
        }
    }
    for (const data::BrdNode* l : level->childrenNamed("Line")) {
        const int a = l->getInt("VertexA").value_or(-1), b = l->getInt("VertexB").value_or(-1);
        const int tag = l->getInt("Tag").value_or(0);
        if (a < 0 || b < 0 || a >= int(vertices.size()) || b >= int(vertices.size())) continue;
        Road* road = nullptr;
        for (Road& r : paths.roads_)
            if (r.tag == tag) road = &r;
        if (!road) {
            paths.roads_.emplace_back();
            road = &paths.roads_.back();
            road->tag = tag;
        }
        road->gated = l->getInt("Property").value_or(0) == kGatedRoad;
        for (Vec2 p : {vertices[std::size_t(a)], vertices[std::size_t(b)]}) {
            bool known = false;
            for (Vec2 q : road->points) known = known || q == p;
            if (!known) road->points.push_back(p);
        }
    }
    paths.linkStops();
    return paths;
}

void MapPaths::linkStops() {
    for (Stop& s : stops_)
        for (std::size_t r = 0; r < roads_.size(); ++r)
            for (Vec2 p : roads_[r].points)
                if (p == s.position) {
                    s.roads.push_back(int(r));
                    break;
                }
    for (std::size_t i = 0; i < stops_.size(); ++i) {
        Stop& s = stops_[i];
        for (std::size_t j = 0; j < stops_.size(); ++j) {
            if (i == j) continue;
            bool shares = false;
            for (int a : s.roads)
                for (int b : stops_[j].roads) shares = shares || a == b;
            if (shares) s.neighbours.push_back(int(j));
        }
        // The first stop met on a road orders its points: nearest point next, from the stop on.
        for (int r : s.roads) {
            Road& road = roads_[std::size_t(r)];
            if (road.ordered) {
                road.end = int(i);
                continue;
            }
            road.start = int(i);
            road.ordered = true;
            std::vector<Vec2> left = road.points, ordered;
            Vec2 at = s.position;
            road.length = 0;
            while (!left.empty()) {
                std::size_t best = 0;
                float bestDistance = 1000000.0f;
                Vec2 next = at;
                for (std::size_t k = 0; k < left.size(); ++k) {
                    const float d = (left[k] - at).lengthSquared();
                    if (d < bestDistance) {
                        bestDistance = d;
                        next = left[k];
                        best = k;
                    }
                }
                road.length += int((next - at).length());
                ordered.push_back(next);
                at = next;
                left.erase(left.begin() + std::ptrdiff_t(best));
            }
            road.points = ordered;
        }
    }
}

void MapPaths::updateStatus(const std::function<bool(int)>& locked, const std::function<bool(int)>& completed) {
    for (Stop& s : stops_) s.locked = !(s.tag < 1 || s.tag == 998) && locked(s.tag);
    for (Road& r : roads_)
        if (r.gated) r.shut = !completed(r.tag);
}

int MapPaths::sharedRoad(int a, int b) const {
    for (int r : stops_[std::size_t(a)].roads)
        for (int q : stops_[std::size_t(b)].roads)
            if (r == q) return r;
    return -1;  // the original asserts
}

int MapPaths::search(int from, int to, std::vector<int>* route) const {
    for (const Stop& s : stops_) {
        s.visited = false;
        s.cost = 999999.0f;
    }
    const Stop& first = stops_[std::size_t(from)];
    first.visited = true;
    first.previous = -1;
    first.cost = 0.0f;
    std::vector<int> visited{from};
    int chosen = -1;
    do {
        // Every visited stop offers its open roads; the cheapest new stop joins.
        chosen = -1;
        float best = 999999.0f;
        for (int v : visited) {
            const Stop& at = stops_[std::size_t(v)];
            for (int n : at.neighbours) {
                const Stop& next = stops_[std::size_t(n)];
                if (next.visited) continue;
                const Road& road = roads_[std::size_t(sharedRoad(v, n))];
                if (road.gated && road.shut) continue;
                const float cost = at.cost + (next.position - at.position).length();
                if (cost < best) {
                    best = cost;
                    next.cost = cost;
                    next.previous = v;
                    chosen = n;
                }
            }
        }
        if (chosen < 0) return 0;
        stops_[std::size_t(chosen)].visited = true;
        visited.push_back(chosen);
    } while (chosen != to);
    int total = 0;
    for (int s = to; s >= 0; s = stops_[std::size_t(s)].previous) {
        total = int(float(total) + stops_[std::size_t(s)].cost);
        if (route) route->push_back(s);
    }
    return total;
}

int MapPaths::closestStop(Vec2 p) const {
    int best = -1;
    float bestDistance = 999999.0f;
    for (std::size_t i = 0; i < stops_.size(); ++i) {
        if (stops_[i].property == kJunction) continue;
        const float d = (stops_[i].position - p).lengthSquared();
        if (d < bestDistance) {
            bestDistance = d;
            best = int(i);
        }
    }
    return best;
}

int MapPaths::roadUnder(Vec2 p, float radius) const {
    for (std::size_t r = 0; r < roads_.size(); ++r) {
        const std::vector<Vec2>& pts = roads_[r].points;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            Vec2 closest;
            if (circleSegmentContact(p, radius, pts[i], pts[i + 1], closest) != 0) return int(r);
        }
    }
    return -1;
}

// findClosestTouchNodeToPointForPathStart @0x1e75d4: a mouse standing at a stop starts there;
// one out on a road starts from whichever end of the road is nearer the goal.
int MapPaths::closestStopForStart(Vec2 from, int end) const {
    const int nearest = closestStop(from);
    if (nearest < 0) return -1;
    if ((stops_[std::size_t(nearest)].position - from).lengthSquared() <= 225.0f) return nearest;
    const int r = roadUnder(from, 225.0f);
    if (r < 0) return -1;
    const Road& road = roads_[std::size_t(r)];
    return search(road.start, end, nullptr) < search(road.end, end, nullptr) ? road.start : road.end;
}

// setupPathStart @0x1e6fe4: from the mouse onto its road, along it to the start stop (the
// road's points ahead of the mouse, not the stop itself).
void MapPaths::appendStart(Vec2 from, int start, int end, std::vector<Vec2>& out) const {
    const int r = roadUnder(from, 15.0f);
    if (r < 0) return;  // the original asserts
    if (!((from - stops_[std::size_t(start)].position).lengthSquared() > 225.0f)) return;
    out.push_back(from);
    const Road& road = roads_[std::size_t(r)];
    const bool backwards = road.start == start;
    const int n = int(road.points.size());
    bool onRoad = false;
    for (int i = 0; i < n - 1; ++i) {
        const int index = backwards ? n - 1 - i : i;
        const Vec2 a = road.points[std::size_t(index)];
        const Vec2 b = road.points[std::size_t(index + (backwards ? -1 : 1))];
        if (!onRoad) {
            Vec2 closest;
            onRoad = circleSegmentContact(from, 15.0f, a, b, closest) != 0;
        } else if ((b - a).dot(a - from) >= 0.0f) {
            out.push_back(a);
        }
    }
    if (start == end) out.push_back(stops_[std::size_t(end)].position);
}

bool MapPaths::createPath(Vec2 target, Vec2 from, DrawnPath& path, bool evenLocked) const {
    // The target arrives as whole numbers (FPToFixed, toward zero).
    target = {float(int(target.x)), float(int(target.y))};
    int end = -1;
    float bestDistance = 999999.0f;
    for (std::size_t i = 0; i < stops_.size(); ++i) {
        if (stops_[i].property == kJunction) continue;
        const float d = (stops_[i].position - target).lengthSquared();
        if (d < bestDistance) {
            bestDistance = d;
            end = int(i);
        }
    }
    if (end < 0) return false;
    const Stop& goal = stops_[std::size_t(end)];
    if ((goal.position - target).length() > 30.0f || (goal.locked && !evenLocked)) return false;
    const int start = closestStopForStart(from, end);
    if (start < 0) return false;
    std::vector<int> route;  // from `end` back to `start`
    if (start != end) search(start, end, &route);
    for (const Road& r : roads_) r.used = false;
    std::vector<Vec2> points;
    appendStart(from, start, end, points);
    // Each road between consecutive stops, walked from the end nearer the stop reached so far.
    for (int k = int(route.size()) - 1; k > 0; --k) {
        const int at = route[std::size_t(k)];
        const Road& road = roads_[std::size_t(sharedRoad(at, route[std::size_t(k - 1)]))];
        if (road.used) continue;
        road.used = true;
        if (road.points.empty()) continue;
        const Vec2 here = stops_[std::size_t(at)].position;
        const bool forward =
            (road.points.back() - here).lengthSquared() >= (road.points.front() - here).lengthSquared();
        if (forward) points.insert(points.end(), road.points.begin(), road.points.end());
        else points.insert(points.end(), road.points.rbegin(), road.points.rend());
    }
    path.clear();
    for (Vec2 p : points) {
        if (path.size() >= DrawnPath::kCapacity) break;
        path.append(p);
    }
    return true;
}

}  // namespace doc::game
