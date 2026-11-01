// =============================================================================================
// WalkPlanner.cpp - routes over the course floors for touch control (enhancement, see header)
// =============================================================================================
#include "session/WalkPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>

namespace captain_toad::session {

namespace {

using captain_toad::RecoveredVec::V3;

constexpr float kCell = 25.0f;       // grid spacing (world units)
constexpr int kRange = 160;          // cells either way of the start (4000 units)
constexpr float kStep = 30.0f;       // the most a floor may rise or fall from a cell to the next
constexpr float kKnee = 45.0f;       // the height above the floors where walls are looked for
constexpr float kHeadroom = 60.0f;   // a floor with another face closer above is not walkable
constexpr float kLedgeCost = 4.0f;   // cost factor of cells next to a ledge
// Drops: less than the fall height where the game plays the panic fall (FallPanicStartHeight 700),
// with a fixed price so a walk down that is not much longer wins.
constexpr float kMaxDrop = 650.0f;
constexpr float kDropCost = 150.0f;
// Ladders: taken at the foot from this far out on the climbing side, left at the top onto the
// ledge behind the ladder; the price of a climb is its height plus this.
constexpr float kLadderApproach = 80.0f;
constexpr float kLadderLedge = 40.0f;
constexpr float kLadderCost = 150.0f;
constexpr int kMaxExpanded = 12000;   // search budget (the long route to lever A in the first course: 1800)
// The longest route as a factor of the straight distance (plus a little for short walks): the
// route rounds corners and blocks; it does not find its way through the course.
constexpr float kDetour = 2.0f;
constexpr float kDetourExtra = 500.0f;

struct Planner {
    const std::vector<captain_toad::RecoveredCollision::Part>& parts;
    V3 origin;
    float top;
    std::unordered_map<int64_t, std::vector<float>> floorCache;
    std::vector<captain_toad::RecoveredCollision::HitInfo> hits;

    static int64_t cellKey(int i, int j) { return int64_t(i + 4096) << 16 | int64_t(j + 4096); }
    V3 centre(int i, int j, float y) const { return {origin.x + i * kCell, y, origin.z + j * kCell}; }

    // The walkable floors of a cell, top first: upward faces under a vertical ray, without the
    // ones that something covers within kHeadroom.
    const std::vector<float>& floors(int i, int j) {
        auto [it, fresh] = floorCache.try_emplace(cellKey(i, j));
        if (!fresh) {
            return it->second;
        }
        hits.clear();
        captain_toad::RecoveredCollision::segmentCheck(parts, centre(i, j, top), V3{0, -(top - origin.y + 3000.0f), 0},
                                                       hits);
        std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.depth < b.depth; });
        float above = 1e30f;   // the lowest face seen so far (anything over the floor below)
        for (const auto& h : hits) {
            const float y = h.contact.y;
            if (h.faceNormal.y > 0.7f && above - y > kHeadroom &&
                (it->second.empty() || std::fabs(it->second.back() - y) > 5.0f)) {
                it->second.push_back(y);
            }
            above = std::min(above, y);
        }
        return it->second;
    }

    // The floor of cell (i, j) a step from height y reaches (the nearest within kStep * scale).
    bool stepTo(int i, int j, float y, float scale, float& out) {
        float best = kStep * scale;
        bool found = false;
        for (float f : floors(i, j)) {
            if (std::fabs(f - y) <= best) {
                best = std::fabs(f - y);
                out = f;
                found = true;
            }
        }
        return found;
    }

    // A drop from height y into cell (i, j): the highest floor there below the step range and
    // within kMaxDrop (where he lands), with room to land (the cell beyond, (i + di, j + dj), has
    // about the same floor).
    bool dropTo(int i, int j, int di, int dj, float y, float& out) {
        for (float f : floors(i, j)) {   // top first
            if (f >= y - kStep) {
                continue;
            }
            if (y - f > kMaxDrop) {
                return false;
            }
            float beyond;
            if (!stepTo(i + di, j + dj, f, 1.0f, beyond)) {
                return false;
            }
            out = f;
            return true;
        }
        return false;
    }

    // The cell and floor near a point (a ladder's end): the floor within 60 of its height (the
    // top piece sits 50 below the ledge).
    bool cellAt(const V3& q, int& i, int& j, float& y) {
        i = int(std::lround((q.x - origin.x) / kCell));
        j = int(std::lround((q.z - origin.z) / kCell));
        if (std::abs(i) > kRange || std::abs(j) > kRange) {
            return false;
        }
        for (float f : floors(i, j)) {
            if (std::fabs(f - q.y) <= 60.0f) {
                y = f;
                return true;
            }
        }
        return false;
    }

    bool wallBetween(const V3& a, const V3& b) {
        hits.clear();
        const V3 from{a.x, a.y + kKnee, a.z};
        const V3 to{b.x, b.y + kKnee, b.z};
        captain_toad::RecoveredCollision::segmentCheck(parts, from, captain_toad::RecoveredVec::sub(to, from), hits);
        for (const auto& h : hits) {
            if (std::fabs(h.faceNormal.y) < 0.7f) {
                return true;
            }
        }
        return false;
    }

    bool nearLedge(int i, int j, float y) {
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        float f;
        for (const auto& o : d) {
            if (!stepTo(i + o[0], j + o[1], y, 1.0f, f)) {
                return true;
            }
        }
        return false;
    }
};

struct Node {
    int i, j;
    float y;
    float cost;
    int parent;
    int climb;   // reached by ladder: 1 up, -1 down
    V3 grab;
};

// A ladder in grid terms: the foot's approach cell and the top's ledge cell, and where to walk to
// take hold at either end.
struct LadderEnds {
    int bi, bj, ti, tj;
    float by, ty;
    V3 grabBottom, grabTop;
    float cost;
};

}   // namespace

WalkRoute planWalk(const std::vector<captain_toad::RecoveredCollision::Part>& parts,
                   const std::vector<LadderSpan>& ladders, const V3& from, const V3& to, float reach) {
    WalkRoute route;
    Planner p{parts, from, std::max(from.y, to.y) + 2000.0f, {}, {}};
    reach = std::max(reach, kCell * 0.75f);
    auto goal = [&](const Node& n) {
        const float dx = p.centre(n.i, n.j, 0).x - to.x, dz = p.centre(n.i, n.j, 0).z - to.z;
        return dx * dx + dz * dz <= reach * reach && std::fabs(n.y - to.y) <= 60.0f;
    };
    auto estimate = [&](int i, int j) {
        const V3 c = p.centre(i, j, 0);
        return std::max(0.0f, std::hypot(c.x - to.x, c.z - to.z) - reach);
    };
    // The start: the floor under the protagonist (he may stand a little above or in the air).
    float y0 = 0;
    {
        bool found = false;
        float best = 1e30f;
        for (float f : p.floors(0, 0)) {
            if (f <= from.y + 20.0f && from.y - f < best) {
                best = from.y - f;
                y0 = f;
                found = true;
            }
        }
        if (!found) {
            return route;
        }
    }
    const float budget = std::hypot(to.x - from.x, to.z - from.z) * kDetour + kDetourExtra;
    std::vector<LadderEnds> ladderEnds;
    for (const LadderSpan& l : ladders) {
        LadderEnds e;
        const V3 foot =
            captain_toad::RecoveredVec::add(l.bottom, captain_toad::RecoveredVec::scale(l.front, kLadderApproach));
        const V3 ledge =
            captain_toad::RecoveredVec::sub(l.top, captain_toad::RecoveredVec::scale(l.front, kLadderLedge));
        if (p.cellAt(foot, e.bi, e.bj, e.by) && p.cellAt(ledge, e.ti, e.tj, e.ty)) {
            // Into the foot against the ladder's front; off the ledge along it, past the top.
            e.grabBottom = captain_toad::RecoveredVec::sub(l.bottom, captain_toad::RecoveredVec::scale(l.front, 20.0f));
            e.grabTop = captain_toad::RecoveredVec::add(l.top, captain_toad::RecoveredVec::scale(l.front, 60.0f));
            e.cost = l.top.y - l.bottom.y + kLadderCost;
            ladderEnds.push_back(e);
        }
    }
    // Walking off a ledge where a ladder's top is, the ladder takes hold of him (its BindTop, r100,
    // 50 up): no drop there, the ladder is the way down.
    auto nearLadderTop = [&](const V3& q) {
        for (const LadderSpan& l : ladders) {
            if (std::hypot(q.x - l.top.x, q.z - l.top.z) < 125.0f && std::fabs(q.y - l.top.y) < 150.0f) {
                return true;
            }
        }
        return false;
    };
    std::vector<Node> nodes{{0, 0, y0, 0.0f, -1, 0, {}}};
    std::unordered_map<int64_t, int> index;   // cell and floor -> node
    auto nodeKey = [](int i, int j, float y) {
        return Planner::cellKey(i, j) << 20 | (int64_t(int(std::lround(y)) + 0x80000) & 0xfffff);
    };
    index[nodeKey(0, 0, y0)] = 0;
    using Entry = std::pair<float, int>;   // estimated total, node
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    open.push({estimate(0, 0), 0});
    std::vector<bool> closed(1, false);
    int found = -1;
    for (int expanded = 0; !open.empty() && expanded < kMaxExpanded; expanded++) {
        const int at = open.top().second;
        open.pop();
        if (closed[size_t(at)]) {
            continue;
        }
        closed[size_t(at)] = true;
        const Node n = nodes[size_t(at)];
        if (goal(n)) {
            found = at;
            break;
        }
        if (n.cost + estimate(n.i, n.j) > budget) {
            break;   // the best remaining route is too long already
        }
        auto relax = [&](int i, int j, float y, float cost, int climb, const V3& grab) {
            const int64_t key = nodeKey(i, j, y);
            auto it = index.find(key);
            if (it == index.end()) {
                index[key] = int(nodes.size());
                nodes.push_back({i, j, y, cost, at, climb, grab});
                closed.push_back(false);
                open.push({cost + estimate(i, j), int(nodes.size()) - 1});
            } else if (!closed[size_t(it->second)] && cost < nodes[size_t(it->second)].cost) {
                Node& m = nodes[size_t(it->second)];
                m.cost = cost;
                m.parent = at;
                m.climb = climb;
                m.grab = grab;
                open.push({cost + estimate(i, j), it->second});
            }
        };
        const V3 a = p.centre(n.i, n.j, n.y);
        for (int di = -1; di <= 1; di++) {
            for (int dj = -1; dj <= 1; dj++) {
                const int i = n.i + di, j = n.j + dj;
                if ((di == 0 && dj == 0) || std::abs(i) > kRange || std::abs(j) > kRange) {
                    continue;
                }
                const float length = di != 0 && dj != 0 ? kCell * 1.4142f : kCell;
                float y;
                if (p.stepTo(i, j, n.y, length / kCell, y)) {
                    if (!p.wallBetween(a, p.centre(i, j, y))) {
                        relax(i, j, y, n.cost + length * (p.nearLedge(i, j, y) ? kLedgeCost : 1.0f), 0, {});
                    }
                } else if (p.dropTo(i, j, di, dj, n.y, y) && !nearLadderTop(a) &&
                           !p.wallBetween(a, p.centre(i, j, n.y))) {
                    relax(i, j, y, n.cost + length + kDropCost, 0, {});
                }
            }
        }
        for (const LadderEnds& e : ladderEnds) {
            if (n.i == e.bi && n.j == e.bj && std::fabs(n.y - e.by) < 1.0f) {
                relax(e.ti, e.tj, e.ty, n.cost + e.cost, 1, e.grabBottom);
            } else if (n.i == e.ti && n.j == e.tj && std::fabs(n.y - e.ty) < 1.0f) {
                relax(e.bi, e.bj, e.by, n.cost + e.cost, -1, e.grabTop);
            }
        }
    }
    if (found < 0) {
        return route;
    }
    for (int k = found; k > 0; k = nodes[size_t(k)].parent) {
        const Node& n = nodes[size_t(k)];
        route.points.push_back({p.centre(n.i, n.j, n.y), n.climb, n.grab});
    }
    std::reverse(route.points.begin(), route.points.end());
    return route;
}

V3 WalkRoute::steerPoint(const V3& pos) {
    auto flat = [&](const V3& q) {
        return std::hypot(q.x - pos.x, q.z - pos.z);
    };
    // A ladder point is passed only by climbing (the caller advances after the climb).
    while (next + 1 < points.size() && points[next].climb == 0 && flat(points[next].pos) < 30.0f) {
        next++;
    }
    if (points[next].climb != 0) {
        return points[next].grab;
    }
    size_t pick = next;
    for (size_t k = next + 1; k < points.size() && k <= next + 4 && points[k].climb == 0; k++) {
        if (flat(points[k].pos) <= 90.0f) {
            pick = k;
        }
    }
    return points[pick].pos;
}

}   // namespace captain_toad::session
