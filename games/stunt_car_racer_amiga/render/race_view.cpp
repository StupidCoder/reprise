#include "stunt_car_racer_amiga/render/race_view.hpp"
#include <algorithm>
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

class Projector {
  public:
    explicit Projector(const Camera& camera)
        : camera_(camera), forward_x_(std::sin(radians(camera.yaw))), forward_z_(std::cos(radians(camera.yaw))),
          roll_sin_(std::sin(radians(camera.roll))), roll_cos_(std::cos(radians(camera.roll))) {}

    double ahead(const Point3& p) const { return (p.x - camera_.x) * forward_x_ + (p.z - camera_.z) * forward_z_; }

    Screen project(const Point3& p) const {
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
    Screen turned(double across, double down) const {
        return {centre_x + (roll_cos_ * across + roll_sin_ * down) * pixels_per_unit,
                centre_y + (roll_cos_ * down - roll_sin_ * across) * pixels_per_unit};
    }

    Camera camera_;
    double forward_x_, forward_z_, roll_sin_, roll_cos_;
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

View draw_race_view(const data::Circuits& circuits, const data::Scenery& scenery, const data::Track& track,
                    int segment, const Camera& camera) {
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

    const int count = int(track.segments.size());
    const auto wrap_segment = [&](int s) { return ((s % count) + count) % count; };
    std::vector<std::vector<data::CrossSection>> sections(static_cast<std::size_t>(count));
    for (const auto& c : data::road(circuits, track)) sections[std::size_t(c.segment)].push_back(c);
    const auto at = [&](int s) -> const std::vector<data::CrossSection>& { return sections[std::size_t(s)]; };
    const auto left = [](const data::CrossSection& c) {
        return Point3{double(c.left.x), double(c.left.z), double(c.left_height)};
    };
    const auto right = [](const data::CrossSection& c) {
        return Point3{double(c.right.x), double(c.right.z), double(c.right_height)};
    };

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

    // The colour of the road piece that ends at the first boundary at or
    // after cross-section `i` of segment `s` (in the track's own direction).
    const auto road_colour = [&](int s, int i) {
        for (int n = 0; n < count; ++n, s = wrap_segment(s + 1), i = 1) {
            const auto& c = at(s);
            const int last = int(c.size()) - 1;
            for (; i <= last; ++i) {
                if (c[std::size_t(i)].marked == (i == last)) continue;
                const int rise = c[std::size_t(i)].left_height - c[std::size_t(i - 1)].left_height;
                return std::abs(rise) >= jump_step ? jump_face : road_colours[s & 1];
            }
        }
        return road_colours[0];
    };

    // Painted from the far end back to the car, each segment from its far
    // end; the one behind the car last, from its near end.
    const Painter painter(view, projector);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const int s = *it;
        const auto& c = at(s);
        const int parity = s & 1;
        const int phase = track.segments[std::size_t(s)].kerb_phase;
        const int steps = int(c.size()) - 1;
        const bool far_end_last = (direction > 0) != (s == behind);
        for (int n = 0; n < steps; ++n) {
            const int i = far_end_last ? steps - n : n + 1;
            const auto al = left(c[std::size_t(i - 1)]), ar = right(c[std::size_t(i - 1)]);
            const auto bl = left(c[std::size_t(i)]), br = right(c[std::size_t(i)]);
            const auto base = [](Point3 p) { return Point3{p.x, p.z, double(side_base)}; };
            painter.draw_side({al, base(al), base(bl), bl}, wall_colours[parity]);
            painter.draw_side({br, base(br), base(ar), ar}, wall_colours[parity]);
            painter.draw({al, bl, br, ar}, road_colour(s, i));
            if (i == steps && s == track.start_line_segment) painter.draw_line(bl, br, start_line);
            if (s == behind) continue;
            const int ahead = steps_ahead(s, i);
            for (const auto& red : edge_reds) {
                if (ahead >= red.below) continue;
                const auto colour = ((i ^ phase) & 1) ? edge_yellow : red.colour;
                painter.draw_line(al, bl, colour);
                painter.draw_line(ar, br, colour);
                break;
            }
        }
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
