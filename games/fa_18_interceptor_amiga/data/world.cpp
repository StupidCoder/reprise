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
                map.objects.push_back({(type & 0x7F) + first_scenery_type,
                                       (column * 16384 + (3 - slot % 4) * 4096 + ox) * scale,
                                       (row * 16384 + (3 - slot / 4) * 4096 + oz) * scale});
            }
        }
    }
    return map;
}

// A cell's polygons: [threshold | $8000] count, count x (x, z), until $FFFF.
void read_water_cell(const amiga::LoadedImage& m, std::uint32_t at, double x0, double z0,
                     std::vector<WaterPolygon>& out) {
    const double unit = 65536 / water_cell_points; // world units per point unit
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

WaterGrid read_water(const amiga::LoadedImage& m) {
    WaterGrid grid;
    grid.cells = water_cells;
    grid.cell_size = 65536;
    for (int z = 0; z < water_cells; ++z) {
        for (int x = 0; x < water_cells; ++x) {
            const auto offset = m.s16(water_grid + std::uint32_t(z * water_row_bytes + 2 * x));
            if (offset <= 0) continue;
            const std::uint32_t cell = water_grid + std::uint32_t(offset);
            // $11F5B6: a negative first word marks a cell without water (an
            // error trap in normal play; with $13AAA0 set its long ends it).
            if (m.s16(cell) < 0) continue;
            const double x0 = x * grid.cell_size, z0 = z * grid.cell_size;
            read_water_cell(m, cell + 4, x0, z0, grid.polygons);
            const auto far = m.s32(cell);
            if (far > 0) read_water_cell(m, std::uint32_t(far), x0, z0, grid.far_polygons);
        }
    }
    return grid;
}

} // namespace

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
    return world;
}

} // namespace interceptor::data
