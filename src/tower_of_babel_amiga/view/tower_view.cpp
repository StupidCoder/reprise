#include "tower_of_babel_amiga/view/tower_view.hpp"
#include "tower_of_babel_amiga/view/polygon.hpp"
#include <span>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace babel::view {
namespace {

using namespace game::cell;

constexpr int cell_size = 0x200;
constexpr int slab = 0x20;       // floor thickness
constexpr int half_span = 0x800; // the tower runs from -half_span to +half_span
constexpr int max_rows = 9;      // rows of cell edges ($14D6 is at most 9)

// The pens' places in Pens::pens.
enum PenIndex { under = 0, side = 1, front = 2, top = 4, second_set = 6, shadow = 13, lift_top = 14 };

// Projected cell edges for each row of depth ($CDE6): 9 x edges, and the y
// of each level's top and its slab's underside.
struct Projection {
    int rows{};
    std::array<std::array<int, 9>, max_rows> x{};
    std::array<std::array<int, 5>, max_rows> top{}, under{};
};

// A 68000 DIVS of a shifted 16-bit value by a positive 16-bit depth.
int project(int offset, int depth) { return int(std::int32_t(std::int16_t(offset)) * 256 / depth); }

Projection project_rows(const Camera& cam, int cx, int cy) {
    Projection p;
    // Rows of edges in front of the camera (asr: arithmetic), at most 9.
    const int ahead = std::int16_t(half_span - cam.z + 0x80) >> 9;
    p.rows = std::min(ahead, 8) + 1;
    int depth = std::int16_t(half_span - cam.z);
    for (int k = 0; k < p.rows && k < max_rows; ++k) {
        for (int i = 0; i < 9; ++i) p.x[std::size_t(k)][std::size_t(i)] = project(-half_span - cam.x + i * cell_size, depth) + cx;
        for (int l = 0; l < 5; ++l) {
            const int y = l * cell_size - cam.y;
            p.top[std::size_t(k)][std::size_t(l)] = cy - project(y, depth);
            p.under[std::size_t(k)][std::size_t(l)] = cy - project(y - slab, depth);
        }
        depth -= cell_size;
        if (depth < 0x100) depth = 0x100;
    }
    return p;
}

// The tower cell seen at view column c (0 = left), view row v (7 = far).
struct Turn {
    int heading;
    void cell(int c, int v, int& x, int& z) const {
        switch (heading & 3) {
        case 0: x = c; z = v; break;
        case 1: x = v; z = 7 - c; break;
        case 2: x = 7 - c; z = 7 - v; break;
        default: x = 7 - v; z = c; break;
        }
    }
};

class Frame {
public:
    Frame(Canvas& canvas, const Window& window, const Pens& pens, const game::TowerMap& map, const Camera& cam, int cx,
          int cy, const LiftHeight& lift_height)
        : canvas_(canvas), window_(window), pens_(pens), map_(map), cam_(cam), cx_(cx), cy_(cy), turn_{cam.heading},
          lift_height_(lift_height), p_(project_rows(cam, cx, cy)) {}

    std::uint8_t at(int f, int c, int v) const {
        if (c < 0 || c > 7 || v < 0 || v > 7) return 0;
        int x, z;
        turn_.cell(c, v, x, z);
        return map_.at(f, x, z);
    }
    bool ground_lit(int c, int v) const {
        int x, z;
        turn_.cell(c, v, x, z);
        return map_.ground_lit(x, z);
    }

    const Pen& pen(int index, std::uint8_t cell, bool odd_heading_shift) const {
        if (cell & second) index += second_set;
        if (odd_heading_shift && (cam_.heading & 1)) index += 1;
        return pens_.pens[std::size_t(index)];
    }

    void polygon(std::vector<Point> points, const Pen& pen) { fill_polygon(canvas_, window_, std::move(points), pen); }

    // $D37A: shadows on the ground under columns that light does not reach.
    void ground_shadows() {
        for (int k = 0; k + 1 < p_.rows; ++k) {
            const int v = 7 - k;
            for (int c = 0; c < 8; ++c) {
                if (ground_lit(c, v) || (at(0, c, v) & solid)) continue;
                if (p_.under[std::size_t(k)][0] > window_.bottom) continue;
                if (x(k, c + 1) < window_.left || x(k, c) > window_.right) continue;
                polygon({{x(k, c), u(k, 0)}, {x(k, c + 1), u(k, 0)}, {x(k + 1, c + 1), u(k + 1, 0)}, {x(k + 1, c), u(k + 1, 0)}},
                        pens_.pens[shadow]);
            }
        }
    }

    void cells() {
        const int column = std::int16_t(cam_.x) >> 9;
        const int right_cells = std::min(2 - column, 7); // $14DC: columns 7 downwards
        const int left_cells = std::min(column + 4, 7);  // $14DA: columns 0 upwards
        unsigned eye = unsigned(std::uint16_t(cam_.y + 0x20)) >> 9; // $14D8 (unsigned shift and test)
        if (eye >= 4) eye = 3;
        for (int k = 0; k + 1 < p_.rows; ++k) {
            const int v = 7 - k;
            for (int f = 3; f > int(eye); --f) { // above eye height
                if (u(k, f) < window_.top) continue;
                for (int i = 0, c = 7; i <= right_cells; ++i, --c) {
                    const auto cell = at(f, c, v);
                    if (!(cell & solid) || x(k, c) > window_.right) continue;
                    if (cell & lift) { lift_above(k, c, v, f, cell, true); continue; }
                    left_face(k, c, v, f, cell);
                    front_face(k, c, v, f, cell);
                    under_face(k, c, v, f, cell);
                }
                for (int i = 0, c = 0; i <= left_cells; ++i, ++c) {
                    const auto cell = at(f, c, v);
                    if (!(cell & solid) || x(k, c + 1) < window_.left) continue;
                    if (cell & lift) { lift_above(k, c, v, f, cell, false); continue; }
                    right_face(k, c, v, f, cell);
                    front_face(k, c, v, f, cell);
                    under_face(k, c, v, f, cell);
                }
            }
            for (int f = 0; f <= int(eye); ++f) { // at and below eye height
                if (t(k, f + 1) > window_.bottom) continue;
                for (int i = 0, c = 7; i <= right_cells; ++i, --c) {
                    const auto cell = at(f, c, v);
                    if (!(cell & solid) || x(k, c) > window_.right) continue;
                    if (cell & lift) { lift_below(k, c, v, f, cell, true); continue; }
                    left_face(k, c, v, f, cell);
                    front_face(k, c, v, f, cell);
                    top_face(k, c, f, cell);
                }
                for (int i = 0, c = 0; i <= left_cells; ++i, ++c) {
                    const auto cell = at(f, c, v);
                    if (!(cell & solid) || x(k, c + 1) < window_.left) continue;
                    if (cell & lift) { lift_below(k, c, v, f, cell, false); continue; }
                    right_face(k, c, v, f, cell);
                    front_face(k, c, v, f, cell);
                    top_face(k, c, f, cell);
                }
            }
        }
    }

private:
    // A lift cell's platform ($DCD8): its screen y at the cell's far and
    // near edge, and whether its top shows (the platform is below the eye).
    struct Platform {
        bool drawn{true}, top{false};
        int far{}, near{};
    };

    Platform platform(int k, int c, int v, int f, std::uint8_t cell) const {
        Platform p;
        const int view_y = f * cell_size - cam_.y; // $FA8 for this level
        const auto object_height = [&]() -> std::optional<int> {
            if (!lift_height_) return std::nullopt;
            int x, z;
            turn_.cell(c, v, x, z);
            return lift_height_(f, x, z);
        };
        // From the lift object's height: projected at the cell's near and far
        // edge, the cell's centre depth -/+ 256 (at least 256).
        const auto from_object = [&](int y) {
            if (y - cam_.y < 0) p.top = true;
            const int centre = 0x700 - cam_.z - k * cell_size; // $FAA
            const int dy = std::int16_t(y - cam_.y);
            p.near = cy_ - project(dy, std::max(centre - 0x100, 0x100));
            p.far = cy_ - project(dy, std::max(centre + 0x100, 0x100));
        };
        if (cell & lift_open) {
            if (cell & opaque) {
                p.near = t(k + 1, f);
                p.far = t(k, f);
                p.top = view_y < 0;
            } else if (const auto y = object_height(); !y || ((*y & 0x1FF) + 0x20) < 0x200) {
                p.drawn = false;
            } else {
                from_object(*y);
            }
        } else if (cell & full_height) {
            p.near = u(k + 1, f + 1);
            p.far = u(k, f + 1);
        } else if (const auto y = object_height()) {
            from_object(*y);
        } else {
            p.near = t(k + 1, f);
            p.far = t(k, f);
            p.top = view_y < 0;
        }
        return p;
    }

    // Lift faces: sides ($DE28 / $DEAC), front ($DF32), underside ($DF80), top ($DFDC).
    void lift_side(int k, int c, int v, int f, std::uint8_t cell, const Platform& p, bool right_of_centre) {
        if (right_of_centre) {
            if (x(k + 1, c) <= cx_ || (at(f, c - 1, v) & full_height)) return;
            polygon({{x(k, c), p.far}, {x(k + 1, c), p.near}, {x(k + 1, c), u(k + 1, f)}, {x(k, c), u(k, f)}}, pen(side, cell, true));
        } else {
            if (x(k + 1, c + 1) >= cx_ || (at(f, c + 1, v) & full_height)) return;
            polygon({{x(k, c + 1), p.far}, {x(k, c + 1), u(k, f)}, {x(k + 1, c + 1), u(k + 1, f)}, {x(k + 1, c + 1), p.near}},
                    pen(side, cell, true));
        }
    }
    void lift_front(int k, int c, int v, int f, std::uint8_t cell, const Platform& p) {
        if (at(f, c, v - 1) & full_height) return;
        fill_rectangle(canvas_, window_, x(k + 1, c), p.near, x(k + 1, c + 1), u(k + 1, f), pen(front, cell, true));
    }
    void lift_under(int k, int c, int f, std::uint8_t cell) {
        polygon({{x(k + 1, c), u(k + 1, f)}, {x(k + 1, c + 1), u(k + 1, f)}, {x(k, c + 1), u(k, f)}, {x(k, c), u(k, f)}},
                pen(under, cell, false));
    }
    void lift_top_face(int k, int c, std::uint8_t cell, const Platform& p) {
        polygon({{x(k, c), p.far}, {x(k, c + 1), p.far}, {x(k + 1, c + 1), p.near}, {x(k + 1, c), p.near}},
                pens_.pens[std::size_t(lift_top + ((cell & lit) ? 1 : 0))]);
    }

    void lift_above(int k, int c, int v, int f, std::uint8_t cell, bool right_of_centre) {
        const auto p = platform(k, c, v, f, cell);
        if (!p.drawn) return;
        lift_side(k, c, v, f, cell, p, right_of_centre);
        lift_front(k, c, v, f, cell, p);
        lift_under(k, c, f, cell);
    }
    void lift_below(int k, int c, int v, int f, std::uint8_t cell, bool right_of_centre) {
        const auto p = platform(k, c, v, f, cell);
        if (!p.drawn) return;
        lift_side(k, c, v, f, cell, p, right_of_centre);
        lift_front(k, c, v, f, cell, p);
        if (p.top) lift_top_face(k, c, cell, p);
    }

    int x(int k, int i) const { return p_.x[std::size_t(k)][std::size_t(i)]; }
    int t(int k, int l) const { return p_.top[std::size_t(k)][std::size_t(l)]; }
    int u(int k, int l) const { return p_.under[std::size_t(k)][std::size_t(l)]; }

    // $D8E0: the left side of a cell right of the centre.
    void left_face(int k, int c, int v, int f, std::uint8_t cell) {
        if (x(k + 1, c) <= cx_) return;
        const auto neighbour = at(f, c - 1, v);
        if (cell & full_height) {
            if (neighbour & full_height) return;
            polygon({{x(k, c), u(k, f + 1)}, {x(k + 1, c), u(k + 1, f + 1)}, {x(k + 1, c), u(k + 1, f)}, {x(k, c), u(k, f)}},
                    pen(side, cell, true));
        } else {
            if (neighbour & opaque) return;
            polygon({{x(k, c), t(k, f)}, {x(k + 1, c), t(k + 1, f)}, {x(k + 1, c), u(k + 1, f)}, {x(k, c), u(k, f)}},
                    pen(side, cell, true));
        }
    }

    // $D96A: the right side of a cell left of the centre.
    void right_face(int k, int c, int v, int f, std::uint8_t cell) {
        if (x(k + 1, c + 1) >= cx_) return;
        const auto neighbour = at(f, c + 1, v);
        if (cell & full_height) {
            if (neighbour & full_height) return;
            polygon({{x(k, c + 1), u(k, f + 1)}, {x(k, c + 1), u(k, f)}, {x(k + 1, c + 1), u(k + 1, f)},
                     {x(k + 1, c + 1), u(k + 1, f + 1)}},
                    pen(side, cell, true));
        } else {
            if (neighbour & opaque) return;
            polygon({{x(k, c + 1), t(k, f)}, {x(k, c + 1), u(k, f)}, {x(k + 1, c + 1), u(k + 1, f)},
                     {x(k + 1, c + 1), t(k + 1, f)}},
                    pen(side, cell, true));
        }
    }

    // $D9F6: the near side, which faces the camera (a rectangle).
    void front_face(int k, int c, int v, int f, std::uint8_t cell) {
        const auto nearer = at(f, c, v - 1);
        if (cell & full_height) {
            if (nearer & full_height) return;
            fill_rectangle(canvas_, window_, x(k + 1, c), u(k + 1, f + 1), x(k + 1, c + 1), u(k + 1, f), pen(front, cell, true));
        } else {
            if (nearer & opaque) return;
            fill_rectangle(canvas_, window_, x(k + 1, c), t(k + 1, f), x(k + 1, c + 1), u(k + 1, f), pen(front, cell, true));
        }
    }

    // $DA4C: the underside of a slab above eye height.
    void under_face(int k, int c, int v, int f, std::uint8_t cell) {
        if (at(f - 1, c, v) & full_height) return;
        polygon({{x(k + 1, c), u(k + 1, f)}, {x(k + 1, c + 1), u(k + 1, f)}, {x(k, c + 1), u(k, f)}, {x(k, c), u(k, f)}},
                pen(under, cell, false));
    }

    // $DAB2: the top of a slab at or below eye height (lit when nothing is above).
    void top_face(int k, int c, int f, std::uint8_t cell) {
        if (cell & full_height) return;
        auto index = top + ((cell & lit) ? 1 : 0);
        polygon({{x(k, c), t(k, f)}, {x(k, c + 1), t(k, f)}, {x(k + 1, c + 1), t(k + 1, f)}, {x(k + 1, c), t(k + 1, f)}},
                pen(index, cell, false));
    }

    Canvas& canvas_;
    const Window& window_;
    const Pens& pens_;
    const game::TowerMap& map_;
    Camera cam_;
    int cx_, cy_;
    Turn turn_;
    const LiftHeight& lift_height_;
    Projection p_;
};

} // namespace

Pens read_pens(const data::Memory& program) {
    constexpr std::uint32_t pens_at = 0x1228; // $A22(a6)
    Pens pens;
    for (std::size_t i = 0; i < pens.pens.size(); ++i) {
        const auto bytes = program.bytes(pens_at + std::uint32_t(i) * 16, 16);
        pens.pens[i] = read_pen(std::span<const std::uint8_t, 16>(bytes.data(), 16));
    }
    return pens;
}

Camera spider_camera(int x, int y, int z, int heading, int offset, int height, int distance) {
    for (int i = 0; i < (heading & 3); ++i) { // $AAD4: (x, z) -> (-z, x) per quarter turn
        const int old_x = x;
        x = -z;
        z = old_x;
    }
    return {std::int16_t(x + offset), std::int16_t(y + height), std::int16_t(z - distance), heading & 3};
}

void TowerView::draw(Canvas& canvas, const Window& window, int centre_x, int centre_y, const game::TowerMap& map,
                     const Camera& camera, const LiftHeight& lift_height) const {
    // $1191C: the view is cleared to colour 7, the sky and ground colour the
    // copper changes line by line.
    for (int y = window.top; y <= window.bottom; ++y)
        for (int x = window.left; x <= window.right; ++x) canvas.set(x, y, 7);
    Frame frame(canvas, window, pens_, map, camera, centre_x, centre_y, lift_height);
    frame.ground_shadows();
    frame.cells();
}

} // namespace babel::view
