#include "fa_18_interceptor_amiga/data/world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace interceptor::data {
namespace {

constexpr std::uint32_t map_a = 0x137584, map_b = 0x137684, map_near = 0x137784;
// The maps number scenery from 16: types 0-15 are the vehicles' shapes and
// 16-31 the sixteen moving objects' own records (the visible lists show
// map type + 16, harness/listsnap.py).
constexpr int first_scenery_type = 16;
constexpr std::uint32_t position_codes = 0x110806; // signed byte pairs for coded positions
constexpr std::uint32_t water_grid = 0x138160;      // fine grid, 32 x 32 words
constexpr int water_cells = 32;
constexpr int water_row_bytes = 64;
constexpr double water_cell_points = 4096;           // point units per cell ($11F6CE)

// $110344 with the placement of $110B90: a map is 128 word offsets, one
// per row (the z cell); a row holds 2n, n sorted x cells, then n long
// pointers to object lists. An object list: [position code << 4 | slot]
// [type, bit 7: position from the code], then x and z words unless coded;
// $FF ends. The slot picks a 4 x 4 grid counted from the cell's far corner.
ObjectMap read_map(const amiga::LoadedImage& m, std::uint32_t table, bool coarse) {
    ObjectMap map;
    map.cells = coarse ? 32 : 128;
    const double scale = coarse ? 4 : 1;      // coarse maps: cells, slots and positions x 4
    map.cell_size = 16384 * scale;
    for (int row = 0; row < 128; ++row) {
        const auto offset = m.s16(table + std::uint32_t(2 * row));
        if (offset < 0) continue;
        const std::uint32_t at = table + std::uint32_t(offset);
        const int count = m.s16(at) / 2;
        if (count <= 0) continue;
        const std::uint32_t pointers = at + m.u16(at) + 2;
        for (int i = 0; i < count; ++i) {
            const int column = m.s16(at + 2 + std::uint32_t(2 * i));
            std::uint32_t p = m.u32(pointers + std::uint32_t(4 * i));
            for (int guard = 0; guard < 64; ++guard) {
                const int header = m.u8(p++);
                if (header == 0xFF) break;
                const int type = m.u8(p++);
                double ox, oz;
                if (type & 0x80) {
                    ox = m.s8(position_codes + std::uint32_t(2 * (header >> 4)));
                    oz = m.s8(position_codes + std::uint32_t(2 * (header >> 4) + 1));
                } else {
                    ox = m.s16(p);
                    oz = m.s16(p + 2);
                    p += 4;
                }
                const int slot = header & 15;
                map.by_cell[row << 8 | column][std::size_t(slot)].push_back(map.objects.size());
                map.objects.push_back({(type & 0x7F) + first_scenery_type,
                                       (column * 16384 + (3 - slot % 4) * 4096 + ox) * scale,
                                       (row * 16384 + (3 - slot / 4) * 4096 + oz) * scale, column, row, slot});
            }
        }
    }
    return map;
}

// A cell's polygons: [threshold | $8000] count, count x (x, z), until $FFFF.
void read_water_cell(const amiga::LoadedImage& m, std::uint32_t at, double x0, double z0,
                     std::vector<WaterPolygon>& out, double cell_size = 65536) {
    const double unit = cell_size / water_cell_points; // world units per point unit
    std::optional<int> min_height; // a threshold holds for the rest of the cell ($11F73C ends the cell)
    for (int guard = 0; guard < 64; ++guard) {
        int count = m.u16(at);
        at += 2;
        if (count == 0xFFFF) break;
        WaterPolygon polygon;
        if (count & 0x8000) {
            const int threshold = (count & 0x7FFF) * 4;
            min_height = std::max(min_height.value_or(0), threshold);
            count = m.u16(at);
            at += 2;
        }
        polygon.min_height = min_height;
        for (int i = 0; i < count; ++i) {
            polygon.points.push_back({x0 + m.s16(at) * unit, z0 + m.s16(at + 2) * unit});
            at += 4;
        }
        out.push_back(std::move(polygon));
    }
}

constexpr std::uint32_t coarse_water_grid = 0x137F9C; // 8 x 8 words
constexpr std::uint32_t water_offsets = 0x11E6E0;     // (x, z) byte pairs
constexpr std::uint32_t coarse_windows = 0x11E712;    // 16 lists of 16 bytes
constexpr std::uint32_t coarse_low_windows = 0x11E812; // 16 lists of 4 bytes
constexpr std::uint32_t window_5x5 = 0x11E8A2, window_3x3 = 0x11E852;
// The low windows by height band: 16 quarters x 8 eighths of 4, 6 or 8 bytes.
constexpr std::uint32_t low_windows[3] = {0x11EFBC, 0x11ECBC, 0x11E8BC};
constexpr int low_window_bytes[3] = {4, 6, 8};
constexpr std::uint32_t own_only_tables[3] = {0x11F1BC, 0x11F1FC, 0x11F23C};
constexpr std::uint32_t far_reach_table = 0x11F5D8;
constexpr std::uint32_t own_far_table = 0x113418;

// A window list: offset indices, the last with bit 7 (or $FF ends it).
std::vector<std::array<int, 2>> read_window(const amiga::LoadedImage& m, std::uint32_t at) {
    std::vector<std::array<int, 2>> out;
    for (int i = 0; i < 32; ++i) {
        const int b = m.u8(at + std::uint32_t(i));
        if (b == 0xFF) break;
        const int index = b & 0x7F;
        out.push_back({m.s8(water_offsets + std::uint32_t(2 * index)), m.s8(water_offsets + std::uint32_t(2 * index + 1))});
        if (b & 0x80) break;
    }
    return out;
}

CoarseWater read_coarse_water(const amiga::LoadedImage& m) {
    CoarseWater w;
    for (int z = 0; z < CoarseWater::cells; ++z) {
        for (int x = 0; x < CoarseWater::cells; ++x) {
            const auto offset = m.s16(coarse_water_grid + std::uint32_t(2 * (z * CoarseWater::cells + x)));
            if (offset <= 0) continue;
            const std::uint32_t cell = coarse_water_grid + std::uint32_t(offset);
            if (m.s16(cell) < 0) continue;
            const double x0 = (x - CoarseWater::origin) * CoarseWater::cell_size;
            const double z0 = (z - CoarseWater::origin) * CoarseWater::cell_size;
            read_water_cell(m, cell + 4, x0, z0, w.polygons, CoarseWater::cell_size);
            w.cell_of.resize(w.polygons.size(), {x, z});
        }
    }
    for (std::size_t k = 0; k < w.windows.size(); ++k) {
        w.windows[k] = read_window(m, coarse_windows + std::uint32_t(16 * k));
        w.low_windows[k] = read_window(m, coarse_low_windows + std::uint32_t(4 * k));
    }
    return w;
}

// $113158 walks this table: a list of z cells (a negative word ends it)
// and a word offset per entry to a list of x cells, likewise to the
// cell's rules: a word (0: the height at most, else at least) and a long
// height, then quarters - a byte and that quarter's eighths, each list
// ended by a negative byte - with a negative byte at the end, padded to
// a word; a negative word ends the rules.
std::vector<WaterWindows::OwnFar> read_own_far(const amiga::LoadedImage& m) {
    std::vector<WaterWindows::OwnFar> out;
    const auto list = [&](std::uint32_t at) {
        std::vector<std::pair<int, std::uint32_t>> entries;
        std::uint32_t p = at;
        while (m.s16(p) >= 0) p += 2;
        const std::uint32_t offsets = p + 2;
        for (std::uint32_t i = 0; at + 2 * i < p; ++i)
            entries.push_back({m.s16(at + 2 * i), own_far_table + std::uint32_t(m.s16(offsets + 2 * i))});
        return entries;
    };
    for (const auto& [z, xs] : list(own_far_table)) {
        for (const auto& [x, at] : list(xs)) {
            WaterWindows::OwnFar cell;
            cell.x = x;
            cell.z = z;
            std::uint32_t p = at;
            for (int guard = 0; guard < 64 && m.s16(p) >= 0; ++guard) {
                WaterWindows::OwnFar::Rule rule;
                rule.at_least = m.u16(p) != 0;
                rule.height = m.s32(p + 2);
                p += 6;
                for (int q = m.s8(p++); q >= 0; q = m.s8(p++)) {
                    std::vector<int> eighths;
                    for (int e = m.s8(p++); e >= 0; e = m.s8(p++)) eighths.push_back(e);
                    rule.quarters.push_back({q, eighths});
                }
                p = (p + 1) & ~1u;
                cell.rules.push_back(std::move(rule));
            }
            out.push_back(std::move(cell));
        }
    }
    return out;
}

WaterWindows read_water_windows(const amiga::LoadedImage& m) {
    WaterWindows w;
    w.wide = read_window(m, window_5x5);
    w.middle = read_window(m, window_3x3);
    for (int band = 0; band < 3; ++band) {
        for (int quarter = 0; quarter < 16; ++quarter) {
            for (int eighth = 0; eighth < 8; ++eighth) {
                const auto at = low_windows[band] +
                                std::uint32_t(low_window_bytes[band] * (8 * quarter + eighth));
                w.low[std::size_t(band)][std::size_t(quarter)][std::size_t(eighth)] = read_window(m, at);
            }
            for (int k = 0; k < 4; ++k)
                w.own_only[std::size_t(band)][std::size_t(quarter)][std::size_t(k)] =
                    m.s8(own_only_tables[band] + std::uint32_t(4 * quarter + k));
        }
    }
    for (std::size_t i = 0; i < w.far_reach.size(); ++i) w.far_reach[i] = m.u16(far_reach_table + std::uint32_t(2 * i));
    w.own_far = read_own_far(m);
    return w;
}

WaterGrid read_water(const amiga::LoadedImage& m) {
    WaterGrid grid;
    grid.cells = water_cells;
    grid.cell_size = 65536;
    grid.by_cell.resize(std::size_t(water_cells * water_cells));
    for (int z = 0; z < water_cells; ++z) {
        for (int x = 0; x < water_cells; ++x) {
            const auto offset = m.s16(water_grid + std::uint32_t(z * water_row_bytes + 2 * x));
            if (offset <= 0) continue;
            const std::uint32_t cell = water_grid + std::uint32_t(offset);
            // $11F5B6: a negative first word marks a cell without water (an
            // error trap in normal play; with $13AAA0 set its long ends it).
            if (m.s16(cell) < 0) continue;
            const double x0 = x * grid.cell_size, z0 = z * grid.cell_size;
            auto& c = grid.by_cell[std::size_t(z * water_cells + x)];
            c.water = true;
            c.first = grid.polygons.size();
            read_water_cell(m, cell + 4, x0, z0, grid.polygons);
            c.count = grid.polygons.size() - c.first;
            grid.cell_of.resize(grid.polygons.size(), {x, z});
            c.far_first = grid.far_polygons.size();
            const auto far = m.s32(cell);
            if (far > 0) read_water_cell(m, std::uint32_t(far), x0, z0, grid.far_polygons);
            c.far_count = grid.far_polygons.size() - c.far_first;
        }
    }
    return grid;
}

} // namespace

const std::array<std::vector<std::size_t>, 16>* ObjectMap::slots(int column, int row) const {
    const auto at = by_cell.find(row << 8 | column);
    return at == by_cell.end() ? nullptr : &at->second;
}

namespace {

// $137D8A: 16-byte records [map type x 20, coarse x, coarse z, slot, x, z],
// $FFFF ends; the slot picks an offset pair at $110732; positions are in
// coarse-map units (x 4). The footprint list is the type record's alternate
// list ($11608C = the record of type 16, + map type x 20, + $10): quads of
// four (x, z) words, $FFFF ends.
std::vector<Runway> read_runways(const amiga::LoadedImage& m) {
    std::vector<Runway> runways;
    for (std::uint32_t at = 0x137D8A;; at += 16) {
        const int offset = m.s16(at);
        if (offset == -1) break;
        if (offset < 0) continue;
        const int slot = m.s16(at + 6);
        const std::uint32_t pair = 0x110732 + std::uint32_t(4 * slot);
        Runway r;
        r.type = offset / 20 + first_scenery_type;
        r.x = m.s16(at + 2) * 65536.0 + (m.s16(pair) + m.s16(at + 8)) * 4.0;
        r.z = m.s16(at + 4) * 65536.0 + (m.s16(pair + 2) + m.s16(at + 10)) * 4.0;
        for (std::uint32_t q = m.u32(0x11608C + std::uint32_t(offset) + 0x10); m.s16(q) != -1; q += 16) {
            std::array<std::array<double, 2>, 4> quad{};
            for (std::uint32_t k = 0; k < 4; ++k) quad[k] = {double(m.s16(q + 4 * k)), double(m.s16(q + 4 * k + 2))};
            r.quads.push_back(quad);
        }
        runways.push_back(std::move(r));
    }
    return runways;
}

bool inside(const std::vector<std::array<double, 2>>& points, double x, double z) {
    bool in = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const auto& a = points[i];
        const auto& b = points[j];
        if ((a[1] > z) != (b[1] > z) && x < (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]) + a[0]) in = !in;
    }
    return in;
}

} // namespace

// $113158: the first rule whose height test fails ends the search; one
// that passes looks for the quarter, then the eighth.
bool WaterWindows::own_cell_far(int x, int z, std::int32_t height, int quarter, int eighth) const {
    for (const auto& cell : own_far) {
        if (cell.x != x || cell.z != z) continue;
        for (const auto& rule : cell.rules) {
            if (rule.at_least ? height < rule.height : height > rule.height) return false;
            for (const auto& [q, eighths] : rule.quarters) {
                if (q == quarter && std::find(eighths.begin(), eighths.end(), eighth) != eighths.end()) return true;
            }
        }
        return false;
    }
    return false;
}

// The water test ($11F83A, first part): inside any of the cell's polygons.
bool World::over_water(double x, double z) const {
    for (const auto& polygon : water.polygons) {
        if (polygon.points.size() >= 3 && inside(polygon.points, x, z)) return true;
    }
    return false;
}

// The runway test ($11F83A, second part): every edge of a quad has the
// point on its inner side.
bool World::on_runway(double x, double z) const {
    for (const auto& r : runways) {
        if (std::abs(x - r.x) > 0x1800 || std::abs(z - r.z) > 0x1800 * 256.0) continue;
        for (const auto& q : r.quads) {
            bool in = true;
            for (std::size_t i = 0; i < 4 && in; ++i) {
                const auto& p = q[i];
                const auto& n = q[(i + 1) % 4];
                const double ex = n[0] - p[0], ez = -(n[1] - p[1]);
                const double px = p[0] + r.x - x, pz = p[1] + r.z - z;
                in = px * ez + pz * ex >= 0;
            }
            if (in) return true;
        }
    }
    return false;
}

World read_world(const amiga::LoadedImage& program) {
    World world;
    world.runways = read_runways(program);
    world.pass_a = read_map(program, map_a, true);
    world.pass_b = read_map(program, map_b, true);
    world.near = read_map(program, map_near, false);
    world.water = read_water(program);
    world.coarse_water = read_coarse_water(program);
    world.water_windows = read_water_windows(program);
    return world;
}

} // namespace interceptor::data
