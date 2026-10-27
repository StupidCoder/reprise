// The crowd race (not the original game; crowd.hpp).
#include "marble_madness_amiga/game/crowd.hpp"
#include "marble_madness_amiga/game/race.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace marble_madness::game {
namespace {

// What a marble can roll over from cell to cell: a wall at most `rise`
// high, a floor at most `climb` higher, a drop of at most `fall`.
constexpr int rise = 3, climb = 10, fall = 48;
// The four directions and the side of the cell a marble leaves by
// (the probe's sides: 0 x = 0, 1 y = 8, 2 x = 8, 3 y = 0).
constexpr int step_x[4] = {1, -1, 0, 0}, step_y[4] = {0, 0, 1, -1};
constexpr int leaving_side[4] = {2, 0, 1, 3};

struct Line {
    std::vector<std::pair<int, int>> cells;
    int first{}, second{};                    // the zones on its two sides
    std::vector<std::pair<int, int>> beyond[2]; // the cells just past it on each side
};

// The Track's zone lines (header +8, five bytes each as $1130BE reads
// them: a, b, end, zone on the first side, zone on the other; a < 0: the
// line runs along x at y = b, else along y at x = a; both zones -1 ends).
std::vector<Line> zone_lines(const data::GameFiles& files, data::Course course) {
    const auto& t = files.track(course);
    std::vector<Line> lines;
    for (std::uint32_t e = t.u32(files.track_header(course) + 8);; e += 5) {
        int a = t.s8(e);
        const int b = std::abs(int(t.s8(e + 1)));
        const int end = t.s8(e + 2);
        Line line;
        line.first = t.s8(e + 3);
        line.second = t.s8(e + 4);
        if (line.first == -1 && line.second == -1) {
            break;
        }
        const bool along_x = a < 0;
        a = std::abs(a);
        for (int k = along_x ? a : b; k <= end; ++k) {
            const int x = along_x ? k : a, y = along_x ? b : k;
            line.cells.push_back({x, y});
            line.beyond[0].push_back({along_x ? x : x - 1, along_x ? y - 1 : y});
            line.beyond[1].push_back({along_x ? x : x + 1, along_x ? y + 1 : y});
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

// Where the original moves a marble through a funnel or lift (course
// pixels: where it went in, where it came out), seen in recordings of the
// original game (.analysis/marble_madness_amiga/harness/runs): beg_goal
// updates 700 and 792, int_goal 333, sil_funnel 196, ult_goal 191. On
// Ultimate the funnel is reached by a jump over the gap: ult_goal backs up
// to 181,108 and pushes with fire straight along y to the gap (updates
// 126-180); that run-up starts at `from`.
struct PortalSeen {
    data::Course course;
    int in_x, in_y, out_x, out_y;
    int from_x{-1}, from_y{-1};
    int zone_out{-99}; // the zone on the far side, when the way crosses a zone line
    bool bridge{};     // a drawbridge: carried across only off the screen
};
constexpr PortalSeen portals_seen[] = {
    {data::Course::beginner, 492, 444, 488, 484, -1, -1, 6, true}, // the drawbridge (beg_goal 581-617)
    {data::Course::beginner, 507, 619, 524, 612},
    {data::Course::beginner, 649, 686, 668, 740},
    {data::Course::intermediate, 620, 654, 620, 596},
    {data::Course::silly, 711, 718, 684, 688},
    {data::Course::ultimate, 181, 257, 240, 320, 181, 108},
};

} // namespace

CrowdMap::CrowdMap(const data::GameFiles& files, data::Course course, const Terrain& terrain) {
    cells_.resize(std::size_t(grid * grid));
    std::vector<std::array<std::int8_t, 4>> kinds(cells_.size());
    std::vector<std::array<std::int16_t, 4>> heights(cells_.size());
    Probe probe;
    for (int cy = 0; cy < grid; ++cy) {
        for (int cx = 0; cx < grid; ++cx) {
            const std::int32_t x = (cx * 8 + 4) << 16, y = (cy * 8 + 4) << 16;
            probe.place(x, y);
            const std::int32_t z = probe.ground_height_here(terrain);
            if (z == 0) {
                continue;
            }
            probe.locate(terrain, x, y, z);
            probe.ground_height();
            probe.analyse_sides();
            const std::size_t i = std::size_t(cy * grid + cx);
            cells_[i].floor = std::int16_t(z >> 16);
            kinds[i] = probe.side_kind;
            heights[i] = probe.side_height;
            cells_[i].flat = std::all_of(probe.side_kind.begin(), probe.side_kind.end(), [](auto k) { return k == 0; });
        }
    }
    // Moving ground (Beginner's drawbridge, the wave, Ultimate's goal area:
    // height map codes $800-$FFF that the regions change) without floor
    // now: as high as the floor beside it (the bridge down).
    for (int pass = 0; pass < 6; ++pass) {
        std::vector<std::int16_t> filled(cells_.size(), 0);
        for (int cy = 0; cy < grid; ++cy) {
            for (int cx = 0; cx < grid; ++cx) {
                const std::size_t i = std::size_t(cy * grid + cx);
                const int code = terrain.stored_code(cx, cy);
                if (cells_[i].floor != 0 || code < 0x800 || code >= 0x1000) {
                    continue;
                }
                for (int d = 0; d < 4; ++d) {
                    if (const Cell* n = cell(cx + step_x[d], cy + step_y[d]); n != nullptr) {
                        filled[i] = std::max(filled[i], n->floor);
                    }
                }
            }
        }
        for (std::size_t i = 0; i < cells_.size(); ++i) {
            if (filled[i] != 0) {
                cells_[i].floor = filled[i];
                cells_[i].flat = true;
                kinds[i] = {};
                heights[i] = {};
            }
        }
    }
    for (int cy = 0; cy < grid; ++cy) {
        for (int cx = 0; cx < grid; ++cx) {
            const std::size_t i = std::size_t(cy * grid + cx);
            if (cells_[i].floor == 0) {
                continue;
            }
            for (int d = 0; d < 4; ++d) {
                const Cell* to = cell(cx + step_x[d], cy + step_y[d]);
                if (to == nullptr || to->floor == 0) {
                    continue;
                }
                const int kind = kinds[i][std::size_t(leaving_side[d])];
                const int wall = heights[i][std::size_t(leaving_side[d])];
                const int up = to->floor - cells_[i].floor;
                if (((kind == 1 || kind == 2) && wall > rise) || up > climb || -up > fall) {
                    continue;
                }
                cells_[i].moves = std::uint8_t(cells_[i].moves | 1 << d);
            }
        }
    }

    for (int cy = 0; cy < grid; ++cy) {
        for (int cx = 0; cx < grid; ++cx) {
            Cell& c = cells_[std::size_t(cy * grid + cx)];
            if (c.floor == 0) {
                continue;
            }
            int room = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const Cell* n = cell(cx + dx, cy + dy);
                    room += n != nullptr && n->floor != 0 && std::abs(n->floor - c.floor) <= 8 ? 1 : 0;
                }
            }
            c.room = std::uint8_t(room);
        }
    }

    for (const auto& p : portals_seen) {
        if (p.course == course) {
            portals_.push_back({p.in_x >> 3, p.in_y >> 3, p.out_x >> 3, p.out_y >> 3, p.from_x < 0 ? -1 : p.from_x >> 3,
                                p.from_y < 0 ? -1 : p.from_y >> 3, p.zone_out, p.bridge});
        }
    }

    // Each zone's way out: the lines to its neighbour nearest the goal.
    const auto lines = zone_lines(files, course);
    std::map<int, int> rank{{-1, 0}};
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& l : lines) {
            for (const auto& [from, to] : {std::pair{l.first, l.second}, std::pair{l.second, l.first}}) {
                if (rank.contains(to) && (!rank.contains(from) || rank[from] > rank[to] + 1)) {
                    rank[from] = rank[to] + 1;
                    changed = true;
                }
            }
        }
    }
    std::vector<std::pair<int, int>> goal_seeds;
    for (const auto& l : lines) {
        for (int side = 0; side < 2; ++side) {
            if ((side == 0 ? l.first : l.second) == -1) {
                goal_seeds.insert(goal_seeds.end(), l.beyond[side].begin(), l.beyond[side].end());
            }
        }
    }
    goal_field_ = flood(goal_seeds);
    ranks_ = rank;
    for (const auto& [zone, r] : rank) {
        if (zone == -1) {
            continue;
        }
        std::vector<std::pair<int, int>> seeds;
        for (const auto& l : lines) {
            for (int side = 0; side < 2; ++side) {
                const int here = side == 0 ? l.second : l.first, there = side == 0 ? l.first : l.second;
                if (here == zone && rank.contains(there) && rank.at(there) < r) {
                    seeds.insert(seeds.end(), l.beyond[side].begin(), l.beyond[side].end());
                }
            }
        }
        if (!seeds.empty()) {
            fields_[zone] = flood(seeds);
        }
    }
    find_starts(files, course);
}

const CrowdMap::Cell* CrowdMap::cell(int cx, int cy) const {
    if (cx < 0 || cy < 0 || cx >= grid || cy >= grid) {
        return nullptr;
    }
    return &cells_[std::size_t(cy * grid + cx)];
}

int CrowdMap::floor(int cx, int cy) const {
    const Cell* c = cell(cx, cy);
    return c != nullptr ? c->floor : 0;
}

std::optional<CrowdMap::Exit> CrowdMap::funnel_exit(int cx, int cy) const {
    for (const auto& p : portals_) {
        if (std::max(std::abs(p.in_x - cx), std::abs(p.in_y - cy)) <= 1) {
            return Exit{p.out_x * 8 + 4, p.out_y * 8 + 4, p.zone_out};
        }
    }
    return std::nullopt;
}

bool CrowdMap::near_way_in(int cx, int cy) const {
    return std::any_of(portals_.begin(), portals_.end(), [&](const Portal& p) {
        return !p.bridge && std::max(std::abs(p.in_x - cx), std::abs(p.in_y - cy)) <= 1;
    });
}

int CrowdMap::rank(int zone) const {
    const auto it = ranks_.find(zone);
    return it != ranks_.end() ? it->second : 99;
}

std::optional<CrowdMap::Lane> CrowdMap::run_up(int cx, int cy) const {
    for (const auto& p : portals_) {
        if (p.from_x < 0) {
            continue;
        }
        // In the lane: within a cell of the line from its first cell to
        // the way in, between the two.
        const double lx = p.in_x - p.from_x, ly = p.in_y - p.from_y;
        const double length = std::hypot(lx, ly);
        const double along = ((cx - p.from_x) * lx + (cy - p.from_y) * ly) / length;
        const double across = std::abs((cx - p.from_x) * ly - (cy - p.from_y) * lx) / length;
        if (along >= -1 && along <= length + 1 && across <= 1) {
            return Lane{p.from_x * 8 + 4, p.from_y * 8 + 4, p.in_x * 8 + 4 + int(lx / length * 32),
                        p.in_y * 8 + 4 + int(ly / length * 32), along};
        }
    }
    return std::nullopt;
}

int CrowdMap::room(int cx, int cy) const {
    const Cell* c = cell(cx, cy);
    return c != nullptr ? c->room : 0;
}

bool CrowdMap::can_move(int cx, int cy, int d) const {
    const Cell* c = cell(cx, cy);
    return c != nullptr && (c->moves >> d & 1) != 0;
}

std::uint16_t CrowdMap::to_go(int zone, int cx, int cy) const {
    if (cell(cx, cy) == nullptr) {
        return none;
    }
    const auto it = fields_.find(zone);
    const auto& field = it != fields_.end() ? it->second : goal_field_;
    return field[std::size_t(cy * grid + cx)];
}

// Distances back from the seeds: a cell gets its neighbour's distance plus
// a step that costs more the fewer ways on it has (near an edge).
std::vector<std::uint16_t> CrowdMap::flood(const std::vector<std::pair<int, int>>& seeds) const {
    std::vector<std::uint16_t> to_go(cells_.size(), none);
    using Entry = std::pair<int, int>; // distance, cell index
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (const auto& [x, y] : seeds) {
        if (const Cell* c = cell(x, y); c != nullptr && c->floor != 0) {
            to_go[std::size_t(y * grid + x)] = 0;
            queue.push({0, y * grid + x});
        }
    }
    while (!queue.empty()) {
        const auto [d, i] = queue.top();
        queue.pop();
        if (d != to_go[std::size_t(i)]) {
            continue;
        }
        const int ax = i % grid, ay = i / grid;
        // Through a funnel: its way in is as near as its way out.
        // (A run-up's lane counts from its first cell.)
        for (const auto& p : portals_) {
            if (p.out_x == ax && p.out_y == ay) {
                const int x = p.from_x >= 0 ? p.from_x : p.in_x, y = p.from_x >= 0 ? p.from_y : p.in_y;
                auto& slot = to_go[std::size_t(y * grid + x)];
                if (d + 4 < slot) {
                    slot = std::uint16_t(d + 4);
                    queue.push({d + 4, y * grid + x});
                }
            }
        }
        for (int dir = 0; dir < 4; ++dir) {
            // A marble on cell b can roll onto a (the opposite direction).
            const int bx = ax - step_x[dir], by = ay - step_y[dir];
            const Cell* b = cell(bx, by);
            if (b == nullptr || (b->moves >> dir & 1) == 0) {
                continue;
            }
            const int ways = __builtin_popcount(b->moves);
            int step = 4 + (4 - ways) * 5;
            // Dropping down costs more, much more onto a narrow way (the
            // marble lands with its speed and may roll off again).
            const Cell* a = cell(ax, ay);
            if (const int drop = b->floor - a->floor; drop > 6) {
                step += a->room < 15 ? drop * 4 : drop;
            }
            const int next = d + step;
            auto& slot = to_go[std::size_t(by * grid + bx)];
            if (next < slot) {
                slot = std::uint16_t(next);
                queue.push({next, by * grid + bx});
            }
        }
    }
    return to_go;
}

// Seven spots on the start's platform: cells joined to the start cell
// at about its height, three round the first player's start, four round
// the second's (Aerial's two launch platforms). The middle of the
// platform before its edges, the nearest first, two cells apart where
// there is room, else one (marbles a cell apart do not touch).
void CrowdMap::find_starts(const data::GameFiles& files, data::Course course) {
    const auto& t = files.track(course);
    const std::uint32_t descriptor = t.u32(files.track_header(course));
    std::array<std::pair<int, int>, 2> start{};
    for (int p = 0; p < 2; ++p) {
        const std::uint16_t w = t.u16(descriptor + 0x14 + std::uint32_t(p) * 2);
        start[std::size_t(p)] = {w >> 8, w & 0xFF};
    }
    std::vector<std::pair<int, int>> taken(start.begin(), start.end());
    std::size_t n = 0;
    for (int p = 0; p < 2; ++p) {
        const auto [sx, sy] = start[std::size_t(p)];
        const int height = floor(sx, sy);
        const std::size_t wanted = p == 0 ? 3 : 4;
        // The platform: cells joined to the start, at most 2 off its height.
        struct Spot {
            int x, y, distance;
            bool edge;
        };
        std::vector<Spot> platform;
        std::vector<std::pair<int, int>> queue{{sx, sy}};
        std::vector<bool> seen(cells_.size());
        seen[std::size_t(sy * grid + sx)] = true;
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const auto [x, y] = queue[q];
            const Cell* c = cell(x, y);
            platform.push_back({x, y, std::max(std::abs(x - sx), std::abs(y - sy)), !c->flat});
            for (int d = 0; d < 4; ++d) {
                const int nx = x + step_x[d], ny = y + step_y[d];
                const Cell* to = cell(nx, ny);
                if (to == nullptr || seen[std::size_t(ny * grid + nx)] || std::abs(to->floor - height) > 2 ||
                    std::max(std::abs(nx - sx), std::abs(ny - sy)) > 5) {
                    continue;
                }
                seen[std::size_t(ny * grid + nx)] = true;
                queue.push_back({nx, ny});
            }
        }
        std::stable_sort(platform.begin(), platform.end(), [](const Spot& a, const Spot& b) {
            return a.edge != b.edge ? !a.edge : a.distance < b.distance;
        });
        std::vector<std::pair<int, int>> found;
        for (int apart : {2, 1}) {
            for (const auto& spot : platform) {
                if (found.size() >= wanted) {
                    break;
                }
                const bool clear = std::all_of(taken.begin(), taken.end(), [&](const auto& c) {
                    return std::max(std::abs(c.first - spot.x), std::abs(c.second - spot.y)) >= apart;
                });
                if (clear) {
                    found.push_back({spot.x, spot.y});
                    taken.push_back(found.back());
                }
            }
        }
        // No room at all (not seen on any course): on the start cell.
        while (found.size() < wanted) {
            found.push_back({sx, sy});
        }
        for (const auto& [x, y] : found) {
            starts_[n++] = {x * 8 + 4, y * 8 + 4};
        }
    }
}

// --- The race's side ---

void Race::set_crowd(bool on) {
    if (on == crowd_) {
        return;
    }
    const Probe probe = probe_; // spawning looks at the ground; the race's probe stays as it was
    if (on) {
        if (!crowd_map_) {
            crowd_map_ = std::make_shared<const CrowdMap>(*files_, course_, terrain_);
        }
        crowd_ = true;
        marble_count_ = max_marbles;
        display_.set_limit(DisplayList::max_capacity);
        for (int i = players_; i < max_marbles; ++i) {
            spawn_bot(i);
        }
    } else {
        for (int i = players_; i < max_marbles; ++i) {
            if (marbles[std::size_t(i)].status != 0) {
                hide(1, std::uint8_t(i));
            }
            marbles[std::size_t(i)] = Marble{};
            marbles[std::size_t(i)].player = std::uint8_t(i);
        }
        crowd_ = false;
        marble_count_ = players_;
        display_.set_limit(DisplayList::capacity);
    }
    probe_ = probe;
}

// A crowd marble on its start spot, as the course start places the
// players' (without a clock).
void Race::spawn_bot(int i) {
    Marble& m = marbles[std::size_t(i)];
    m = Marble{};
    m.player = std::uint8_t(i);
    const auto [x, y] = crowd_map_->starts()[std::size_t(i - 1)];
    m.x = x << 16;
    m.y = y << 16;
    probe_.invalidate();
    locate(m);
    m.z = probe_.ground_height();
    m.status = 1;
    m.mode = Marble::rolling;
    m.tenths = 9;
    m.timer_frames = -1;
    reset_shine(m);
    project(m, false);
    rotate_shine(m);
    check_visible(m);
    show(1, m.player);
    drivers_[std::size_t(i)] = new_driver(i);
}

// A driver with its own pace, from a steady roll to nearly flat out, and
// its own dice.
CrowdDriver Race::new_driver(int i) const {
    static constexpr std::int32_t paces[max_marbles] = {0x24000, 0x2C000, 0x20000, 0x30000,
                                                         0x26000, 0x1C000, 0x34000, 0x28000};
    CrowdDriver d;
    d.top_speed = paces[i];
    d.seed = std::uint32_t(0x9E3779B9u * std::uint32_t(i + 1) + std::uint32_t(course_));
    return d;
}

// The course's moving pieces (funnels and lifts among them) run only near
// the screen, as in the original. A crowd marble well off the screen that
// reaches a funnel's way in is carried to its way out, as the funnel would.
void Race::carry_off_screen(Marble& m) {
    if (m.status != 1 || !crowd_map_ || m.mode == Marble::respawning || m.mode == Marble::breaking) {
        return;
    }
    // (Visible from -$10 to $100, $11F486; a margin either way.)
    const int screen_y = std::int16_t(m.screen[0]);
    if (screen_y > -0x30 && screen_y < 0x130) {
        return; // near enough: the funnel itself
    }
    const auto out = crowd_map_->funnel_exit(m.x >> 19, m.y >> 19);
    if (!out) {
        return;
    }
    // To a free spot: the way out or a cell beside it at its height (two
    // marbles on one spot would hold each other there); none free: later.
    static constexpr int offsets[9][2] = {{0, 0}, {8, 0}, {0, 8}, {-8, 0}, {0, -8}, {8, 8}, {-8, 8}, {8, -8}, {-8, -8}};
    const int floor = crowd_map_->floor(out->x >> 3, out->y >> 3);
    int to_x = -1, to_y = -1;
    for (const auto& o : offsets) {
        const int x = out->x + o[0], y = out->y + o[1];
        if (crowd_map_->floor(x >> 3, y >> 3) != floor) {
            continue;
        }
        const bool free = std::none_of(marbles.begin(), marbles.end(), [&](const Marble& other) {
            return &other != &m && other.status == 1 && std::abs((other.x >> 16) - x) < 10 && std::abs((other.y >> 16) - y) < 10;
        });
        if (free) {
            to_x = x;
            to_y = y;
            break;
        }
    }
    if (to_x < 0) {
        return;
    }
    m.x = to_x << 16;
    m.y = to_y << 16;
    if (out->zone != -99) {
        m.feature = std::int8_t(out->zone);
    }
    m.vx = m.vy = m.vz = 0;
    m.air = 0;
    m.region_code = 0;
    m.mode = Marble::rolling;
    probe_.invalidate();
    locate(m);
    m.z = m.fall_start_z = probe_.ground_height();
    project(m, true);
}

// The push a crowd marble's driver gives for this update: towards a cell a
// few steps down the distance to go in its zone, as fast as its pace (and
// slower where the way is narrow or about to drop), nudged loose when it
// gets no nearer for a while.
void Race::steer_bot(Marble& m) {
    // (By its slot: an empty slot's marble says player 0.)
    Push& push = push_[std::size_t(&m - marbles.data())];
    push = {};
    if (m.status != 1 || !crowd_map_) {
        return;
    }
    auto& d = drivers_[m.player];
    if (d.top_speed == 0) {
        d = new_driver(m.player); // marble 0 in the research tools
    }
    if (d.trial_left > 0) {
        // On a copy of the race: the push being tried.
        --d.trial_left;
        push = {d.trial_forward, d.trial_side};
        return;
    }
    const bool steerable = m.air == 0 && (m.mode == Marble::rolling || m.mode == Marble::landing || m.mode == Marble::bumped);
    // (A run-up looks like a fall to the plan: in the lane, flat out.)
    const bool running_up = crowd_map_->run_up(m.x >> 19, m.y >> 19).has_value();
    if (!planning_ && steerable) {
        const double progress = double(crowd_map_->rank(m.feature)) * 65536.0 +
                                crowd_map_->to_go(m.feature, m.x >> 19, m.y >> 19);
        if (progress < d.plan_best) {
            d.plan_best = progress;
            d.plan_stall = 0;
        } else if (++d.plan_stall > 200) {
            d.plan_stall = 0;
            d.bold = 90;
        }
    }
    if (d.bold > 0) {
        --d.bold;
    }
    if (!planning_ && steerable && !running_up && d.bold == 0) {
        if (--d.plan_left <= 0) {
            plan(m);
        }
        if (!d.plan_steer) {
            push = {d.plan_forward, d.plan_side};
            return;
        }
    }
    push = usual_push(m);
}

// Trying pushes ahead: on copies of the race the marble holds each of the
// eight directions (with and without fire), no push, or its usual
// steering for a few updates, then steers as usual; the one that gets
// nearest the goal by the end without losing the marble is its plan for
// the next updates.
void Race::plan(Marble& m) {
    constexpr int horizon = 20, held = 6, replan = 4;
    auto& d = drivers_[m.player];
    const CrowdMap& map = *crowd_map_;
    struct Candidate {
        bool steer;
        std::int16_t forward, side;
        bool dash{}; // held all the way (a dash across a drawbridge before it rises)
    };
    std::array<Candidate, 26> candidates{};
    candidates[0] = {true, 0, 0};
    candidates[1] = {false, 0, 0};
    static constexpr int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int k = 0; k < 24; ++k) {
        const int size = k < 8 ? 0x20 : 0x40;
        candidates[std::size_t(2 + k)] = {false, std::int16_t(dx[k % 8] * size), std::int16_t(-dy[k % 8] * size), k >= 16};
    }
    double best = -1e18;
    Candidate chosen = candidates[0];
    const int index = m.player;
    for (const auto& c : candidates) {
        Race copy = *this;
        copy.planning_ = true;
        copy.sounds_.clear();
        auto& cd = copy.drivers_[std::size_t(index)];
        cd.trial_left = c.steer ? 0 : c.dash ? horizon + 30 : held;
        cd.trial_forward = c.forward;
        cd.trial_side = c.side;
        double score = 0;
        bool lost = false;
        // (Past the horizon while in the air: on until it has come down.)
        for (int t = 0; t < horizon || (copy.marbles[std::size_t(index)].air != 0 && t < horizon + 30); ++t) {
            copy.update({copy.push_[0], copy.push_[1]});
            const Marble& cm = copy.marbles[std::size_t(index)];
            if (cm.status == 3 || cm.mode == Marble::finishing) {
                score = 1e9 - t;
                break;
            }
            if (map.run_up(cm.x >> 19, cm.y >> 19) || map.near_way_in(cm.x >> 19, cm.y >> 19)) {
                score = 1e8 - t; // into a run-up lane (its jump is no loss) or a funnel
                break;
            }
            // Falling far, or in the air over no floor at all.
            const bool falling = (cm.air == 2 && cm.fall_start_z - cm.z > 0x600000) ||
                                 (cm.air != 0 && map.floor(cm.x >> 19, cm.y >> 19) == 0);
            if (cm.status != 1 || cm.respawns != m.respawns || falling || cm.mode == Marble::breaking ||
                cm.mode == Marble::respawning || cm.mode == Marble::captured || cm.mode == Marble::dissolving ||
                cm.mode == Marble::eaten || cm.mode == Marble::frozen) {
                score = -1e9 + t * 1e6; // later is better
                lost = true;
                break;
            }
        }
        if (score == 0 && !lost) {
            const Marble& cm = copy.marbles[std::size_t(index)];
            const auto left = map.to_go(cm.feature, cm.x >> 19, cm.y >> 19);
            score = -(double(map.rank(cm.feature)) * 65536.0 + (left == CrowdMap::none ? 60000.0 : left));
        }
        if (c.steer) {
            score += 2; // the usual steering when nothing is better
        }
        if (score > best) {
            best = score;
            chosen = c;
        }
    }
    d.plan_left = replan;
    d.plan_steer = chosen.steer;
    d.plan_forward = chosen.forward;
    d.plan_side = chosen.side;
}

// The usual steering: towards a cell a few steps down the distance to go,
// at its pace (less where the way is narrow, braking before an edge).
Push Race::usual_push(Marble& m) {
    Push push{};
    auto& d = drivers_[m.player];
    auto dice = [&] {
        d.seed ^= d.seed << 13;
        d.seed ^= d.seed >> 17;
        d.seed ^= d.seed << 5;
        return d.seed;
    };
    const CrowdMap& map = *crowd_map_;
    const int px = m.x >> 16, py = m.y >> 16;
    const int cx = px >> 3, cy = py >> 3;
    const std::uint16_t here = map.to_go(m.feature, cx, cy);

    // Progress: a new zone starts over; a long time without getting nearer
    // gives a random push for a moment.
    if (d.best_zone != m.feature) {
        d.best_zone = m.feature;
        d.best = here;
        d.stuck = 0;
    } else if (here < d.best) {
        d.best = here;
        d.stuck = 0;
    } else if (m.mode == Marble::rolling && ++d.stuck > 150) {
        d.stuck = 0;
        d.wiggle = 20;
        const int dir = int(dice() % 8);
        static constexpr int wx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, wy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        d.wiggle_forward = std::int16_t(wx[dir] * 0x40);
        d.wiggle_side = std::int16_t(-wy[dir] * 0x40);
    }
    if (d.wiggle > 0) {
        --d.wiggle;
        push = {d.wiggle_forward, d.wiggle_side};
        return push;
    }
    if (m.mode != Marble::rolling && m.mode != Marble::landing && m.mode != Marble::bumped) {
        return push;
    }
    // A run-up: flat out with fire held, straight along the lane, from its
    // start or once fast; joined further down, back up to its start first.
    const auto lane = map.run_up(cx, cy);
    if (!lane) {
        d.running = false;
        d.backing = 0;
    } else {
        const double lx = lane->to_x - lane->from_x, ly = lane->to_y - lane->from_y;
        const double length = std::hypot(lx, ly);
        const double along_speed = (m.vx * lx + m.vy * ly) / length;
        // (Backing up for long without getting there: from here then.)
        if (lane->along > 1.5 && along_speed < 0x28000 && !d.running && ++d.backing < 150) {
            const double bx = lane->from_x - m.x / 65536.0, by = lane->from_y - m.y / 65536.0;
            const double size = std::max(std::abs(bx), std::abs(by));
            if (size > 0) {
                push.forward = std::int16_t(std::lround(bx / size * 0x20));
                push.side = std::int16_t(-std::lround(by / size * 0x20));
            }
            return push;
        }
        d.running = true;
        const double rx = lane->to_x - m.x / 65536.0, ry = lane->to_y - m.y / 65536.0;
        const double size = std::max(std::abs(rx), std::abs(ry));
        if (size > 0) {
            push.forward = std::int16_t(std::lround(rx / size * 0x40));
            push.side = std::int16_t(-std::lround(ry / size * 0x40));
        }
        return push;
    }

    // Where to: four steps down the distance (diagonal steps when both
    // straight ones are open); off the known ways, back to the nearest
    // cell on one.
    int ax = cx, ay = cy;
    std::uint16_t value = here;
    if (value == CrowdMap::none) {
        int best = 0x7FFF;
        for (int r = 1; r <= 3 && best == 0x7FFF; ++r) {
            for (int y = cy - r; y <= cy + r; ++y) {
                for (int x = cx - r; x <= cx + r; ++x) {
                    const auto v = map.to_go(m.feature, x, y);
                    if (v != CrowdMap::none && v < best) {
                        best = v;
                        ax = x;
                        ay = y;
                    }
                }
            }
        }
    } else {
        const int steps = map.room(cx, cy) >= 18 ? 4 : 2;
        for (int step = 0; step < steps; ++step) {
            int nx = ax, ny = ay;
            std::uint16_t nv = value;
            for (int dir = 0; dir < 4; ++dir) {
                static constexpr int sx[4] = {1, -1, 0, 0}, sy[4] = {0, 0, 1, -1};
                if (!map.can_move(ax, ay, dir)) {
                    continue;
                }
                const auto v = map.to_go(m.feature, ax + sx[dir], ay + sy[dir]);
                if (v < nv) {
                    nv = v;
                    nx = ax + sx[dir];
                    ny = ay + sy[dir];
                }
            }
            if (nx == ax && ny == ay) {
                break;
            }
            ax = nx;
            ay = ny;
            value = nv;
        }
    }
    const double tx = ax * 8 + 4 - (m.x / 65536.0), ty = ay * 8 + 4 - (m.y / 65536.0);
    const double distance = std::hypot(tx, ty);

    // How fast: its pace where there is room, under half of it on a narrow
    // way, and braking when where it is going in the next updates is not a
    // way it knows (an edge).
    const int room = map.room(cx, cy);
    double speed = d.top_speed * std::clamp((room - 6) / 14.0, 0.4, 1.0);
    if (here != CrowdMap::none && m.air == 0) {
        // Along its course for the next twelve updates: each cell it enters
        // must be one it can roll onto from the last.
        int lx = cx, ly = cy;
        for (int t = 1; t <= 12; ++t) {
            const int fx = (m.x + m.vx * t) >> 19, fy = (m.y + m.vy * t) >> 19;
            if (fx == lx && fy == ly) {
                continue;
            }
            const int dx = fx - lx, dy = fy - ly;
            auto step = [&](int x, int y, int sx, int sy) {
                const int dir = sx > 0 ? 0 : sx < 0 ? 1 : sy > 0 ? 2 : 3;
                return map.can_move(x, y, dir);
            };
            bool ok = std::abs(dx) <= 1 && std::abs(dy) <= 1;
            if (ok && dx != 0 && dy != 0) {
                ok = (step(lx, ly, dx, 0) && step(lx + dx, ly, 0, dy)) || (step(lx, ly, 0, dy) && step(lx, ly + dy, dx, 0));
            } else if (ok) {
                ok = step(lx, ly, dx, dy);
            }
            if (!ok) {
                speed *= t <= 4 ? 0.0 : t <= 8 ? 0.3 : 0.6;
                break;
            }
            // A drop onto a narrow way: arrive slowly, or roll off its far side.
            if (map.floor(lx, ly) - map.floor(fx, fy) > 6 && map.room(fx, fy) < 15) {
                speed *= 0.25;
                break;
            }
            lx = fx;
            ly = fy;
        }
    }
    double want_x = 0, want_y = 0;
    if (distance > 0.5) {
        want_x = tx / distance * speed;
        want_y = ty / distance * speed;
    }
    auto axis = [](double change) {
        const double size = std::abs(change);
        const int amount = size < 0x3000 ? 0 : size < 0x14000 ? 0x20 : 0x40;
        return std::int16_t(change < 0 ? -amount : amount);
    };
    push.forward = axis(want_x - m.vx);  // adds to vx
    push.side = std::int16_t(-axis(want_y - m.vy)); // subtracts from vy
    return push;
}

} // namespace marble_madness::game
