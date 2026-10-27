#include "mindwalker_amiga/landscape/view_scene.hpp"
#include <algorithm>
#include <vector>

namespace mindwalker::landscape {
namespace {

// A's data (research layout).
constexpr std::uint32_t grid_rows = 0x2058A8;                         // 18 rows of horizontal lines
constexpr std::uint32_t grid_top_x = 0x2058CC, grid_bottom_x = 0x2058E4, grid_bottom_y = 0x2058FC; // 12 each
constexpr int grid_horizon = 31;
constexpr std::uint32_t column_order = 0x202E88;                      // 4 longs
constexpr std::uint32_t row_top = 0x202E9C, row_base = 0x202EA4;      // words by row (base: +2 = next row's)
constexpr int grid_pen = 14;
constexpr int half_cell = 40;

struct Point {
    int x, y;
};

// Even-odd scanline fill of a polygon (pixel centres), plus its outline.
void fill(screen::Canvas& canvas, const std::vector<Point>& points, int pen, int outline = -1, int top = 0) {
    int y0 = 1 << 30, y1 = -(1 << 30);
    for (const auto& p : points) {
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    std::vector<int> xs;
    for (int y = y0; y <= y1; ++y) {
        xs.clear();
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto a = points[i], b = points[(i + 1) % points.size()];
            if (a.y == b.y) continue;
            const bool upward = a.y < b.y;
            const auto lo = upward ? a : b, hi = upward ? b : a;
            if (y < lo.y || y >= hi.y) continue;
            xs.push_back(lo.x + (y - lo.y) * (hi.x - lo.x) / (hi.y - lo.y));
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
            for (int x = xs[k]; x <= xs[k + 1]; ++x) canvas.plot(x, top + y, pen);
        }
    }
    if (outline >= 0) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto a = points[i], b = points[(i + 1) % points.size()];
            canvas.line(a.x, top + a.y, b.x, top + b.y, outline);
        }
    }
}

} // namespace

ViewPainter::ViewPainter(const amiga::LoadedImage& landscape) : a_(landscape) {}

// $20129A: the 4 x 4 tables in view order for a direction, each cell with its
// place in the 9 x 9 lattice (whose numbers double as pens) and its shade.
ViewPainter::Turned ViewPainter::turn(const ViewCells& view, int direction) const {
    Turned turned;
    const bool any_marked = view.landmarks != 0 || view.beacon || view.creature_places != 0 || view.find;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            int sx = 0, sy = 0, column = 0, row = 0;
            switch (direction & 3) {
            case 0: sy = c; column = c + 5; sx = r; row = r + 1; break;
            case 1: sy = 3 - r; sx = c; column = c + 1; row = 8 - r; break;
            case 2: sx = 3 - r; sy = 3 - c; column = 8 - c; row = 4 - r; break;
            default: sx = 3 - c; column = 4 - c; sy = r; row = r + 5; break;
            }
            int shade = sy + sx;
            if (view.water && shade == 6) shade = 5;
            if (view.marker_2000 && shade == 3) shade = 2;
            if (any_marked && shade == 4) shade = 5;
            turned[std::size_t(r)][std::size_t(c)] = {view.terrain[std::size_t(sx)][std::size_t(sy)],
                                                     view.objects[std::size_t(sx)][std::size_t(sy)], 0, column, row,
                                                     shade};
        }
    }
    return turned;
}

// $205218: the floor grid, 18 horizontal lines and 12 converging pairs.
void ViewPainter::paint_grid(screen::Canvas& canvas, int top) const {
    for (std::uint32_t i = 0; i < 18; ++i) {
        const int y = a_.s16(grid_rows + i * 2);
        canvas.line(0, top + y, 319, top + y, grid_pen);
    }
    for (std::uint32_t i = 0; i < 12; ++i) {
        const int x0 = a_.s16(grid_top_x + i * 2), x1 = a_.s16(grid_bottom_x + i * 2);
        const int y1 = std::min(a_.s16(grid_bottom_y + i * 2) + top, 199);
        canvas.line(x0, top + grid_horizon, x1, y1, grid_pen);
        if (i != 11) canvas.line(319 - x0, top + grid_horizon, 319 - x1, y1, grid_pen);
    }
}

void ViewPainter::paint(screen::Canvas& canvas, const ViewCells& view, int direction, int floor_level,
                        int ceiling_level, int top) const {
    paint_grid(canvas, top);
    const auto cells = turn(view, direction);
    for (int row = 3; row >= 0; --row) {
        const int row_top_y = a_.s16(row_top + std::uint32_t(row * 2));
        const int base_y = a_.s16(row_base + std::uint32_t(row * 2));
        const int next_base_y = a_.s16(row_base + 2 + std::uint32_t(row * 2));
        for (std::uint32_t k = 0; k < 4; ++k) {
            const int column = a_.s32(column_order + k * 4);
            const auto& cell = cells[std::size_t(column)][std::size_t(row)];
            const std::uint16_t t = cell.terrain, kind = t & 0xC000;
            const bool marked = (t & 0x100) != 0;
            int level = t & 0xFF;
            int pen = 0, body_pen = cell.lattice_row, lattice_pen = cell.lattice_column, outline_pen = 0;
            if (!marked) {
                if (cell.objects & 2) {
                    pen = 13;
                } else if (kind == 0xC000) {
                    pen = 15;
                    level = floor_level;
                } else {
                    pen = kind == 0x8000 ? cell.lattice_column : cell.shade + 9;
                }
            } else {
                outline_pen = cell.shade + 9;
                body_pen = lattice_pen = 0;
                level = std::min(level, ceiling_level);
            }
            const int x = FixedCamera::cell_x(column), z = FixedCamera::cell_z(row);
            const auto corner = [&](int dx, int dz) {
                const auto p = camera_.project(x + dx, level, z + dz);
                return Point{p.x, p.y};
            };
            // Far left, far right, near right, near left ($202606).
            const std::vector<Point> face = {corner(-half_cell, half_cell), corner(half_cell, half_cell),
                                             corner(half_cell, -half_cell), corner(-half_cell, -half_cell)};
            const auto centre = camera_.project(x, level, z);
            if (kind == 0x4000) {
                // A pedestal under a platform, narrowing towards the back.
                const int w = 5 - row;
                fill(canvas,
                     {{centre.x - w, centre.y}, {centre.x + w, centre.y}, {centre.x + 2 * w, row_top_y},
                      {centre.x - 2 * w, row_top_y}},
                     lattice_pen, marked ? outline_pen : -1, top);
            } else if (kind == 0x8000) {
                // A pillar: lines from the corners up out of the picture.
                for (const auto& p : face) canvas.line(p.x, top + p.y, p.x, top, marked ? outline_pen : body_pen);
            } else {
                // Ground or water: a box down to the row's base line.
                canvas.fill(face[3].x, top + face[3].y, face[2].x, top + base_y, body_pen);
                if (marked) {
                    const Point box[] = {{face[3].x, base_y}, face[3], face[2], {face[2].x, base_y}};
                    for (int i = 0; i < 4; ++i)
                        canvas.line(box[i].x, top + box[i].y, box[(i + 1) % 4].x, top + box[(i + 1) % 4].y, outline_pen);
                }
            }
            int face_pen = pen;
            if ((t & 0x1000) && kind != 0xC000) face_pen = 9;
            const bool outlined = (t & 0x400) || kind == 0x4000 || marked;
            fill(canvas, face, face_pen, outlined ? (marked ? outline_pen : 0) : -1, top);
            if (cell.objects & 1) {
                // A visited cell's marker ($205158): a smaller square in pen 0.
                const auto small = [&](int dx, int dz) {
                    const auto p = camera_.project(x + dx, level, z + dz);
                    return Point{p.x, p.y};
                };
                fill(canvas, {small(-20, 16), small(20, 16), small(20, -16), small(-20, -16)}, 0, -1, top);
            }
            if (kind == 0 || kind == 0xC000) {
                // The outer columns' side faces.
                if (column == 3) {
                    fill(canvas, {face[0], face[3], {face[3].x, base_y}, {face[0].x, next_base_y}}, lattice_pen,
                         marked ? outline_pen : -1, top);
                } else if (column == 0) {
                    fill(canvas, {face[2], face[1], {face[1].x, next_base_y}, {face[2].x, base_y}}, lattice_pen,
                         marked ? outline_pen : -1, top);
                }
            }
            if (t & 0x1000) {
                // A pyramid on the cell (pens 4, 3 and 1).
                const int peak_level = std::max(level - 0x46, 0x19);
                const auto near = camera_.project(x, peak_level, z - half_cell);
                const auto far = camera_.project(x, peak_level, z + half_cell);
                const auto apex = camera_.project(x, peak_level - 0x19, z);
                fill(canvas, {{face[0].x, far.y}, {face[3].x, near.y}, {face[2].x, near.y}, {face[1].x, far.y},
                              {apex.x, apex.y}}, 4, 0, top);
                fill(canvas, {{face[3].x, near.y}, {apex.x, apex.y}, {face[2].x, near.y}}, 3, -1, top);
                const int mid = (near.y + apex.y) >> 1;
                fill(canvas, {{(face[2].x + face[3].x) >> 1, near.y}, {(apex.x + face[3].x) >> 1, mid},
                              {(apex.x + face[2].x) >> 1, mid}}, 1, -1, top);
            }
        }
    }
}

} // namespace mindwalker::landscape
