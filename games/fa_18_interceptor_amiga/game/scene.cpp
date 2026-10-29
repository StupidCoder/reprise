#include "fa_18_interceptor_amiga/game/scene.hpp"
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
          depth_(std::size_t(image.width * image.height), 1e30) {}

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
        auto& d = depth_[std::size_t(y * image_.width + x)];
        if (test && z >= d) return;
        if (test) d = z;
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
                plot(x, y, wa * a[2] + wb * b[2] + wc * c[2], colour, test);
            }
        }
    }

    IndexedImage& image_;
    int centre_, bottom_;
    const Camera& camera_;
    std::vector<double> depth_;
};

} // namespace

Scene::Scene(const data::Assets& assets, const data::World& world) : world_(world) {
    for (const auto* map : {&world.pass_a, &world.pass_b, &world.near}) {
        for (const auto& object : map->objects) {
            if (levels_.count(object.type) || ground_.count(object.type)) continue;
            const auto family = data::shape_family(assets.program, object.type);
            if (family == data::ShapeFamily::none) continue;
            if (family == data::ShapeFamily::ground) ground_.emplace(object.type, data::read_ground_shape(assets.program, object.type));
            else levels_.emplace(object.type, data::read_detail_levels(assets.program, object.type));
        }
    }
}

void Scene::draw(IndexedImage& image, int centre, int bottom, const Camera& camera) const {
    Raster r(image, centre, bottom, camera);
    const auto& eye = camera.position;
    // Sky, then the land as a plane in colour 4 and the bay's water on it,
    // painted in order.
    r.fill(sky);
    r.polygon({{eye[0] - far_ground, 0, eye[2] - far_ground}, {eye[0] + far_ground, 0, eye[2] - far_ground},
               {eye[0] + far_ground, 0, eye[2] + far_ground}, {eye[0] - far_ground, 0, eye[2] + far_ground}},
              land, false);
    std::vector<Point> corners;
    for (const auto& polygon : world_.water.polygons) {
        if (polygon.min_height && eye[1] < *polygon.min_height) continue; // the rest only from that height up
        corners.clear();
        for (const auto& p : polygon.points) corners.push_back({p[0], 0, p[1]});
        r.polygon(corners, water, false);
    }
    // The scenery in the builder's window. Each object shows the level of
    // detail for its distance; each pass draws only the models meant for it
    // ($111DD2): the coarse maps the ones marked for them, the fine map the
    // others. The flat ground shapes are drawn by every pass.
    for (const auto* map : {&world_.pass_a, &world_.pass_b, &world_.near}) {
        const bool near_pass = map == &world_.near;
        const double window = near_pass ? fine_window : coarse_window;
        for (const auto& object : map->objects) {
            const double dx = object.x - eye[0], dz = object.z - eye[2];
            if (std::abs(dx) > window || std::abs(dz) > window) continue;
            const data::Shape* chosen = nullptr;
            if (const auto g = ground_.find(object.type); g != ground_.end()) {
                chosen = &g->second;
            } else if (const auto l = levels_.find(object.type); l != levels_.end()) {
                const double distance = std::sqrt(dx * dx + eye[1] * eye[1] + dz * dz);
                for (const auto& level : l->second) {
                    if (distance >= level.nearer_than) continue;
                    if (level.shape && level.shape->coarse_pass != near_pass) chosen = &*level.shape;
                    break;
                }
            }
            if (!chosen) continue;
            const auto& shape = *chosen;
            const auto place = [&](const data::ShapePoint& p) {
                return Point{object.x + p.x * shape.scale, p.y * shape.scale, object.z + p.z * shape.scale};
            };
            for (const auto& face : shape.faces) {
                corners.clear();
                for (const auto& c : face.corners) corners.push_back(place(c));
                int colour = face.colour;
                if (face.facing != data::Facing::always && !r.faces_camera(corners)) {
                    if (face.facing == data::Facing::one_sided) continue;
                    colour = face.back_colour;
                }
                r.polygon(corners, colour, true);
            }
            for (const auto& line : shape.lines) r.line(place(line.from), place(line.to), line.colour);
            for (const auto& dot : shape.dots) r.dot(place(dot.at), dot.colour);
        }
    }
}

} // namespace interceptor::game
