#include "fa_18_interceptor_amiga/game/draw_order.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include "fa_18_interceptor_amiga/game/attitude.hpp"
#include "fa_18_interceptor_amiga/game/flight_tables.hpp"
#include <algorithm>
#include <cmath>

namespace interceptor::game {
namespace {

using Point = std::array<double, 3>;

constexpr int sky = 5, land = 4, water = 6; // flight palette indices
constexpr double near_plane = 4;
constexpr double far_ground = 400000;
// The scene builder's window of cells around the camera ($1106B4: offsets
// up to two or three cells): the coarse maps' cells are 65536 units, the
// fine map's 16384.
constexpr double coarse_window = 3 * 65536.0, fine_window = 3 * 16384.0;

class Raster {
public:
    Raster(IndexedImage& image, int centre, int bottom, const Camera& camera)
        : image_(image), centre_(centre), bottom_(std::min(bottom, image.height - 1)), camera_(camera),
          depth_(std::size_t(image.width * image.height), 1e30), owner_(depth_.size(), -1) {}

    // The original paints back to front ($112650 orders each object's
    // lists from the camera). Here a depth buffer settles only what is in
    // front between objects: within one object (since the last call) later
    // parts paint over earlier ones, as in the original.
    void begin_object() { ++object_; }

    Point to_camera(const Point& p) const {
        const double dx = p[0] - camera_.position[0], dy = p[1] - camera_.position[1], dz = p[2] - camera_.position[2];
        const auto& m = camera_.matrix;
        return {m[0] * dx + m[1] * dy + m[2] * dz, m[3] * dx + m[4] * dy + m[5] * dz, m[6] * dx + m[7] * dy + m[8] * dz};
    }

    // $112B1C: the face's front is towards the camera.
    bool faces_camera(const std::vector<Point>& world) const {
        if (world.size() < 3) return true;
        const auto a = to_camera(world[0]), b = to_camera(world[1]), c = to_camera(world[2]);
        const Point u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const Point n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        return n[0] * a[0] + n[1] * a[1] + n[2] * a[2] < 0;
    }

    void fill(int colour) {
        for (int y = 0; y <= bottom_; ++y)
            for (int x = 0; x < image_.width; ++x) image_.put(x, y, colour);
    }

    void polygon(const std::vector<Point>& world, int colour, bool depth_test) {
        std::vector<Point> view;
        view.reserve(world.size());
        for (const auto& p : world) view.push_back(to_camera(p));
        clip_near(view);
        if (view.size() < 3) return;
        std::vector<Point> screen;
        screen.reserve(view.size());
        for (const auto& v : view) screen.push_back({centre_ - 160 * v[0] / v[2], 90 - 90 * v[1] / v[2], v[2]});
        for (std::size_t i = 1; i + 1 < screen.size(); ++i) triangle(screen[0], screen[i], screen[i + 1], colour, depth_test);
    }

    void line(const Point& from, const Point& to, int colour) {
        auto a = to_camera(from), b = to_camera(to);
        if (a[2] < near_plane && b[2] < near_plane) return;
        if (a[2] < near_plane) a = cut(a, b);
        else if (b[2] < near_plane) b = cut(b, a);
        const double x0 = centre_ - 160 * a[0] / a[2], y0 = 90 - 90 * a[1] / a[2];
        const double x1 = centre_ - 160 * b[0] / b[2], y1 = 90 - 90 * b[1] / b[2];
        const int steps = int(std::min(2000.0, std::max(std::abs(x1 - x0), std::abs(y1 - y0)))) + 1;
        for (int i = 0; i <= steps; ++i) {
            const double t = double(i) / steps;
            plot(int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t), a[2] + (b[2] - a[2]) * t - 1, colour, true);
        }
    }

    void dot(const Point& at, int colour) {
        const auto v = to_camera(at);
        if (v[2] < near_plane) return;
        plot(int(centre_ - 160 * v[0] / v[2]), int(90 - 90 * v[1] / v[2]), v[2] - 1, colour, true);
    }

    int centre() const { return centre_; }
    int bottom() const { return bottom_; }
    // How far the picture reaches sideways from the centre, in the
    // original's 160ths of the depth (160 in its 320 columns).
    double half_width() const { return std::max(centre_, image_.width - 1 - centre_); }

    // A triangle given on the screen (column, row and depth per corner),
    // filled row by row from its top row to its bottom one, each row from
    // its leftmost to its rightmost edge crossing, so even a sliver keeps
    // a pixel a row (as the original's filler, `$124B48`, draws every row
    // a piece spans). One depth, the corners' mean.
    void screen_triangle(const Point& a, const Point& b, const Point& c, int colour) {
        const Point* p[3] = {&a, &b, &c};
        const double low = std::min({a[1], b[1], c[1]}), high = std::max({a[1], b[1], c[1]});
        const double z = (a[2] + b[2] + c[2]) / 3;
        for (int y = int(std::floor(low)); y <= int(std::floor(high)); ++y) {
            const double row = std::clamp(y + 0.5, low, high);
            double left = 1e30, right = -1e30;
            for (int e = 0; e < 3; ++e) {
                const Point& u = *p[e];
                const Point& v = *p[(e + 1) % 3];
                if (row < std::min(u[1], v[1]) || row > std::max(u[1], v[1])) continue;
                const double x = u[1] == v[1] ? u[0] : u[0] + (v[0] - u[0]) * (row - u[1]) / (v[1] - u[1]);
                const double other = u[1] == v[1] ? v[0] : x;
                left = std::min({left, x, other});
                right = std::max({right, x, other});
            }
            if (left > right) continue;
            for (int x = int(std::floor(left)); x <= int(std::floor(right)); ++x) plot(x, y, z, colour, true);
        }
    }
    void screen_dot(int x, int y, double z, int colour) { plot(x, y, z, colour, true); }

    // $123DC4: a filled disc. The midpoint circle's half widths for the
    // rows from the top to the middle (`$145104`), then the same back:
    // 2 x radius rows from `y` - (radius - 1), each 2 half + 1 wide.
    void disc(int x, int y, int radius, int colour, double z) {
        if (radius < 1) return;
        std::vector<int> half(std::size_t(radius) + 1, 0);
        if (radius == 1) {
            half[0] = 1;
        } else {
            int cx = 0, cy = radius, d = 3 - 2 * radius;
            std::size_t top = 0, low = std::size_t(radius);
            while (cx < cy) {
                half[top] = cx;
                half[low] = cy;
                if (d < 0) {
                    d += 4 * cx + 6;
                } else {
                    d += 4 * (cx - cy) + 10;
                    --cy;
                    ++top;
                }
                --low;
                ++cx;
            }
            if (cx == cy) half[top] = cx;
        }
        for (int j = 0; j < 2 * radius; ++j) {
            const int w = half[std::size_t(j < radius ? j : 2 * radius - 1 - j)];
            const int row = y - (radius - 1) + j;
            for (int px = x - w; px <= x + w; ++px) plot(px, row, z, colour, true);
        }
    }

private:
    static Point cut(const Point& in, const Point& out) {
        const double t = (near_plane - in[2]) / (out[2] - in[2]);
        return {in[0] + (out[0] - in[0]) * t, in[1] + (out[1] - in[1]) * t, near_plane};
    }
    static void clip_near(std::vector<Point>& poly) {
        std::vector<Point> out;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const auto& a = poly[i];
            const auto& b = poly[(i + 1) % poly.size()];
            if (a[2] >= near_plane) out.push_back(a);
            if ((a[2] >= near_plane) != (b[2] >= near_plane)) out.push_back(cut(a[2] < near_plane ? a : b, a[2] < near_plane ? b : a));
        }
        poly = std::move(out);
    }
    void plot(int x, int y, double z, int colour, bool test) {
        if (x < 0 || y < 0 || x >= image_.width || y > bottom_) return;
        const auto at = std::size_t(y * image_.width + x);
        auto& d = depth_[at];
        if (test && owner_[at] != object_ && z >= d) return;
        if (test) {
            d = z;
            owner_[at] = object_;
        }
        image_.pixels[std::size_t(y * image_.width + x)] = std::uint8_t(colour & 31);
    }
    void triangle(const Point& a, const Point& b, const Point& c, int colour, bool test) {
        const double area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
        if (std::abs(area) < 1e-9) return;
        const int x0 = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]}))));
        const int x1 = std::min(image_.width - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
        const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
        const int y1 = std::min(bottom_, int(std::ceil(std::max({a[1], b[1], c[1]}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double wa = ((b[0] - px) * (c[1] - py) - (c[0] - px) * (b[1] - py)) / area;
                const double wb = ((c[0] - px) * (a[1] - py) - (a[0] - px) * (c[1] - py)) / area;
                const double wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                // (Depth is linear in 1 / z across the screen.)
                plot(x, y, 1 / (wa / a[2] + wb / b[2] + wc / c[2]), colour, test);
            }
        }
    }

    IndexedImage& image_;
    int centre_, bottom_;
    const Camera& camera_;
    std::vector<double> depth_;
    std::vector<int> owner_; // the object that painted each pixel's depth
    int object_ = 0;
};

} // namespace

data::BuiltPoints road_points(const amiga::LoadedImage& program) {
    // $1197E0, once as the game starts: a list of word offsets from
    // $139B74 (negative: the end), each to runs of 20-byte records in the
    // ground shapes' point blocks: a road's two ends A and B (x, z words;
    // x $FFFF ends the run), then three points it fills in: the corners of
    // the road's strip, from the direction A -> B made 46 long ($119CAE)
    // and turned both ways by about 110 degrees (cos -88/256, sin 241/256):
    // A + one turn, A + the other, B - the first.
    constexpr std::uint32_t table = 0x139B74;
    const auto tables = read_flight_tables(program);
    data::BuiltPoints built;
    for (std::uint32_t entry = table; program.s16(entry) >= 0; entry += 2) {
        for (std::uint32_t at = table + program.u16(entry); program.u16(at) != 0xFFFF; at += 20) {
            const std::int16_t ax = program.s16(at), az = program.s16(at + 2);
            const std::int16_t bx = program.s16(at + 4), bz = program.s16(at + 6);
            const auto v = scaled_vector(tables, 46, std::int16_t(bx - ax), 0, std::int16_t(bz - az));
            const int u = v[0], w = v[2];
            const auto word = [](int value) { return int(std::int16_t(value)); };
            const int x1 = (u * -88 + w * 241) >> 8, z1 = (u * -241 + w * -88) >> 8;
            const int x2 = (u * -88 + w * -241) >> 8, z2 = (u * 241 + w * -88) >> 8;
            built[at + 8] = {word(ax + x1), word(az + z1)};
            built[at + 12] = {word(ax + x2), word(az + z2)};
            built[at + 16] = {word(bx - x1), word(bz - z1)};
        }
    }
    return built;
}

Scene::Scene(const data::Assets& assets, const data::World& world)
    : world_(world), program_(assets.program), explosions_(data::read_explosions(assets.program)) {
    for (int i = 0; i < 3; ++i) smoke_shapes_[std::size_t(i)] = data::read_smoke_shape(assets.program, 33 + i);
    const auto roads = road_points(assets.program);
    for (const auto* map : {&world.pass_a, &world.pass_b, &world.near}) {
        for (const auto& object : map->objects) {
            if (levels_.count(object.type) || ground_.count(object.type)) continue;
            const auto family = data::shape_family(assets.program, object.type);
            if (family == data::ShapeFamily::none) continue;
            if (family == data::ShapeFamily::ground) ground_.emplace(object.type, data::read_ground_levels(assets.program, object.type, roads));
            else levels_.emplace(object.type, data::read_detail_levels(assets.program, object.type));
        }
    }
}

std::optional<std::array<int, 2>> Scene::project(const Camera& camera, const std::array<double, 3>& p, int centre) {
    const double dx = p[0] - camera.position[0], dy = p[1] - camera.position[1], dz = p[2] - camera.position[2];
    const auto& m = camera.matrix;
    const double x = m[0] * dx + m[1] * dy + m[2] * dz, y = m[3] * dx + m[4] * dy + m[5] * dz,
                 z = m[6] * dx + m[7] * dy + m[8] * dz;
    if (z < near_plane) return std::nullopt;
    // ($123894 truncates the quotients, then takes them from the centre.)
    return std::array<int, 2>{centre - int(160 * x / z), 90 - int(90 * y / z)};
}

const std::vector<data::DetailLevel>& Scene::vehicle_levels(int type) const {
    auto found = vehicle_levels_.find(type);
    if (found == vehicle_levels_.end()) found = vehicle_levels_.emplace(type, data::read_detail_levels(program_, type)).first;
    return found->second;
}

bool Scene::in_range(int type, double distance) const {
    for (const auto& level : vehicle_levels(type)) {
        if (distance < level.nearer_than) return level.shape.has_value();
    }
    return false;
}

namespace {

// Paints a shape as the original does ($112650, $11284E): the draw list's
// tree walked from the camera gives the polygon lists in order, each list
// painted part by part; a one-sided face seen from behind ends its list
// ($113836). `origin` is the object's place, `place` a shape point's world
// place; `depth` false paints flat on the ground (no depth test).
template <class Place>
void paint_shape(Raster& r, const data::Shape& shape, const Point& origin, const Place& place, const Point& eye,
                 bool depth) {
    const auto order = shape_draw_order(shape, [&](const auto& points) {
        return r.faces_camera({place(points[0]), place(points[1]), place(points[2])});
    }, origin, eye);
    // Each list's parts in their order.
    struct Part {
        int order;
        int kind; // 0 face, 1 line, 2 dot
        std::size_t index;
    };
    std::vector<std::vector<Part>> lists(std::size_t(std::max(shape.lists, 1)));
    const auto add = [&](const data::ListPlace& at, int kind, std::size_t index) {
        const auto list = at.list >= 0 && at.list < int(lists.size()) ? std::size_t(at.list) : 0;
        lists[list].push_back({at.order, kind, index});
    };
    for (std::size_t i = 0; i < shape.faces.size(); ++i) add(shape.faces[i], 0, i);
    for (std::size_t i = 0; i < shape.lines.size(); ++i) add(shape.lines[i], 1, i);
    for (std::size_t i = 0; i < shape.dots.size(); ++i) add(shape.dots[i], 2, i);
    std::vector<Point> corners;
    for (const int list : order) {
        auto& parts = lists[std::size_t(list)];
        std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) { return a.order < b.order; });
        for (const auto& part : parts) {
            if (part.kind == 1) {
                const auto& line = shape.lines[part.index];
                r.line(place(line.from), place(line.to), line.colour);
                continue;
            }
            if (part.kind == 2) {
                const auto& dot = shape.dots[part.index];
                r.dot(place(dot.at), dot.colour);
                continue;
            }
            const auto& face = shape.faces[part.index];
            corners.clear();
            for (const auto& c : face.corners) corners.push_back(place(c));
            int colour = face.colour;
            if (face.facing != data::Facing::always && !r.faces_camera(corners)) {
                if (face.facing == data::Facing::one_sided) break;
                colour = face.back_colour;
            }
            r.polygon(corners, colour, depth);
        }
    }
}

// The camera-space point of a world point (the original's `$13AEC8` rows,
// world units), and whether it lies in the view's pyramid (`$121AD6`,
// `$1238AE`: |x| and |y| within the depth; sideways as far as the picture
// reaches).
struct Seen {
    Point v;
    bool in{};
};

// $12185E: an exploding record's pieces. Each piece of a burst flies out
// along its direction times the explosion's age (world units) and is a
// triangle turning through the eight shapes by piece number + age, or for
// the cruise missile's first burst a disc growing with the age ($2800 +
// $2800 x age / 6 over the distance, at most 127). Far from the camera (a
// coordinate past $4000 or $40000 units) the cruise missile's explosion is
// worked out 16 or 256 times smaller, which leaves its discs that much
// larger and its pieces' triangles too (their corners are shifted twice).
// Not drawn more than $8000 units above or below the camera ($121880).
// A piece shows only with every corner in the view's pyramid; the pieces'
// rows are one above other points' (89 - 90 y / z, `$121AFC`).
template <class SeenFn>
void draw_explosion(Raster& r, const data::Explosions& e, const Vehicle& v, const Point& eye, const SeenFn& seen) {
    const Point rel{v.position[0] - eye[0], v.position[1] - eye[1], v.position[2] - eye[2]};
    const int age = v.explosion_age;
    if (std::abs(rel[1]) > 0x8000 || age > 15) return;
    struct Part {
        int kind, age;
        Point offset;
    };
    std::vector<Part> parts{{v.explosion_kind, age, {}}};
    int shift = 0, radius = 0;
    if (v.explosion_kind == 3) {
        const double far = std::max({std::abs(rel[0]), std::abs(rel[1]), std::abs(rel[2])});
        if (far > 0x4000) shift = far > 0x40000 ? 8 : 4;
        const int length = int(std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2])) >> shift;
        const int size = 0x2800 * age / 6 + 0x2800;
        radius = length > 0 ? std::min(size / length, 127) : 127;
        const double unit = std::ldexp(1.0, shift);
        parts.push_back({4, std::max(age - 1, 0), {}});
        parts.push_back({5, std::max(age - 2, 0), {20 * unit, 10 * unit, 20 * unit}});
    }
    const double grow = std::ldexp(1.0, shift);
    for (const auto& part : parts) {
        const auto& burst = e.bursts[std::size_t(part.kind - 1)];
        for (std::size_t i = burst.directions.size(); i-- > 0;) {
            const auto& d = burst.directions[i];
            const Point p{v.position[0] + d[0] * part.age + part.offset[0], v.position[1] + d[1] * part.age + part.offset[1],
                          v.position[2] + d[2] * part.age + part.offset[2]};
            std::array<Point, 3> corners{p, p, p};
            const int count = part.kind == 3 ? 1 : 3;
            if (count == 3) {
                const auto& tri = e.pieces[std::size_t(e.piece_of[i + std::size_t(part.age)] & 7)];
                for (std::size_t c = 0; c < 3; ++c)
                    for (std::size_t k = 0; k < 3; ++k) corners[c][k] += tri[c][k] * grow;
            }
            std::array<Point, 3> screen{};
            bool in = true;
            for (int c = 0; c < count && in; ++c) {
                const auto s = seen(corners[std::size_t(c)], false);
                in = s.in;
                if (in)
                    screen[std::size_t(c)] = {double(r.centre() - int(160 * s.v[0] / s.v[2])),
                                              double(89 - int(90 * s.v[1] / s.v[2])), s.v[2]};
            }
            if (!in) continue;
            if (count == 1) r.disc(int(screen[0][0]), int(screen[0][1]), radius, burst.colours[i], screen[0][2]);
            else r.screen_triangle(screen[0], screen[1], screen[2], burst.colours[i]);
        }
    }
}

// $11587C: the trail's discs, $9C0 over the distance (world units) in
// radius, at most 127, odd ring slots in colour 8 and even ones in $A (3
// and $C while it spins); with a length over 1, between two places three
// more at a quarter, a half and three quarters, in the later one's colour.
// The original paints them oldest first when the aircraft comes toward the
// camera (its move against the way from the camera), else newest first;
// the last one painted is an eighth smaller.
template <class SeenFn>
void draw_trail(Raster& r, const Vehicle::Trail& trail, const Point& at, const Point& eye, const SeenFn& seen) {
    const std::size_t n = trail.places.size();
    if (n == 0) return;
    const double toward = trail.move[0] * (at[0] - eye[0]) + trail.move[1] * (at[1] - eye[1]) + trail.move[2] * (at[2] - eye[2]);
    const auto disc = [&](const Point& p, int radius, int colour) {
        const auto s = seen(p, true);
        if (!s.in) return;
        const int x = r.centre() - int(160 * s.v[0] / s.v[2]), y = 90 - int(90 * s.v[1] / s.v[2]);
        if (y <= r.bottom()) r.disc(x, y, radius, colour, s.v[2]);
    };
    Point last{};
    int last_radius = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t i = toward < 0 ? k : n - 1 - k;
        const auto& p = trail.places[i];
        const bool odd = trail.slots[i] & 1;
        const int colour = odd ? (trail.spinning ? 3 : 8) : (trail.spinning ? 0xC : 0xA);
        const int length = int(std::sqrt((p[0] - eye[0]) * (p[0] - eye[0]) + (p[1] - eye[1]) * (p[1] - eye[1]) +
                                         (p[2] - eye[2]) * (p[2] - eye[2])));
        int radius = length > 0 ? std::min(0x9C0 / length, 127) : 127;
        if (k > 0 && trail.length > 1) {
            const auto mix = [&](double f) {
                return Point{last[0] + (p[0] - last[0]) * f, last[1] + (p[1] - last[1]) * f, last[2] + (p[2] - last[2]) * f};
            };
            disc(mix(0.25), last_radius + ((radius - last_radius) >> 2), colour);
            disc(mix(0.5), last_radius + ((radius - last_radius) >> 1), colour);
            disc(mix(0.75), radius + ((last_radius - radius) >> 2), colour);
        }
        last = p;
        last_radius = radius;
        if (k + 1 == n) radius -= radius >> 3;
        disc(p, radius, colour);
    }
}

} // namespace

void Scene::draw(IndexedImage& image, int centre, int bottom, const Camera& camera,
                 const std::vector<Vehicle>& vehicles, const std::vector<Spark>& sparks,
                 const std::vector<SmokeColumn>& smoke) const {
    Raster r(image, centre, bottom, camera);
    const auto& eye = camera.position;
    // Sky, then the land as a plane in colour 4 and the bay's water on it,
    // painted in order.
    r.fill(sky);
    r.polygon({{eye[0] - far_ground, 0, eye[2] - far_ground}, {eye[0] + far_ground, 0, eye[2] - far_ground},
               {eye[0] + far_ground, 0, eye[2] + far_ground}, {eye[0] - far_ground, 0, eye[2] + far_ground}},
              land, false);
    std::vector<Point> corners;
    // $11F27C: above height $3F8 the coarse grid's cells around the camera
    // first, then the fine grid's, in a window that shrinks with height.
    const double height = eye[1] * 256; // (32-bit units, as the original compares)
    const auto in = [](const std::vector<std::array<int, 2>>& window, int cx, int cz, const std::array<int, 2>& cell) {
        for (const auto& o : window)
            if (cell[0] == cx + o[0] && cell[1] == cz + o[1]) return true;
        return false;
    };
    if (height > 0x3F8) {
        const auto& coarse = world_.coarse_water;
        const auto x32 = std::int64_t(eye[0] * 256), z32 = std::int64_t(eye[2] * 256);
        const int cx = int(x32 >> 28) + data::CoarseWater::origin, cz = int(z32 >> 28) + data::CoarseWater::origin;
        const int qx = 3 - int((x32 >> 26) & 3), qz = 3 - int((z32 >> 26) & 3);
        const auto& window = coarse.windows[std::size_t(qz * 4 + qx)];
        for (std::size_t i = 0; i < coarse.polygons.size(); ++i) {
            if (!in(window, cx, cz, coarse.cell_of[i])) continue;
            corners.clear();
            for (const auto& p : coarse.polygons[i].points) corners.push_back({p[0], 0, p[1]});
            r.polygon(corners, water, false);
        }
    }
    const auto* water_window = height > 0x10000 ? &world_.window_5x5 : height > 0x9000 ? &world_.window_3x3 : nullptr;
    const int fx = int(std::floor(eye[0] / world_.water.cell_size)), fz = int(std::floor(eye[2] / world_.water.cell_size));
    for (std::size_t i = 0; i < world_.water.polygons.size(); ++i) {
        const auto& polygon = world_.water.polygons[i];
        if (polygon.min_height && eye[1] < *polygon.min_height) continue; // the rest only from that height up
        if (water_window && !in(*water_window, fx, fz, world_.water.cell_of[i])) continue;
        corners.clear();
        for (const auto& p : polygon.points) corners.push_back({p[0], 0, p[1]});
        r.polygon(corners, water, false);
    }
    // The scenery in the builder's window. Each object shows the level of
    // detail for its distance; each pass draws only the models meant for it
    // ($111DD2): the coarse maps the ones marked for them, the fine map the
    // others. The flat ground shapes are drawn by every pass. With high
    // detail (an enhancement) every object takes the nearest level its pass
    // draws, at any distance in the window and at any height.
    struct Placed {
        const data::MapObject* object;
        const data::Shape* shape;
        double distance;
    };
    std::array<std::vector<Placed>, 3> passes;
    std::size_t pass = 0;
    for (const auto* map : {&world_.pass_a, &world_.pass_b, &world_.near}) {
        const bool near_pass = map == &world_.near;
        const double window = near_pass ? fine_window : coarse_window;
        for (const auto& object : map->objects) {
            const double dx = object.x - eye[0], dz = object.z - eye[2];
            if (std::abs(dx) > window || std::abs(dz) > window) continue;
            const data::Shape* chosen = nullptr;
            const double distance = std::sqrt(dx * dx + eye[1] * eye[1] + dz * dz);
            if (const auto g = ground_.find(object.type); g != ground_.end()) {
                if (!high_detail_ && eye[1] > data::ground_ceiling(program_, object.type)) continue;
                for (const auto& level : g->second) {
                    if (!high_detail_ && distance >= level.nearer_than) continue;
                    if (level.shape) chosen = &*level.shape;
                    break;
                }
            } else if (const auto l = levels_.find(object.type); l != levels_.end()) {
                for (const auto& level : l->second) {
                    if (high_detail_) {
                        if (!level.shape || level.shape->coarse_pass == near_pass) continue;
                    } else if (distance >= level.nearer_than) {
                        continue;
                    }
                    if (level.shape && level.shape->coarse_pass != near_pass) chosen = &*level.shape;
                    break;
                }
            }
            if (chosen) passes[pass].push_back({&object, chosen, distance});
        }
        ++pass;
    }
    // What lies flat on the ground (the ground shapes, the airfields'
    // paving) is painted first, pass by pass and far to near within a pass,
    // with no depth test: it is all on one level, so only its order counts
    // (hypothesis: the original's lists run far to near). Then the
    // aircraft's shadows, then everything standing.
    const auto scenery_place = [](const data::MapObject& object, const data::Shape& shape) {
        return [&object, &shape](const data::ShapePoint& p) {
            return Point{object.x + p.x * shape.scale, p.y * shape.scale, object.z + p.z * shape.scale};
        };
    };
    for (auto& placed : passes) {
        std::stable_sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.distance > b.distance; });
        for (const auto& p : placed) {
            if (!p.shape->flat) continue;
            r.begin_object();
            paint_shape(r, *p.shape, {p.object->x, 0, p.object->z}, scenery_place(*p.object, *p.shape), eye, false);
        }
    }
    const auto vehicle_shape = [&](const Vehicle& v) -> const data::Shape* {
        const double dx = v.position[0] - eye[0], dy = v.position[1] - eye[1], dz = v.position[2] - eye[2];
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        for (const auto& level : vehicle_levels(v.type)) {
            if (high_detail_) {
                if (level.shape) return &*level.shape;
                continue;
            }
            if (distance >= level.nearer_than) continue;
            return level.shape ? &*level.shape : nullptr;
        }
        return nullptr;
    };
    const auto vehicle_place = [](const Vehicle& v, const data::Shape& shape) {
        return [&v, &shape](const data::ShapePoint& p) {
            const auto& b = v.body;
            const double x = p.x * shape.scale, y = p.y * shape.scale, z = p.z * shape.scale;
            return Point{v.position[0] + (b[0] * x + b[3] * y + b[6] * z) / 16384.0,
                         v.position[1] + (b[1] * x + b[4] * y + b[7] * z) / 16384.0,
                         v.position[2] + (b[2] * x + b[5] * y + b[8] * z) / 16384.0};
        };
    };
    for (const auto& v : vehicles) {
        const auto* shape = vehicle_shape(v);
        if (!shape || !v.model || !v.shadow || v.position[1] > shape->shadow_height) continue;
        const auto place = vehicle_place(v, *shape);
        for (const auto& shadow : shape->shadows) {
            corners.clear();
            for (const auto& c : shadow) {
                auto p = place(c);
                p[1] = 0;
                corners.push_back(p);
            }
            r.polygon(corners, 0, false);
        }
    }
    for (const auto& placed : passes) {
        for (const auto& p : placed) {
            if (p.shape->flat) continue;
            r.begin_object();
            paint_shape(r, *p.shape, {p.object->x, 0, p.object->z}, scenery_place(*p.object, *p.shape), eye, true);
        }
    }
    // The moving objects ($10FC0C): their shape for the distance, turned
    // by their body matrix (its rows map the world into the object's axes,
    // so its columns map the shape back out).
    const auto seen = [&](const Point& p, bool strict) {
        const auto v = r.to_camera(p);
        const double side = std::abs(v[0]) * 160 / r.half_width();
        const bool in = strict ? v[2] > 0 && side < v[2] && std::abs(v[1]) < v[2]
                               : v[2] > 0 && side <= v[2] && std::abs(v[1]) <= v[2];
        return Seen{v, in};
    };
    for (const auto& v : vehicles) {
        const auto* chosen = vehicle_shape(v);
        if (!chosen) continue;
        r.begin_object();
        if (v.explosion_age >= 0) draw_explosion(r, explosions_, v, eye, seen);
        r.begin_object();
        if (v.trail) draw_trail(r, *v.trail, v.position, eye, seen);
        if (!v.model) continue;
        r.begin_object();
        paint_shape(r, *chosen, v.position, vehicle_place(v, *chosen), eye, true);
    }
    // The smoke slots ($111D5C for types 33-35): near, the patch and the
    // column of discs for the slot's timer; farther, a two-pixel dot.
    for (const auto& column : smoke) {
        if (column.type < 33 || column.type > 35) continue;
        r.begin_object();
        const auto& shape = smoke_shapes_[std::size_t(column.type - 33)];
        const auto& at = column.position;
        const double dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const auto place = [&](const data::ShapePoint& p) {
            return Point{at[0] + p.x * shape.scale, at[1] + p.y * shape.scale, at[2] + p.z * shape.scale};
        };
        if (distance < shape.near_limit) {
            if (shape.patch) {
                for (const auto& face : shape.patch->faces) {
                    corners.clear();
                    for (const auto& c : face.corners) corners.push_back(place(c));
                    r.polygon(corners, face.colour, true);
                }
                for (const auto& line : shape.patch->lines) r.line(place(line.from), place(line.to), line.colour);
            }
            if (column.timer < 0 || column.timer > 127) continue;
            for (const auto& disc : shape.column[std::size_t(column.timer)]) {
                // $11582A: the radius over the distance (the camera-space
                // length, times the model's scale shift; `$1108C4`), at most
                // 127; $1238A0 places it.
                const auto s = seen(place(disc.at), true);
                if (!s.in) continue;
                const auto& v = s.v;
                const int length = int(std::min(std::ldexp(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), shape.distance_shift), 32767.0));
                const int radius = length > 0 ? std::min(disc.radius / length, 127) : 127;
                const int x = r.centre() - int(160 * v[0] / v[2]), y = 90 - int(90 * v[1] / v[2]);
                if (y > r.bottom()) continue;
                r.disc(x, y, radius, disc.colour, v[2]);
            }
        } else if (shape.far_dot && distance < shape.far_limit) {
            const auto s = seen(place(shape.far_dot->at), true);
            if (!s.in) continue;
            const int x = r.centre() - int(160 * s.v[0] / s.v[2]), y = 90 - int(90 * s.v[1] / s.v[2]);
            // ($12426E: two pixels, the one to the left too.)
            r.screen_dot(x - 1, y, s.v[2], shape.far_dot->colour);
            r.screen_dot(x, y, s.v[2], shape.far_dot->colour);
        }
    }
    static constexpr int burst[3][6] = {{-3, 3, -3, 3, -3, 3}, {2, 1, -2, -2, -1, 2}, {-3, -3, -3, 3, 3, 3}};
    for (const auto& s : sparks) {
        r.begin_object();
        const auto& p = s.position;
        if (!s.burst) {
            r.dot(p, s.colour);
            continue;
        }
        for (const auto& b : burst)
            r.line({p[0] + b[0], p[1] + b[1], p[2] + b[2]}, {p[0] + b[3], p[1] + b[4], p[2] + b[5]}, s.colour);
    }
}

} // namespace interceptor::game
