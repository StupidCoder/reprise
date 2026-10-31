#include "stunt_car_racer_amiga/render/race_view.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <cmath>
#include <numbers>

namespace stuntcar::render {
namespace {

// The race colours: 16 words with three bits a gun; the game widens each gun
// to four bits as 2c + 1 (0 stays 0) when it loads them.
constexpr std::uint32_t race_colours = 0x20184;

// Colours (game.md, "Race view"). Road and sides change colour from one
// segment to the next (even, odd); the lines along the road edges near the
// car change every step, yellow and a red that pales with distance.
constexpr std::uint8_t sky = 7, ground = 13;
constexpr std::uint8_t road_colours[2] = {1, 2}, wall_colours[2] = {15, 10};
constexpr std::uint8_t jump_face = 0;
constexpr std::uint8_t edge_yellow = 3;
constexpr std::uint8_t start_line = 15;
// The dark lines down the sides' edges near the car ($691B3).
constexpr std::uint8_t side_line = 9;
// Edge lines are drawn up to 12 steps ahead of the car: red 9 for the first
// six, 10 for the next four, then 11.
constexpr struct {
    int below;
    std::uint8_t colour;
} edge_reds[] = {{6, 9}, {10, 10}, {12, 11}};
// Sides reach down to the ground, at this height.
constexpr int side_base = 0x200;
// The road surface is drawn in pieces split at boundaries: where segments
// meet, and at marked steps of the left height profile (a segment's last
// step, marked, means no boundary there). A piece takes the colour of the
// segment at its far boundary, or the dark jump face when the left edge
// rises or falls by jump_step or more at that boundary.
constexpr int jump_step = 0x280;

constexpr double turn = 65536.0;
constexpr double pixels_per_unit = 1.0 / 32; // 2048 pixels a turn
constexpr double centre_x = 128, centre_y = 64;
// Points nearer than this ahead of the eye are clipped away.
constexpr double near_distance = 32;

double wrap(double angle) {
    angle = std::fmod(angle, turn);
    if (angle >= turn / 2) angle -= turn;
    if (angle < -turn / 2) angle += turn;
    return angle;
}

double radians(int angle) { return angle * 2 * std::numbers::pi / turn; }

struct Point3 {
    double x, z, h; // plan, plan, height units
};

struct Screen {
    double x, y;
};

// The track picture's projection ($62454 and $624C2 with $1BB8F set): plan
// distances are halved, the bearing taken against a point $4900 further
// off (at a quarter of the forward distance), the drop against half the
// forward distance plus $3100, heights scaled by $4C1B / $8000.
constexpr double picture_bearing_offset = 0x4900, picture_distance_offset = 0x3100, picture_height_scale = 0x4C1B;

class Projector {
  public:
    explicit Projector(const Camera& camera, bool picture = false)
        : camera_(camera), forward_x_(std::sin(radians(camera.yaw))), forward_z_(std::cos(radians(camera.yaw))),
          roll_sin_(std::sin(radians(camera.roll))), roll_cos_(std::cos(radians(camera.roll))), picture_(picture) {}

    double ahead(const Point3& p) const {
        const double forward = (p.x - camera_.x) * forward_x_ + (p.z - camera_.z) * forward_z_;
        return picture_ ? std::floor(forward / 2) + picture_distance_offset : forward;
    }

    Screen project(const Point3& p) const {
        if (picture_) return project_picture(p);
        const double dx = p.x - camera_.x, dz = p.z - camera_.z;
        const double bearing = std::atan2(dx, dz) * turn / (2 * std::numbers::pi);
        const double across = wrap(bearing - camera_.yaw);
        // Heights count a quarter as much as plan distances, in whole units.
        const double rise = std::floor((camera_.eye - p.h) / 4);
        const double below = std::atan2(rise, std::hypot(dx, dz)) * turn / (2 * std::numbers::pi);
        const double down = below - camera_.pitch;
        return turned(across, down);
    }

    // Whether a screen point lies below the horizon (a straight line, turned
    // by the roll).
    bool below_horizon(double sx, double sy) const {
        const double u = (sx - centre_x) / pixels_per_unit, w = (sy - centre_y) / pixels_per_unit;
        const double down = roll_sin_ * u + roll_cos_ * w;
        return down > -camera_.pitch;
    }

    // A point of the scenery: a bearing and an angle above the horizon.
    Screen project_scenery(double bearing, double above) const {
        return turned(wrap(bearing - camera_.yaw), -above - camera_.pitch);
    }

  private:
    Screen project_picture(const Point3& p) const {
        const double dx = p.x - camera_.x, dz = p.z - camera_.z;
        const double forward = std::round(dx * forward_x_ + dz * forward_z_);
        const double rightward = std::round(dx * forward_z_ - dz * forward_x_);
        const double half_forward = std::floor(forward / 2), half_right = std::floor(rightward / 2);
        const double across = std::atan2(half_right, std::floor(half_forward / 2) + picture_bearing_offset) * turn /
                              (2 * std::numbers::pi);
        const double rise = std::floor(std::floor((camera_.eye - p.h) / 4) * picture_height_scale * 2 / 65536);
        const double below =
            std::atan2(rise, half_forward + picture_distance_offset) * turn / (2 * std::numbers::pi);
        return turned(across, below - camera_.pitch);
    }

    Screen turned(double across, double down) const {
        return {centre_x + (roll_cos_ * across + roll_sin_ * down) * pixels_per_unit,
                centre_y + (roll_cos_ * down - roll_sin_ * across) * pixels_per_unit};
    }

    Camera camera_;
    double forward_x_, forward_z_, roll_sin_, roll_cos_;
    bool picture_;
};

// The game's projection bends straight lines; long edges are cut into short
// pieces so that a near road fills the view as it should.
std::vector<Point3> subdivided(const std::vector<Point3>& polygon, bool closed, const auto& ahead) {
    std::vector<Point3> out;
    const std::size_t edges = closed ? polygon.size() : polygon.size() - 1;
    for (std::size_t i = 0; i < edges; ++i) {
        const auto& a = polygon[i];
        const auto& b = polygon[(i + 1) % polygon.size()];
        const double length = std::hypot(b.x - a.x, b.z - a.z, (b.h - a.h) / 4);
        const double near = std::max(16.0, std::min(ahead(a), ahead(b)));
        const int pieces = std::clamp(int(std::ceil(length / (near / 4))), 1, 64);
        for (int k = 0; k < pieces; ++k) {
            const double t = double(k) / pieces;
            out.push_back({a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.h + (b.h - a.h) * t});
        }
    }
    if (!closed) out.push_back(polygon.back());
    return out;
}

// Draws a one-pixel line, clipped to the view. The game draws its edge lines
// from the road outline's rows one row higher (its line routine starts at
// the edge's first row minus one).
void line(View& view, Screen a, Screen b, std::uint8_t colour) {
    const int steps = std::max(1, int(std::ceil(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)))));
    if (steps > 4096) return;
    for (int k = 0; k <= steps; ++k) {
        const double t = double(k) / steps;
        const int x = int(std::floor(a.x + (b.x - a.x) * t)), y = int(std::floor(a.y + (b.y - a.y) * t)) - 1;
        if (x >= 0 && x < view_width && y >= 0 && y < view_height) view.pixels[std::size_t(y * view_width + x)] = colour;
    }
}

// Fills a polygon by scanlines (pixel centres), clipped to the view.
void fill(View& view, const std::vector<Screen>& points, std::uint8_t colour) {
    if (points.size() < 3) return;
    double top = points[0].y, bottom = points[0].y;
    for (const auto& p : points) top = std::min(top, p.y), bottom = std::max(bottom, p.y);
    const int y0 = std::max(0, int(std::ceil(top - 0.5))), y1 = std::min(view_height - 1, int(std::floor(bottom - 0.5)));
    std::vector<double> crossings;
    for (int y = y0; y <= y1; ++y) {
        const double cy = y + 0.5;
        crossings.clear();
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& a = points[i];
            const auto& b = points[(i + 1) % points.size()];
            if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy))
                crossings.push_back(a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(crossings.begin(), crossings.end());
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int x0 = std::max(0, int(std::ceil(crossings[i] - 0.5)));
            const int x1 = std::min(view_width - 1, int(std::floor(crossings[i + 1] - 0.5)));
            for (int x = x0; x <= x1; ++x) view.pixels[std::size_t(y * view_width + x)] = colour;
        }
    }
}

double signed_area(const std::vector<Screen>& points) {
    double area = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area / 2;
}

class Painter {
  public:
    Painter(View& view, const Projector& projector) : view_(view), projector_(projector) {}

    // Clips the polygon to the space ahead of the eye, projects and fills it.
    // Returns the projected outline (empty when wholly behind).
    std::vector<Screen> outline(const std::vector<Point3>& polygon) const {
        std::vector<Point3> kept;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto& a = polygon[i];
            const auto& b = polygon[(i + 1) % polygon.size()];
            const double da = projector_.ahead(a) - near_distance, db = projector_.ahead(b) - near_distance;
            if (da >= 0) kept.push_back(a);
            if ((da >= 0) != (db >= 0)) {
                const double t = da / (da - db);
                kept.push_back({a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.h + (b.h - a.h) * t});
            }
        }
        std::vector<Screen> screen;
        if (kept.size() < 3) return screen;
        for (const auto& p : subdivided(kept, true, [&](const Point3& q) { return projector_.ahead(q); }))
            screen.push_back(projector_.project(p));
        return screen;
    }

    // A line along a road edge (only where it is ahead of the eye).
    void draw_line(Point3 a, Point3 b, std::uint8_t colour) const {
        double da = projector_.ahead(a) - near_distance, db = projector_.ahead(b) - near_distance;
        if (da < 0 && db < 0) return;
        if (da < 0) a = {a.x + (b.x - a.x) * da / (da - db), a.z + (b.z - a.z) * da / (da - db), a.h + (b.h - a.h) * da / (da - db)};
        if (db < 0) b = {b.x + (a.x - b.x) * db / (db - da), b.z + (a.z - b.z) * db / (db - da), b.h + (a.h - b.h) * db / (db - da)};
        const auto points = subdivided({a, b}, false, [&](const Point3& q) { return projector_.ahead(q); });
        for (std::size_t i = 1; i < points.size(); ++i)
            line(view_, projector_.project(points[i - 1]), projector_.project(points[i]), colour);
    }

    void draw(const std::vector<Point3>& polygon, std::uint8_t colour) const { fill(view_, outline(polygon), colour); }

    // A side is seen only from outside the road.
    void draw_side(const std::vector<Point3>& polygon, std::uint8_t colour) const {
        const auto screen = outline(polygon);
        if (screen.size() >= 3 && signed_area(screen) > 0) fill(view_, screen, colour);
    }

  private:
    View& view_;
    const Projector& projector_;
};

// The cross-sections of one segment, first to last.
std::vector<data::CrossSection> segment_sections(const std::vector<data::CrossSection>& road, int segment) {
    std::vector<data::CrossSection> out;
    for (const auto& c : road)
        if (c.segment == segment) out.push_back(c);
    return out;
}

// The road's cross-sections by segment, and what the painting asks of them.
Point3 left(const data::CrossSection& c) { return {double(c.left.x), double(c.left.z), double(c.left_height)}; }
Point3 right(const data::CrossSection& c) { return {double(c.right.x), double(c.right.z), double(c.right_height)}; }

// A cross-section in drawing order ($67092 builds the rows from the near
// end outwards); a step joins a row to the one before it.
struct Row {
    int segment, index;
};

class Road {
  public:
    Road(const data::Circuits& circuits, const data::Track& track) : track_(track), count_(int(track.segments.size())) {
        sections_.resize(std::size_t(count_));
        for (const auto& c : data::road(circuits, track)) sections_[std::size_t(c.segment)].push_back(c);
    }
    const data::Track& track() const { return track_; }
    int count() const { return count_; }
    int wrap(int s) const { return ((s % count_) + count_) % count_; }
    const std::vector<data::CrossSection>& at(int s) const { return sections_[std::size_t(s)]; }
    const std::vector<std::vector<data::CrossSection>>& sections() const { return sections_; }
    Point3 point(const Row& r, bool right_side) const {
        const auto& c = at(r.segment)[std::size_t(r.index)];
        return right_side ? right(c) : left(c);
    }

    // The rows of a run of segments, near to far.
    std::vector<Row> rows_of(const std::vector<int>& segments, int dir) const {
        std::vector<Row> rows;
        for (std::size_t n = 0; n < segments.size(); ++n) {
            const int s = segments[n], last = int(at(s).size()) - 1;
            for (int k = 0; k <= last; ++k) {
                const int i = dir > 0 ? k : last - k;
                if (n > 0 && k == 0) continue; // shared with the segment before
                rows.push_back({s, i});
            }
        }
        return rows;
    }

    // A road boundary ($1E bit 7 clear): where a piece of road surface ends.
    bool boundary(Row r) const {
        if (r.index == 0) {
            r.segment = wrap(r.segment - 1);
            r.index = int(at(r.segment).size()) - 1;
        }
        const int last = int(at(r.segment).size()) - 1;
        return at(r.segment)[std::size_t(r.index)].marked != (r.index == last);
    }

    // The colour of the road piece that ends at the first boundary at or
    // after cross-section `i` of segment `s` (in the track's own direction).
    std::uint8_t road_colour(int s, int i) const {
        for (int n = 0; n < count_; ++n, s = wrap(s + 1), i = 1) {
            const auto& c = at(s);
            const int last = int(c.size()) - 1;
            for (; i <= last; ++i) {
                if (c[std::size_t(i)].marked == (i == last)) continue;
                const int rise = c[std::size_t(i)].left_height - c[std::size_t(i - 1)].left_height;
                return std::abs(rise) >= jump_step ? jump_face : road_colours[s & 1];
            }
        }
        return road_colours[0];
    }

  private:
    const data::Track& track_;
    int count_;
    std::vector<std::vector<data::CrossSection>> sections_;
};

// The edge lines' colour for the step ending at (segment, index), if any.
using EdgeColour = std::function<std::optional<std::uint8_t>(int segment, int index)>;
// Called after each road piece: the rows and the piece's range in them.
using AfterPiece = std::function<void(const std::vector<Row>&, std::size_t bottom, std::size_t top)>;

// Paints a run of rows from its far end back to its near one, a road piece
// (a group of steps between boundaries) at a time ($68F28): both sides,
// each a single polygon down to the ground with dark lines (colour 9) along
// its edges at the rows whose edge lines are red ($6892A, $6897C; rows with
// yellow lines have no such edge inside a piece, $67284); the surface
// ($68D82) with the start line; the edge lines along it ($689CE, $68A12).
// Normally the sides come first; while the car is beside the road ($1BB9A
// set) the side nearer the car comes after the surface, since it then
// stands between the car and the road.
void paint_run(const Road& road, const Painter& painter, const std::vector<Row>& rows, int dir,
               const EdgeColour& edge_colours, Beside beside, const AfterPiece& after) {
    if (rows.size() < 2) return;
    const auto base = [](Point3 p) { return Point3{p.x, p.z, double(side_base)}; };
    // The step from row k - 1 to row k, as (segment, higher index).
    const auto step_index = [&](std::size_t k) { return dir > 0 ? rows[k].index : rows[k].index + 1; };
    const auto edge_colour = [&](std::size_t k) -> std::optional<std::uint8_t> {
        if (!edge_colours) return std::nullopt;
        return edge_colours(rows[k].segment, step_index(k));
    };
    std::size_t top = rows.size() - 1;
    while (top > 0) {
        std::size_t bottom = top - 1;
        while (bottom > 0 && !road.boundary(rows[bottom])) --bottom;
        // Rows bottom..top in track order (lowest track position first).
        std::vector<Row> run(rows.begin() + std::ptrdiff_t(bottom), rows.begin() + std::ptrdiff_t(top) + 1);
        if (dir < 0) std::reverse(run.begin(), run.end());
        const std::uint8_t wall = wall_colours[rows[top].segment & 1];

        const auto side = [&](bool right_side) {
            std::vector<Point3> polygon;
            for (const auto& r : run) polygon.push_back(base(road.point(r, right_side)));
            for (auto it = run.rbegin(); it != run.rend(); ++it) polygon.push_back(road.point(*it, right_side));
            // Wound so that a side seen from outside turns positively.
            if (right_side) std::reverse(polygon.begin(), polygon.end());
            painter.draw_side(polygon, wall);
            for (std::size_t k = top; k > bottom; --k) {
                const auto colour = edge_colour(k);
                if (!colour || *colour == edge_yellow) continue;
                const auto p = road.point(rows[k], right_side);
                painter.draw_line(p, base(p), side_line);
            }
        };
        const auto surface = [&]() {
            std::vector<Point3> polygon;
            for (const auto& r : run) polygon.push_back(road.point(r, false));
            for (auto it = run.rbegin(); it != run.rend(); ++it) polygon.push_back(road.point(*it, true));
            painter.draw(polygon, road.road_colour(rows[top].segment, step_index(top)));
            for (std::size_t k = top; k > bottom; --k) {
                const auto& r = rows[k];
                const int last = int(road.at(r.segment).size()) - 1;
                if (r.segment == road.track().start_line_segment && r.index == last)
                    painter.draw_line(road.point(r, false), road.point(r, true), start_line);
            }
        };
        const auto edges = [&](bool right_side) {
            for (std::size_t k = top; k > bottom; --k)
                if (const auto colour = edge_colour(k))
                    painter.draw_line(road.point(rows[k - 1], right_side), road.point(rows[k], right_side), *colour);
        };
        const auto car = [&]() {
            if (after) after(rows, bottom, top);
        };
        switch (beside) {
        case Beside::left:
            side(true), surface(), edges(true), car(), side(false), edges(false);
            break;
        case Beside::right:
            side(false), surface(), edges(false), car(), side(true), edges(true);
            break;
        case Beside::no:
            side(false), side(true), surface(), edges(false), edges(true), car();
            break;
        }
        top = bottom;
    }
}

} // namespace

Palette race_palette(const data::Program& program) {
    Palette palette;
    for (int i = 0; i < 16; ++i) {
        const auto word = program.u16(race_colours + std::uint32_t(i) * 2);
        const auto widen = [](int c) { return std::uint8_t(c ? (c * 2 + 1) * 17 : 0); };
        palette[std::size_t(i)] = {widen(word >> 8 & 7), widen(word >> 4 & 7), widen(word & 7)};
    }
    return palette;
}

// The opponent's car, drawn as the game does it: its four corners are
// projected (at the body's height, and at the lower of that and $50 above
// the road for the shadow); everything else is built on screen from each
// axle's projected half-vector U and V = (U.y, -U.x) / 2, which points up
// the screen, in halves, quarters and eighths ($599E2-$5A182). The rear
// axle's cross-section is tall (from -V/2 to V), the front one low (from 0
// to -V/2), the body the box between them; each wheel a quad from the
// axle's end half-way in, hanging V below. Faces ($67AA6): the shadow (5,
// while over the road), far wheels (0), sides (12), back (10), top (15),
// underside (9), near wheels (0); the body's faces only when facing the eye.
constexpr std::uint8_t opponent_shadow = 5, opponent_wheel = 0, opponent_side = 12, opponent_back = 10,
                       opponent_top = 15, opponent_underside = 9;
constexpr int opponent_half_width = 27;           // lateral units of 256 across the road
constexpr double opponent_axles = 0.5;            // steps between the axles
constexpr int opponent_shadow_height = 0x50;      // the shadow sits at most this far above the road
constexpr int opponent_shadow_road_min = 0x1C, opponent_shadow_road_max = 0xE4; // its middle over the road

namespace {

struct Pixel {
    int x, y;
};

// The game's polygon filler for the opponent: from the top corner, the
// polygon's outline is followed both ways; going forward gives each row's
// right end, going back its left end, and a row is filled only where its
// left end lies left of its right end. A face turned away fills nothing; one
// seen almost edge-on leaves the slivers the game shows.
void fill_chains(View& view, const std::vector<Screen>& loop, std::uint8_t colour) {
    const int n = int(loop.size());
    if (n < 3) return;
    int top = 0, bottom = 0;
    for (int i = 1; i < n; ++i) {
        if (loop[std::size_t(i)].y < loop[std::size_t(top)].y) top = i;
        if (loop[std::size_t(i)].y > loop[std::size_t(bottom)].y) bottom = i;
    }
    // x where a chain (stepping `dir` from the top) crosses row centre cy.
    const auto cross = [&](int dir, double cy, double& x) {
        for (int i = top, k = 0; k < n; ++k) {
            const int j = ((i + dir) % n + n) % n;
            const auto& a = loop[std::size_t(i)];
            const auto& b = loop[std::size_t(j)];
            if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy)) {
                x = a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y);
                return true;
            }
            if (i == bottom && k > 0) break;
            i = j;
        }
        return false;
    };
    const int y0 = std::max(0, int(std::ceil(loop[std::size_t(top)].y - 0.5)));
    const int y1 = std::min(view_height - 1, int(std::floor(loop[std::size_t(bottom)].y - 0.5)));
    for (int y = y0; y <= y1; ++y) {
        double right = 0, left = 0;
        if (!cross(+1, y + 0.5, right) || !cross(-1, y + 0.5, left) || left >= right) continue;
        const int x0 = std::max(0, int(std::ceil(left - 0.5))), x1 = std::min(view_width - 1, int(std::floor(right - 0.5)));
        for (int x = x0; x <= x1; ++x) view.pixels[std::size_t(y * view_width + x)] = colour;
    }
}

// Truncating division by a power of two, as the game's shift-and-negate does.
int toward_zero(int v, int shift) { return v < 0 ? -((-v) >> shift) : v >> shift; }

void draw_opponent(View& view, const Projector& projector, const std::vector<std::vector<data::CrossSection>>& sections,
                   const Opponent& opponent) {
    const auto& cs = sections[std::size_t(opponent.segment)];
    struct Corner {
        Point3 body, low;
    };
    // Corner on the road at a step position and a lateral position.
    const auto corner = [&](double step, double across, double lift) -> std::optional<Corner> {
        const int i = int(std::floor(step));
        if (i < 0 || i + 1 >= int(cs.size())) return std::nullopt;
        const double f = step - i, t = across / 256;
        const auto lerp = [](double a, double b, double k) { return a + (b - a) * k; };
        const auto& a = cs[std::size_t(i)];
        const auto& b = cs[std::size_t(i + 1)];
        const double lx = lerp(a.left.x, b.left.x, f), lz = lerp(a.left.z, b.left.z, f);
        const double rx = lerp(a.right.x, b.right.x, f), rz = lerp(a.right.z, b.right.z, f);
        const double road = lerp(lerp(a.left_height, b.left_height, f), lerp(a.right_height, b.right_height, f), t);
        const double x = lerp(lx, rx, t), z = lerp(lz, rz, t), body = road + lift;
        return Corner{{x, z, body}, {x, z, std::min(body, road + opponent_shadow_height)}};
    };
    const double left = opponent.across - opponent_half_width, right = opponent.across + opponent_half_width;
    const auto rear_left = corner(opponent.step, left, opponent.lift[0]);
    const auto rear_right = corner(opponent.step, right, opponent.lift[1]);
    const auto front_left = corner(opponent.step + opponent_axles, left, opponent.lift[2]);
    const auto front_right = corner(opponent.step + opponent_axles, right, opponent.lift[3]);
    if (!rear_left || !rear_right || !front_left || !front_right) return;
    for (const auto* c : {&*rear_left, &*rear_right, &*front_left, &*front_right})
        if (projector.ahead(c->body) < near_distance) return;
    const auto pixel = [&](const Point3& p) {
        const auto s = projector.project(p);
        return Pixel{int(std::floor(s.x)), int(std::floor(s.y))};
    };

    struct Axle {
        Pixel centre, u, v, u2, u4, u8, v2;
    };
    const auto axle = [&](Pixel a, Pixel b) {
        int dx = (b.x - a.x) >> 1, dy = (b.y - a.y) >> 1;
        dx = std::clamp(dx, -0xFF, 0xFF), dy = std::clamp(dy, -0xFF, 0xFF);
        Axle x;
        x.centre = {a.x + dx, a.y + dy};
        x.u = {dx, dy};
        x.v = {dy >> 1, (-dx) >> 1};
        x.u2 = {toward_zero(dx, 1), toward_zero(dy, 1)};
        x.u4 = {toward_zero(dx, 2), toward_zero(dy, 2)};
        x.u8 = {toward_zero(dx, 3), toward_zero(dy, 3)};
        x.v2 = {toward_zero(x.v.x, 1), toward_zero(x.v.y, 1)};
        return x;
    };
    const auto rear = axle(pixel(rear_left->body), pixel(rear_right->body));
    const auto front = axle(pixel(front_left->body), pixel(front_right->body));
    const auto at = [](const Axle& a, int x, int y, int grow_x = 0, int grow_y = 0) {
        return Screen{double(a.centre.x + x + grow_x), double(a.centre.y + y + grow_y)};
    };
    // A wheel from the axle's end (-1: left, +1: right) half-way in.
    const auto wheel = [&](const Axle& a, int side) {
        const int ox = side < 0 ? -a.u.x : a.u.x - a.u2.x, oy = side < 0 ? -a.u.y : a.u.y - a.u2.y;
        return std::vector<Screen>{at(a, ox - a.v.x, oy - a.v.y, 0, 1), at(a, ox, oy),
                                   at(a, ox + a.u2.x, oy + a.u2.y, 1, 0), at(a, ox + a.u2.x - a.v.x, oy + a.u2.y - a.v.y, 1, 1)};
    };
    // Rear cross-section (108, 10A, 10C, 10E) and front one (110-116).
    const Screen r0 = at(rear, -rear.u2.x - rear.v2.x, -rear.u2.y - rear.v2.y, 0, 1);
    const Screen r1 = at(rear, rear.v.x - rear.u4.x - rear.u8.x, rear.v.y - rear.u4.y - rear.u8.y);
    const Screen r2 = at(rear, rear.v.x + rear.u4.x + rear.u8.x, rear.v.y + rear.u4.y + rear.u8.y, 1, 0);
    const Screen r3 = at(rear, rear.u2.x - rear.v2.x, rear.u2.y - rear.v2.y, 1, 1);
    const Screen f0 = at(front, -front.u2.x - front.v2.x, -front.u2.y - front.v2.y, 0, 1);
    const Screen f1 = at(front, -front.u2.x, -front.u2.y);
    const Screen f2 = at(front, front.u2.x, front.u2.y, 1, 0);
    const Screen f3 = at(front, front.u2.x - front.v2.x, front.u2.y - front.v2.y, 1, 1);

    if (opponent.across >= opponent_shadow_road_min && opponent.across <= opponent_shadow_road_max) {
        auto s0 = pixel(rear_left->low), s1 = pixel(front_left->low), s2 = pixel(front_right->low), s3 = pixel(rear_right->low);
        s0.x -= rear.v.x, s0.y -= rear.v.y - 1, s3.x -= rear.v.x, s3.y -= rear.v.y - 1;
        s1.x -= front.v.x, s1.y -= front.v.y, s2.x -= front.v.x, s2.y -= front.v.y;
        fill_chains(view, {{double(s0.x), double(s0.y)}, {double(s1.x), double(s1.y)}, {double(s2.x), double(s2.y)},
                           {double(s3.x), double(s3.y)}},
                    opponent_shadow);
    }
    fill_chains(view, wheel(front, -1), opponent_wheel);
    fill_chains(view, wheel(front, +1), opponent_wheel);
    const auto face = [&](std::vector<Screen> loop, std::uint8_t colour) { fill_chains(view, loop, colour); };
    face({f0, f1, r1, r0}, opponent_side);
    face({r3, r2, f2, f3}, opponent_side);
    face({r0, r1, r2, r3}, opponent_back);
    face({r1, f1, f2, r2}, opponent_top);
    face({f0, r0, r3, f3}, opponent_underside);
    fill_chains(view, wheel(rear, -1), opponent_wheel);
    fill_chains(view, wheel(rear, +1), opponent_wheel);
}

} // namespace

View draw_race_view(const data::Circuits& circuits, const data::Scenery& scenery, const data::Track& track,
                    int segment, const Camera& camera, const std::optional<Opponent>& opponent, Beside beside) {
    View view;
    const Projector projector(camera);
    for (int y = 0; y < view_height; ++y)
        for (int x = 0; x < view_width; ++x)
            view.pixels[std::size_t(y * view_width + x)] = projector.below_horizon(x + 0.5, y + 0.5) ? ground : sky;

    // The hills, those whose bearing lies from 28/256 of a turn left of the
    // view's heading to 16/256 right of it (the game's window; shapes extend
    // rightwards from their bearing).
    for (const auto& placed : scenery.placed) {
        const int offset = (placed.bearing - (camera.yaw >> 8) + 0x1C) & 0xFF;
        if (offset >= 0x2C) continue;
        const auto& shape = scenery.shapes.at(std::size_t(placed.shape));
        for (const auto& polygon : shape.polygons) {
            std::vector<Screen> outline;
            for (const int corner : polygon.corners) {
                const auto& p = shape.points.at(std::size_t(corner));
                outline.push_back(projector.project_scenery(placed.bearing * 256.0 + p.x * 8.0, p.y * 8.0));
            }
            fill(view, outline, polygon.colour);
        }
    }

    const Road road(circuits, track);
    const auto wrap_segment = [&](int s) { return road.wrap(s); };
    const auto at = [&](int s) -> const std::vector<data::CrossSection>& { return road.at(s); };
    const int count = road.count();

    // Where the car is along its segment, and which way the view faces along
    // the track (the game walks the track backwards when the car faces back).
    const auto& own = at(segment);
    int car_step = 0;
    double best = 1e18;
    for (std::size_t i = 0; i < own.size(); ++i) {
        const auto middle = (left(own[i]).x + right(own[i]).x) / 2, depth = (left(own[i]).z + right(own[i]).z) / 2;
        const double d = std::hypot(middle - camera.x, depth - camera.z);
        if (d < best) best = d, car_step = int(i);
    }
    const auto& from = own[std::size_t(std::min(car_step, int(own.size()) - 2))];
    const auto& to = own[std::size_t(std::min(car_step, int(own.size()) - 2) + 1)];
    const Point3 travel{(left(to).x + right(to).x - left(from).x - right(from).x) / 2,
                        (left(to).z + right(to).z - left(from).z - right(from).z) / 2, 0};
    const int direction = projector.ahead({camera.x + travel.x, camera.z + travel.z, 0}) >= 0 ? 1 : -1;

    // Steps along the whole track, for counting how far ahead a step lies.
    std::vector<int> first_step(std::size_t(count) + 1, 0);
    for (int s = 0; s < count; ++s) first_step[std::size_t(s) + 1] = first_step[std::size_t(s)] + int(at(s).size()) - 1;
    const int total_steps = first_step.back();
    const int car_global = first_step[std::size_t(segment)] + car_step;
    // How many steps ahead of the car the far end of step i of segment s is.
    const auto steps_ahead = [&](int s, int i) {
        const int far = first_step[std::size_t(s)] + (direction > 0 ? i : i - 1);
        return ((direction * (far - car_global)) % total_steps + total_steps) % total_steps;
    };

    // The segments drawn: the one behind the car, the car's, then onwards
    // until a segment ends off the side of the view (the game stops there).
    const int behind = wrap_segment(segment - direction);
    std::vector<int> order{behind, segment};
    for (int n = 1; n < count - 1; ++n) {
        const int s = wrap_segment(segment + n * direction);
        order.push_back(s);
        if (n < 3) continue;
        const auto& end = direction > 0 ? at(s).back() : at(s).front();
        bool on_screen = false;
        for (const auto& edge : {left(end), right(end)}) {
            if (projector.ahead(edge) < near_distance) continue;
            const auto p = projector.project(edge);
            on_screen = on_screen || (p.x >= 0 && p.x < view_width);
        }
        if (!on_screen) break;
    }

    // The road piece after which the opponent is drawn: the one ending at
    // the cross-section behind its rear axle (or the previous segment's
    // last, at a segment's start).
    std::pair<int, int> opponent_piece{-1, -1};
    bool opponent_drawn = false;
    if (opponent) {
        const int k = int(std::floor(opponent->step));
        opponent_piece = k >= 1 ? std::pair{opponent->segment, k}
                                : std::pair{wrap_segment(opponent->segment - 1), int(at(wrap_segment(opponent->segment - 1)).size()) - 1};
    }

    const Painter painter(view, projector);
    // Edge lines near the car: red, paler with distance, or yellow on
    // alternate steps ($673EE).
    const auto near_edges = [&](int s, int i) -> std::optional<std::uint8_t> {
        const int ahead = steps_ahead(s, i);
        for (const auto& red : edge_reds)
            if (ahead < red.below) return ((i ^ track.segments[std::size_t(s)].kerb_phase) & 1) ? edge_yellow : red.colour;
        return std::nullopt;
    };
    // The opponent after the piece holding the row behind its rear axle
    // ($69188 with $57C69 clear).
    const auto car = [&](const std::vector<Row>& rows, std::size_t bottom, std::size_t top) {
        if (!opponent || opponent_drawn) return;
        for (std::size_t k = bottom; k <= top; ++k)
            if (rows[k].segment == opponent_piece.first && rows[k].index == opponent_piece.second) {
                draw_opponent(view, projector, road.sections(), *opponent);
                opponent_drawn = true;
                return;
            }
    };
    std::vector<int> ahead_segments(order.begin() + 1, order.end());
    paint_run(road, painter, road.rows_of(ahead_segments, direction), direction, near_edges, beside, car);
    paint_run(road, painter, road.rows_of({behind}, -direction), -direction, nullptr, beside, car);
    if (opponent && !opponent_drawn) draw_opponent(view, projector, road.sections(), *opponent);
    return view;
}

// The track picture ($5D20C, $604B4): the camera stands high above the
// middle of one side of the arena ($60552/$60556 its x and z high bytes,
// $6055A its heading, for views 0-3) looking down at $700; every cell is
// drawn from the far rows of the grid to the near, each row from the right
// edge in, then from the left edge in to the middle ($602E4), each cell's
// segment whole, its rows walked from the camera outwards. The edge lines'
// row count starts afresh in each cell, so they run red and yellow all
// round.
View draw_track_picture(const data::Program& program, const data::Circuits& circuits, const data::Track& track,
                        int view_number) {
    constexpr std::uint32_t camera_x = 0x60552, camera_z = 0x60556, camera_yaw = 0x6055A;
    constexpr int picture_pitch = 0x700, picture_eye = (0x03F00000 >> 11) + (0x780 >> 4);
    constexpr int cell_size = 0x800, grid = 16, rows_out = 16, half_width = 8;
    View view;
    view.pixels.fill(View::untouched);
    const int k = view_number & 3;
    Camera camera;
    camera.x = double(program.u8(camera_x + std::uint32_t(k)) << 12);
    camera.z = double(program.u8(camera_z + std::uint32_t(k)) << 12);
    camera.yaw = program.u8(camera_yaw + std::uint32_t(k)) << 8;
    camera.eye = picture_eye;
    camera.pitch = picture_pitch;
    const Projector projector(camera, true);
    const Painter painter(view, projector);
    const Road road(circuits, track);

    std::map<std::pair<int, int>, int> segment_in_cell;
    for (int s = 0; s < road.count(); ++s)
        segment_in_cell[{track.segments[std::size_t(s)].cell_x, track.segments[std::size_t(s)].cell_z}] = s;
    const int home_x = int(camera.x) / cell_size, home_z = int(camera.z) / cell_size;
    const auto cell_for = [&](int across, int out) -> std::pair<int, int> {
        switch (k) {
        case 0: return {home_x + across, home_z + out};
        case 1: return {home_x + out, home_z - across};
        case 2: return {home_x - across, home_z - out};
        default: return {home_x - out, home_z + across};
        }
    };
    const auto edges = [&](int s, int i) -> std::optional<std::uint8_t> {
        return ((i ^ track.segments[std::size_t(s)].kerb_phase) & 1) ? edge_yellow : edge_reds[0].colour;
    };
    const auto draw_cell = [&](int across, int out) {
        const auto [x, z] = cell_for(across, out);
        if (x < 0 || z < 0 || x >= grid || z >= grid) return;
        const auto found = segment_in_cell.find({x, z});
        if (found == segment_in_cell.end()) return;
        const int s = found->second;
        const auto& c = road.at(s);
        const auto middle = [&](const data::CrossSection& cs) {
            return Point3{(cs.left.x + cs.right.x) / 2.0, (cs.left.z + cs.right.z) / 2.0, 0};
        };
        const int dir = projector.ahead(middle(c.front())) <= projector.ahead(middle(c.back())) ? 1 : -1;
        paint_run(road, painter, road.rows_of({s}, dir), dir, edges, Beside::no, nullptr);
    };
    for (int out = rows_out; out >= 0; --out) {
        for (int across = half_width; across >= 1; --across) draw_cell(across, out);
        for (int across = -half_width; across <= 0; ++across) draw_cell(across, out);
    }
    return view;
}

platform::Image to_image(const View& view, const Palette& palette) {
    platform::Image image(view_width, view_height);
    for (std::size_t i = 0; i < view.pixels.size(); ++i) {
        const auto& c = palette[view.pixels[i]];
        image.pixels[i] = {c.r, c.g, c.b, 255};
    }
    return image;
}

Camera start_camera(const data::Circuits& circuits, const data::Track& track) {
    // The car comes down near the middle of its start segment's cell.
    const auto sections = segment_sections(data::road(circuits, track), track.car_start_segment);
    const auto& a = sections[sections.size() / 2];
    const auto& b = sections[sections.size() / 2 + 1];
    Camera camera;
    camera.x = (a.left.x + a.right.x) / 2.0;
    camera.z = (a.left.z + a.right.z) / 2.0;
    camera.eye = (a.left_height + a.right_height) / 2.0 + 99;
    const double dx = (b.left.x + b.right.x) / 2.0 - camera.x, dz = (b.left.z + b.right.z) / 2.0 - camera.z;
    camera.yaw = int(std::lround(std::atan2(dx, dz) * turn / (2 * std::numbers::pi))) & 0xFFFF;
    return camera;
}

} // namespace stuntcar::render
