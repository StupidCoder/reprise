// F/A-18 Interceptor research tool: imports the disk image into a content
// folder and exports the decoded data (games/fa_18_interceptor_amiga/game.md).
//
//   fa_18_interceptor_assets import <image.adf> <content folder>
//   fa_18_interceptor_assets export <content folder> <output folder>
//   fa_18_interceptor_assets viewcheck <content folder> <camera.txt> <out.png>
//   fa_18_interceptor_assets flightcheck <content folder> <capture.jsonl>
//   fa_18_interceptor_assets soundcheck <content folder> <capture.jsonl>
//   fa_18_interceptor_assets fly <content folder> <out prefix> <tick:keys ...>
//
// fly (research): runs the game module headless from the start of free
// flight with scripted controls - `tick:keys` holds `keys` from that tick
// (u d l r stick, + - throttle, digits, g gear, b brake, s speed brake,
// < > rudder; `.` none; `!png` saves a picture at that tick instead;
// `!wav` anywhere saves the run's sound as <prefix>.wav).
//
// flightcheck (research): runs the flight model from every frame of a
// capture (harness/flightcap.py: the player's object record and the
// globals at the start of each frame) and compares the result with the
// next frame's record, field by field.
//
// soundcheck (research): runs the game's sound rules frame by frame on a
// capture made with `flightcap.py --calls` (the flight from each record,
// its sound events, the sound update with the end state) and compares the
// calls of the original's sound routines with the ones logged.
//
// viewcheck (research): draws the world from the decoded maps, water grid
// and shapes for a camera captured from the original (harness/viewgrab.py:
// camera matrix $13AEC8, fine cell, offset $13AD52) with the original's
// projection ($123894: x = 159 - 160 x / z, y = 90 - 90 y / z, a 320 x 180
// window), for comparison with the original's frame (tools/viewcmp.py).
//
// The export writes the pictures, every screen-text block (and the main
// menu put together), the cockpit messages, fonts and palettes as PNG, the
// sounds as WAV (8-bit, at the rate their load-time period gives), and a
// listing of the texts, 3D shapes and sounds on stdout.

#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/preview.hpp"
#include "fa_18_interceptor_amiga/data/shapes.hpp"
#include "fa_18_interceptor_amiga/data/world.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include "fa_18_interceptor_amiga/game/autopilot.hpp"
#include "fa_18_interceptor_amiga/game/collisions.hpp"
#include "fa_18_interceptor_amiga/game/replay.hpp"
#include "fa_18_interceptor_amiga/game/scene.hpp"
#include "fa_18_interceptor_amiga/game/carrier.hpp"
#include "fa_18_interceptor_amiga/game/object_step.hpp"
#include "fa_18_interceptor_amiga/game/object_world.hpp"
#include "fa_18_interceptor_amiga/game/radar.hpp"
#include "fa_18_interceptor_amiga/game/shot_hits.hpp"
#include "fa_18_interceptor_amiga/game/world_objects.hpp"
#include "fa_18_interceptor_amiga/game/cockpit.hpp"
#include "fa_18_interceptor_amiga/game/messages.hpp"
#include "fa_18_interceptor_amiga/game/module.hpp"
#include "fa_18_interceptor_amiga/game/sound_driver.hpp"
#include "fa_18_interceptor_amiga/game/sounds.hpp"
#include "fa_18_interceptor_amiga/game/weapons.hpp"
#include "fa_18_interceptor_amiga/game/text_screen.hpp"
#include "fa_18_interceptor_amiga/game/trigger_areas.hpp"
#include "fa_18_interceptor_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

namespace {

constexpr double pal_clock = 3546895.0; // Paula's PAL clock

void save_wav(const std::filesystem::path& file, std::span<const std::int8_t> samples, int rate) {
    std::vector<std::uint8_t> wav;
    const auto put = [&](std::uint32_t v, int n) { for (int k = 0; k < n; ++k) wav.push_back(std::uint8_t(v >> (8 * k))); };
    const auto bytes = std::uint32_t(samples.size());
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'}); put(36 + bytes, 4);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(16, 4); put(1, 2); put(1, 2); put(std::uint32_t(rate), 4); put(std::uint32_t(rate), 4); put(1, 2); put(8, 2);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'}); put(bytes, 4);
    for (auto s : samples) wav.push_back(std::uint8_t(s + 128)); // WAV 8-bit is unsigned
    platform::write_file(file, wav);
}

std::string file_name(std::string path) {
    for (auto& c : path) if (c == '/') c = '_';
    return path;
}

// --- viewcheck ---------------------------------------------------------------

struct Camera {
    std::array<double, 9> matrix{}; // rows x, y, z; 1.0 = 256
    double x{}, y{}, z{};           // world units
    int bottom{};                   // last row of the 3D window ($13AC74)
};

Camera read_camera(const std::filesystem::path& file) {
    std::ifstream in(file);
    Camera camera;
    std::string line;
    int fine_x = 0, fine_z = 0;
    long ox = 0, oy = 0, oz = 0;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "matrix") for (auto& v : camera.matrix) { words >> v; v /= 256; }
        if (key == "fine") words >> fine_x >> fine_z;
        if (key == "offset") words >> ox >> oy >> oz;
        if (key == "bottom") words >> camera.bottom;
    }
    // $13AD52: minus the camera's offset from its fine cell corner, x 256
    camera.x = fine_x * 16384.0 - ox / 256.0;
    camera.y = -oy / 256.0;
    camera.z = fine_z * 16384.0 - oz / 256.0;
    return camera;
}

class View {
public:
    View(const Camera& camera, const interceptor::data::Palette& palette)
        : camera_(camera), palette_(palette), image_(320, 180), depth_(320 * 180, 1e30) {}

    void fill(int colour) {
        for (int y = 0; y <= std::min(179, camera_.bottom); ++y)
            for (int x = 0; x < 320; ++x) image_.put(x, y, interceptor::data::to_color(palette_[std::size_t(colour)]));
    }
    // A polygon in world units; `depth_test` off for the ground layers,
    // which the original paints in order.
    // $112B1C: true when the face's front is towards the camera (its first
    // three corners in camera space).
    bool faces_camera(const std::vector<std::array<double, 3>>& world) const {
        if (world.size() < 3) return true;
        const auto a = to_camera(world[0]), b = to_camera(world[1]), c = to_camera(world[2]);
        const std::array<double, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const std::array<double, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        return n[0] * a[0] + n[1] * a[1] + n[2] * a[2] < 0;
    }
    void polygon(const std::vector<std::array<double, 3>>& world, int colour, bool depth_test) {
        std::vector<std::array<double, 3>> view;
        for (const auto& p : world) view.push_back(to_camera(p));
        clip_near(view);
        if (view.size() < 3) return;
        std::vector<std::array<double, 3>> screen;
        for (const auto& v : view) screen.push_back({159 - 160 * v[0] / v[2], 90 - 90 * v[1] / v[2], v[2]});
        const auto c = interceptor::data::to_color(palette_[std::size_t(colour & 31)]);
        for (std::size_t i = 1; i + 1 < screen.size(); ++i) triangle(screen[0], screen[i], screen[i + 1], c, depth_test);
    }
    void line(std::array<double, 3> a, std::array<double, 3> b, int colour) {
        auto va = to_camera(a), vb = to_camera(b);
        if (va[2] < near_ && vb[2] < near_) return;
        if (va[2] < near_) va = cut(va, vb); else if (vb[2] < near_) vb = cut(vb, va);
        const auto c = interceptor::data::to_color(palette_[std::size_t(colour & 31)]);
        const double x0 = 159 - 160 * va[0] / va[2], y0 = 90 - 90 * va[1] / va[2];
        const double x1 = 159 - 160 * vb[0] / vb[2], y1 = 90 - 90 * vb[1] / vb[2];
        const int steps = int(std::min(2000.0, std::max(std::abs(x1 - x0), std::abs(y1 - y0)))) + 1;
        for (int i = 0; i <= steps; ++i) {
            const double t = double(i) / steps;
            plot(int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t), va[2] + (vb[2] - va[2]) * t - 1, c, true);
        }
    }
    void dot(std::array<double, 3> p, int colour) {
        const auto v = to_camera(p);
        if (v[2] < near_) return;
        plot(int(159 - 160 * v[0] / v[2]), int(90 - 90 * v[1] / v[2]), v[2] - 1,
             interceptor::data::to_color(palette_[std::size_t(colour & 31)]), true);
    }
    const platform::Image& image() const { return image_; }

private:
    std::array<double, 3> to_camera(const std::array<double, 3>& p) const {
        const double dx = p[0] - camera_.x, dy = p[1] - camera_.y, dz = p[2] - camera_.z;
        const auto& m = camera_.matrix;
        return {m[0] * dx + m[1] * dy + m[2] * dz, m[3] * dx + m[4] * dy + m[5] * dz, m[6] * dx + m[7] * dy + m[8] * dz};
    }
    std::array<double, 3> cut(std::array<double, 3> in, std::array<double, 3> out) const {
        const double t = (near_ - in[2]) / (out[2] - in[2]);
        return {in[0] + (out[0] - in[0]) * t, in[1] + (out[1] - in[1]) * t, near_};
    }
    void clip_near(std::vector<std::array<double, 3>>& poly) const {
        std::vector<std::array<double, 3>> out;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const auto& a = poly[i];
            const auto& b = poly[(i + 1) % poly.size()];
            if (a[2] >= near_) out.push_back(a);
            if ((a[2] >= near_) != (b[2] >= near_)) out.push_back(cut(a[2] < near_ ? a : b, a[2] < near_ ? b : a));
        }
        poly = std::move(out);
    }
public:
    int high_pixels{}; // research: pixels drawn in the top rows (VIEWCHECK_DEBUG)
private:
    void plot(int x, int y, double z, platform::Color c, bool test) {
        if (x < 0 || y < 0 || x >= 320 || y >= 180 || y > camera_.bottom) return;
        if (y < 30) ++high_pixels;
        auto& d = depth_[std::size_t(y * 320 + x)];
        if (test && z >= d) return;
        if (test) d = z;
        image_.put(x, y, c);
    }
    void triangle(std::array<double, 3> a, std::array<double, 3> b, std::array<double, 3> c, platform::Color col, bool test) {
        const double area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
        if (std::abs(area) < 1e-9) return;
        const int x0 = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]}))));
        const int x1 = std::min(319, int(std::ceil(std::max({a[0], b[0], c[0]}))));
        const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
        const int y1 = std::min(179, int(std::ceil(std::max({a[1], b[1], c[1]}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double wa = ((b[0] - px) * (c[1] - py) - (c[0] - px) * (b[1] - py)) / area;
                const double wb = ((c[0] - px) * (a[1] - py) - (a[0] - px) * (c[1] - py)) / area;
                const double wc = 1 - wa - wb;
                if (wa < 0 || wb < 0 || wc < 0) continue;
                plot(x, y, wa * a[2] + wb * b[2] + wc * c[2], col, test);
            }
    }

    Camera camera_;
    const interceptor::data::Palette& palette_;
    platform::Image image_;
    std::vector<double> depth_;
    double near_{4};
};

int view_check(const std::filesystem::path& content, const std::filesystem::path& camera_file,
               const std::filesystem::path& out) {
    using namespace interceptor::data;
    const auto assets = load_assets(content);
    const auto world = read_world(assets.program);
    const auto camera = read_camera(camera_file);
    const auto& palette = assets.palettes.at(flight_palette);
    View view(camera, palette);
    // Sky above, land below: the ground plane in colour 4 (hypothesis from
    // the captures: green land under the sea polygons), drawn as a large
    // square around the camera.
    view.fill(5);
    const double far = 400000;
    view.polygon({{camera.x - far, 0, camera.z - far}, {camera.x + far, 0, camera.z - far},
                  {camera.x + far, 0, camera.z + far}, {camera.x - far, 0, camera.z + far}}, 4, false);
    for (const auto& water : world.water.polygons) {
        if (water.min_height && camera.y < *water.min_height) continue; // the rest only from that height up
        std::vector<std::array<double, 3>> corners;
        for (const auto& p : water.points) corners.push_back({p[0], 0, p[1]});
        view.polygon(corners, 6, false);
    }
    // Objects of all three maps, with their shapes.
    std::map<int, Shape> shapes;
    const auto shape_of = [&](int type) -> const Shape* {
        auto found = shapes.find(type);
        if (found == shapes.end()) {
            const auto family = shape_family(assets.program, type);
            if (family == ShapeFamily::none) return nullptr;
            found = shapes.emplace(type, family == ShapeFamily::ground ? read_ground_shape(assets.program, type)
                                                                      : read_shape(assets.program, type)).first;
        }
        return &found->second;
    };
    int drawn = 0;
    const char* skip = std::getenv("VIEWCHECK_SKIP"); // research: maps to leave out, e.g. "ab"
    const char* names = "abc";
    int map_index = -1;
    for (const auto* map : {&world.pass_a, &world.pass_b, &world.near}) {
        ++map_index;
        if (skip && std::strchr(skip, names[map_index])) continue;
        for (const auto& object : map->objects) {
            const auto* shape = shape_of(object.type);
            if (shape == nullptr) continue;
            const auto place = [&](const ShapePoint& p) {
                return std::array<double, 3>{object.x + p.x * shape->scale, p.y * shape->scale, object.z + p.z * shape->scale};
            };
            for (const auto& face : shape->faces) {
                std::vector<std::array<double, 3>> corners;
                for (const auto& c : face.corners) corners.push_back(place(c));
                int colour = face.colour;
                if (face.facing != Facing::always && !view.faces_camera(corners)) {
                    if (face.facing == Facing::one_sided) continue;
                    colour = face.back_colour;
                }
                view.polygon(corners, colour, true);
            }
            for (const auto& line : shape->lines) view.line(place(line.from), place(line.to), line.colour);
            for (const auto& dot : shape->dots) view.dot(place(dot.at), dot.colour);
            if (std::getenv("VIEWCHECK_DEBUG") && view.high_pixels > 0) {
                std::printf("map %c type %d at %.0f %.0f: %d pixels in the top rows\n", names[map_index], object.type,
                            object.x, object.z, view.high_pixels);
                view.high_pixels = 0;
            }
            ++drawn;
        }
    }
    // Low-res pixels drawn twice, as harness/screen.py shows the original.
    platform::Image sheet(640, 180);
    for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 640; ++x) sheet.put(x, y, view.image().pixels[std::size_t(y * 320 + x / 2)]);
    platform::save_png(sheet, out);
    std::printf("camera %.0f %.0f %.0f, %d objects placed\n", camera.x, camera.y, camera.z, drawn);
    return 0;
}

// --- flightcheck -------------------------------------------------------------

struct Frame {
    int number{};
    std::vector<std::uint8_t> object, globals;
};

std::vector<std::uint8_t> hex_field(const std::string& line, const std::string& key) {
    const auto at = line.find("\"" + key + "\": \"");
    if (at == std::string::npos) throw std::runtime_error("capture line without " + key);
    std::vector<std::uint8_t> bytes;
    for (auto i = at + key.size() + 5; i + 1 < line.size() && line[i] != '"'; i += 2) {
        bytes.push_back(std::uint8_t(std::stoi(line.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

struct Record {
    const std::vector<std::uint8_t>& b;
    std::uint8_t u8(int at) const { return b[std::size_t(at)]; }
    std::int8_t s8(int at) const { return std::int8_t(b[std::size_t(at)]); }
    std::uint16_t u16(int at) const { return std::uint16_t(b[std::size_t(at)] << 8 | b[std::size_t(at + 1)]); }
    std::int16_t s16(int at) const { return std::int16_t(u16(at)); }
    std::int32_t s32(int at) const { return std::int32_t(std::uint32_t(u16(at)) << 16 | u16(at + 2)); }
};

// `$13AA2A` (the stage's first list entry) as a research address: newer
// recordings log where the object lists (`$11DFB2`) were loaded ("lists").
std::uint32_t list_start_of(const std::string& line, const Record& g) {
    const auto running = std::uint32_t(g.s32(0x13AA2A - 0x13A9E0));
    const auto at = line.find("\"lists\": \"");
    if (at == std::string::npos) return running;
    const auto loaded = std::uint32_t(std::stoul(line.substr(at + 10, 8), nullptr, 16));
    return running - loaded + 0x11DFB2;
}

interceptor::game::Aircraft aircraft_from(const Frame& f) {
    const Record o{f.object};
    const Record g{f.globals};
    interceptor::game::Aircraft a;
    a.x = o.s32(0x14); a.y = o.s32(0x18); a.z = o.s32(0x1C);
    a.vx = o.s32(0x3E); a.vy = o.s32(0x42); a.vz = o.s32(0x46);
    a.attitude = {o.s16(0x66), o.s16(0x68), o.s16(0x6A)};
    a.pitch_rate = o.s16(0x56); a.yaw_rate = o.s16(0x58); a.roll_rate = o.s16(0x5A);
    a.pitch_input = o.s8(0x28); a.rudder_input = o.s8(0x29); a.roll_input = o.s8(0x2A);
    a.throttle = o.s8(0x2B);
    a.throttle_target = g.s8(0x13AB60 - 0x13A9E0);
    a.throttle_steps = o.u8(0x39);
    a.controls = o.u8(0x65);
    a.speed = o.s16(0x6C); a.airspeed = o.s16(0x6E);
    a.fuel = o.s32(0x72);
    a.sink = o.s16(0x76); a.climb_speed = o.s16(0x78);
    a.ground_drag = o.s16(0x26);
    a.bank_sine = o.s16(0x22); a.bank_cosine = o.s16(0x24);
    a.bank_side = o.s16(0x50); a.bank_side_was = o.s16(0x52); a.bank_turn = o.s16(0x54);
    a.bank_turn_limit = o.s16(0x7E);
    a.gear = o.u8(0x7C);
    a.deck_height = o.s16(0x4E);
    for (int i = 0; i < 9; ++i) {
        a.body[std::size_t(i)] = o.s16(0x80 + 2 * i);
        a.view[std::size_t(i)] = o.s16(0x92 + 2 * i);
    }
    a.state = o.u16(0); a.motion = o.u16(2);
    a.surface = o.u8(4); a.handling = o.u8(0x20);
    a.type = o.u8(0x62); a.role = o.u8(5); a.timer = o.s16(0x4C);
    a.trail = o.u8(0x3D);
    a.stall_warning = g.u8(0x13AE43 - 0x13A9E0) & 0x80;
    a.fuel_warning = std::uint8_t(g.u8(0x13AE43 - 0x13A9E0) & 0x43);
    a.supersonic = g.u16(0x13ABC2 - 0x13A9E0) & 0x4000;
    a.gear_speed_warned = g.u8(0x13AB36 - 0x13A9E0) != 0;
    return a;
}

// A record of a recording as the port's object.
interceptor::game::WorldObject world_object_from(int record, const std::vector<std::uint8_t>& bytes,
                                                 const std::vector<std::uint8_t>& globals) {
    using namespace interceptor;
    const Record o{bytes};
    game::WorldObject w;
    w.craft = aircraft_from(Frame{0, bytes, globals});
    w.craft.player = false;
    w.record = record;
    w.scale = o.u8(0x7D) & 15;
    w.group = o.u8(0x5E);
    w.area = o.s8(0x5D);
    w.course.target = {o.s16(0x2C), o.s16(0x2E), o.s16(0x30), o.s16(0x32), o.s32(0x34)};
    w.course.target_record = o.u8(0x38);
    w.course.path = o.u8(0x3A);
    w.course.distance = o.s16(0x4A);
    w.course.behaviour = o.u8(0x7A);
    w.pilot = o.u8(0x64);
    w.weapons = o.u8(0x63);
    w.stores = o.u8(0x5F);
    w.damage = o.u8(0x3C);
    return w;
}

// The steps of a mission's other aircraft in a frame, after the player's
// (as the module's `objects_tick`).
void step_objects(std::vector<interceptor::game::WorldObject>& objects, interceptor::game::Aircraft& player,
                  const interceptor::game::ObjectStep& step, const interceptor::game::Autopilot& autopilot,
                  const interceptor::game::FlightTables& tables, std::uint8_t mode, std::int8_t level, std::uint32_t list,
                  std::int16_t player_distance = 0x7FFF, int locked = -1) {
    using namespace interceptor;
    game::Records records{&player, player_distance, &objects};
    game::StepWorld world;
    world.locked = locked;
    world.mode = mode;
    world.level = level;
    world.list = list;
    for (auto& o : objects) {
        auto& a = o.craft;
        if (!o.active() || (a.type & 0xF0) != 0x10) continue;
        step.before_flight(o, records, world);
        if (a.state & 0x0400) { // ($11A398, as the module's)
            if (a.timer <= 0) {
                a.state &= ~0x0400;
                if ((a.motion & game::flag::on_ground) || (a.state & 0x8000)) {
                    a.state &= ~(0x0040 | 0x4000 | 0x2000);
                    a.timer = 0x14;
                    continue;
                }
            }
            a.state &= ~0x2000;
        }
        if (!(a.state & 0x1000) && !(a.state & 0x0100)) continue;
        game::PilotWorld pw;
        pw.mode = mode;
        pw.level = level;
        autopilot.fly(a, o.pilot, pw, &o.course);
        game::FlightControls c;
        c.recorded = a.controls;
        const auto roll_before = a.attitude.roll;
        game::FlightRules rules;
        const std::array<std::int32_t, 3> before{a.x, a.y, a.z};
        if (a.state & 0x1000) game::fly(a, c, rules, tables);
        game::Autopilot::level_out(a, o.pilot, roll_before);
        if (a.state & 0x0400) continue;
        if ((a.handling & 0x02) && (a.motion & game::flag::on_ground) && !(a.state & 0x0004)) {
            a.state = std::uint16_t((a.state & ~0x1000) | 0x0400);
            a.timer = 15;
        }
        (void)before;
    }
}

int flight_check(const std::filesystem::path& content, const std::filesystem::path& capture) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    std::ifstream in(capture);
    std::vector<Frame> frames;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Frame f;
        f.number = std::stoi(line.substr(line.find("\"frame\": ") + 9));
        f.object = hex_field(line, "obj");
        f.globals = hex_field(line, "glob");
        frames.push_back(std::move(f));
    }
    int matched = 0, compared = 0;
    for (std::size_t k = 0; k + 1 < frames.size(); ++k) {
        if (frames[k + 1].number != frames[k].number + 1) continue;
        auto a = aircraft_from(frames[k]);
        const auto want = aircraft_from(frames[k + 1]);
        const Record g{frames[k].globals};
        game::FlightRules rules;
        rules.stall_model = g.u8(0x13AA74 - 0x13A9E0) != 0;
        rules.bank_turns = g.u8(0x13AA7F - 0x13A9E0) != 0;
        rules.flying = (g.u16(0x13ABBC - 0x13A9E0) & 0x40) != 0;
        rules.ceiling = g.s32(0x13A9EA - 0x13A9E0);
        game::FlightControls c;
        c.nose_down = a.controls & 0x10; c.nose_up = a.controls & 0x20;
        c.left = a.controls & 0x08; c.right = a.controls & 0x04;
        c.rudder_left = a.controls & 0x80; c.rudder_right = a.controls & 0x40;
        game::fly(a, c, rules, tables);
        std::string diff;
        const auto check = [&](const char* name, long got, long expected) {
            if (got != expected) diff += std::string(" ") + name + " " + std::to_string(got) + "/" + std::to_string(expected);
        };
        check("x", a.x, want.x); check("y", a.y, want.y); check("z", a.z, want.z);
        check("vx", a.vx, want.vx); check("vy", a.vy, want.vy); check("vz", a.vz, want.vz);
        check("pitch", a.attitude.pitch, want.attitude.pitch);
        check("heading", a.attitude.heading, want.attitude.heading);
        check("roll", a.attitude.roll, want.attitude.roll);
        check("rp", a.pitch_rate, want.pitch_rate); check("ry", a.yaw_rate, want.yaw_rate);
        check("rr", a.roll_rate, want.roll_rate);
        check("in_p", a.pitch_input, want.pitch_input); check("in_r", a.roll_input, want.roll_input);
        check("thr", a.throttle, want.throttle); check("steps", a.throttle_steps, want.throttle_steps);
        check("speed", a.speed, want.speed); check("air", a.airspeed, want.airspeed);
        check("fuel", a.fuel, want.fuel); check("sink", a.sink, want.sink); check("climb", a.climb_speed, want.climb_speed);
        check("drag", a.ground_drag, want.ground_drag);
        check("bturn", a.bank_turn, want.bank_turn); check("bsin", a.bank_sine, want.bank_sine);
        check("motion", a.motion, want.motion); check("gear", a.gear, want.gear);
        for (std::size_t i = 0; i < 9; ++i) {
            check("body", a.body[i], want.body[i]);
            check("view", a.view[i], want.view[i]);
        }
        ++compared;
        if (diff.empty()) ++matched;
        else std::printf("frame %d:%s\n", frames[k].number, diff.c_str());
    }
    std::printf("%d of %d ticks match\n", matched, compared);
    return matched == compared ? 0 : 1;
}

// The `calls` list of a capture line: [["109EFA", 701, 43, 32], ...].
std::vector<std::vector<std::int32_t>> calls_field(const std::string& line) {
    std::vector<std::vector<std::int32_t>> calls;
    auto at = line.find("\"calls\": [");
    if (at == std::string::npos) throw std::runtime_error("capture line without calls (flightcap.py --calls)");
    at += 10;
    int depth = 1;
    std::vector<std::int32_t> call;
    std::string token;
    bool hex = false;
    for (; at < line.size() && depth > 0; ++at) {
        const char ch = line[at];
        if (ch == '"') {
            hex = !hex;
            continue;
        }
        if (hex) {
            token += ch;
            continue;
        }
        if (ch == '[') {
            ++depth;
            call.clear();
        } else if (ch == ',' || ch == ']') {
            if (!token.empty()) {
                call.push_back(std::int32_t(call.empty() ? std::stol(token, nullptr, 16) : std::stol(token)));
                token.clear();
            }
            if (ch == ']') {
                if (--depth == 1) calls.push_back(call);
            }
        } else if (ch != ' ') {
            token += ch;
        }
    }
    return calls;
}

std::string call_text(const std::vector<std::int32_t>& call) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%X", unsigned(call[0]));
    std::string text = buffer;
    for (std::size_t i = 1; i < call.size(); ++i) text += " " + std::to_string(call[i]);
    return "[" + text + "]";
}

int sound_check(const std::filesystem::path& content, const std::filesystem::path& capture) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    const auto world = data::read_world(assets.program);
    struct Line {
        Frame frame;
        std::vector<std::vector<std::int32_t>> calls;
        std::string procedure;
    };
    std::vector<Line> lines;
    std::ifstream in(capture);
    for (std::string text; std::getline(in, text);) {
        if (text.empty()) continue;
        Line l;
        l.frame.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
        l.frame.object = hex_field(text, "obj");
        l.frame.globals = hex_field(text, "glob");
        l.calls = calls_field(text);
        const auto p = text.find("\"proc\": \"");
        if (p != std::string::npos) l.procedure = text.substr(p + 9, text.find('"', p + 9) - p - 9);
        lines.push_back(std::move(l));
    }
    if (lines.empty()) return 1;
    game::SoundDriver driver(assets);
    game::Sounds sounds(driver);
    std::vector<std::vector<std::int32_t>> log;
    driver.log = &log;
    sounds.log = &log;
    const auto global = [](const Frame& f) { return Record{f.globals}; };
    const auto at = [](std::uint32_t address) { return int(address - 0x13A9E0); };
    game::Messages messages(assets.program);
    {
        const auto g = global(lines[0].frame);
        sounds.set_counters(g.u8(at(0x13AAB1)), g.s8(at(0x13AA87)), g.u8(at(0x13AB75)), g.s8(at(0x13AB7A)));
        const auto abbc = g.u16(at(0x13ABBC)), abbe = g.u16(at(0x13ABBE));
        messages.set_state({g.u16(at(0x13ADD0)), g.u16(at(0x13ADD2)), g.u16(at(0x13ADCE)), (abbc & 1) != 0,
                            (abbc & 4) != 0, (abbe & 0x2000) != 0, (abbe & 0x20) != 0, g.s8(at(0x13AB82)),
                            g.u8(at(0x13AB52)), g.u8(at(0x13AB83)), g.u8(at(0x13AB53)), g.u8(at(0x13AB80))});
    }
    int message_matched = 0;
    game::TextRunner text(assets);
    bool typing = false;
    int matched = 0, compared = 0;
    for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
        const auto& now = lines[k].frame;
        const auto& next = lines[k + 1].frame;
        log.clear();
        const auto g = global(now);
        {
            // The view; a view key asks for the engine's update at once
            // ($10E52E: $13ABA0 = $FF when it was 0).
            const int view = g.s8(at(0x13AA75));
            const auto next_g = global(next);
            sounds.set_view(view != 0, next_g.s8(at(0x13ABA2)), next_g.s8(at(0x13AA75)));
            if (g.u8(at(0x13ABA2)) != next_g.u8(at(0x13ABA2))) sounds.event(game::EngineSound::update);
        }
        auto a = aircraft_from(now);
        const auto end = aircraft_from(next);
        bool flying = (g.u16(at(0x13ABBC)) & 0x40) != 0;
        // The screen procedure's part ($102EFE, $1038D8, $103980, $1039C2).
        const auto& proc = lines[k].procedure;
        if (proc == "102EFE" && (a.state & game::flag::crashed)) {
            sounds.stop_all();
            sounds.noise(0x3F, 0x78);
            flying = false;
        } else if (proc == "1038D8") {
            if (global(next).u8(at(0x13AB87)) > 0) sounds.event(game::EngineSound::start);
            else sounds.set_active(false);
            messages.clear();
            sounds.clear_warnings();
        } else if (proc == "103980") {
            sounds.event(game::EngineSound::start);
            messages.clear();
            sounds.clear_warnings();
        } else if (proc == "1039C2" && std::int16_t(g.u16(at(0x13ADC6))) <= 0) {
            sounds.event(game::EngineSound::start);
        } else if (proc == "1039F0" && std::int16_t(g.u16(at(0x13ADC6))) <= 0) {
            text.clear();
            text.queue(99);
            typing = true;
        }
        if ((proc == "1039F0" || proc == "103A36") && typing && text.tick(1.0 / 15)) sounds.click();
        messages.clock();
        if (flying) {
            game::ground_warnings(a, messages, sounds);
            // (The gear key, seen by its handle: not the handle the water raises.)
            if (((a.gear ^ end.gear) & 0x80) && !(end.surface & 2)) sounds.gear_moved();
            game::FlightRules rules;
            rules.stall_model = g.u8(at(0x13AA74)) != 0;
            rules.bank_turns = g.u8(at(0x13AA7F)) != 0;
            rules.ceiling = g.s32(at(0x13A9EA));
            rules.world = &world;
            game::FlightControls c;
            c.nose_down = a.controls & 0x10; c.nose_up = a.controls & 0x20;
            c.left = a.controls & 0x08; c.right = a.controls & 0x04;
            c.rudder_left = a.controls & 0x80; c.rudder_right = a.controls & 0x40;
            const auto events = game::fly(a, c, rules, tables);
            if (events.engine_sound != 0) sounds.event(game::EngineSound(events.engine_sound));
            if (events.engine_restart_in != 0) sounds.restart_after(events.engine_restart_in);
            if (events.tyres_volume != 0) sounds.tyres(events.tyres_volume);
            if (events.stall_warning) sounds.warn(game::warning::stall);
            if (events.gear_speed_warning) sounds.warn_beep(4);
        }
        if (proc != "1039F0" && proc != "103A36") sounds.warn(messages.update(a));
        sounds.update(end);
        // The message line against the next record ($13ADD0, $13ADCE).
        const auto gn = global(next);
        if (messages.code() == gn.u16(at(0x13ADD0)) && (messages.shown() == (gn.u16(at(0x13ADCE)) & 0xFF)))
            ++message_matched;
        else
            std::printf("frame %d: message %04X shows %d, original %04X shows %d\n", now.number, messages.code(),
                        messages.shown(), gn.u16(at(0x13ADD0)), gn.u16(at(0x13ADCE)) & 0xFF);
        std::string got, want;
        for (const auto& c : log) got += call_text(c);
        for (const auto& c : lines[k + 1].calls) want += call_text(c);
        ++compared;
        if (got == want) ++matched;
        else std::printf("frame %d (%s):\n  port     %s\n  original %s\n", now.number, proc.c_str(), got.c_str(), want.c_str());
        // A real second passed: the original counts it in the frame's pacer.
        if (global(next).s32(at(0x13ADE2)) != g.s32(at(0x13ADE2))) sounds.second();
    }
    std::printf("%d of %d frames' sound calls match, %d messages\n", matched, compared, message_matched);
    return matched == compared && message_matched == compared ? 0 : 1;
}

// --- weaponcheck ---------------------------------------------------------------

// Research: steps the missile records (1-3) and the projectile slots of a
// recording made with `flightcap.py --objs` one frame at a time from the
// original's state, launching and firing where the original did, and
// compares the next frame's positions, velocities and counters. A missile
// below the ground blows up there; with `--smoke` the smoke slot it leaves
// is compared too (its type, place and timer), and the slots' timers.
int weapon_check(const std::filesystem::path& content, const std::filesystem::path& capture) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    const game::Weapons weapons(assets.program, tables);
    const game::Autopilot autopilot(assets.program, tables);
    const game::ShotHits shot_hits(assets.program, tables);
    const auto world = data::read_world(assets.program);
    struct Line {
        Frame frame;
        std::vector<std::uint8_t> objs, shots, smoke;
        std::map<int, std::vector<std::uint8_t>> recs; // (`--recs`: the records a lock can be on)
    };
    std::vector<Line> lines;
    std::ifstream in(capture);
    for (std::string text; std::getline(in, text);) {
        if (text.empty()) continue;
        Line l;
        l.frame.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
        l.frame.object = hex_field(text, "obj");
        l.frame.globals = hex_field(text, "glob");
        l.objs = hex_field(text, "objs");
        l.shots = hex_field(text, "shots");
        if (text.find("\"smoke\"") != std::string::npos) l.smoke = hex_field(text, "smoke");
        for (int r = 5; r < 16; ++r) {
            if (text.find("\"" + std::to_string(r) + "\": \"") != std::string::npos) l.recs[r] = hex_field(text, std::to_string(r));
        }
        lines.push_back(std::move(l));
    }
    const auto record = [](const Line& l, int slot) {
        return std::vector<std::uint8_t>(l.objs.begin() + slot * 0x200, l.objs.begin() + (slot + 1) * 0x200);
    };
    const auto missile_from = [&](const Line& l, int slot) {
        Frame f{l.frame.number, record(l, slot), l.frame.globals};
        const Record o{f.object};
        game::Missile m;
        m.body = aircraft_from(f);
        m.type = o.u8(0x62);
        m.life = o.s16(0x4C);
        m.fading = o.u16(0) & 0x400;
        m.pilot = o.u8(0x64);
        m.course.target = {o.s16(0x2C), o.s16(0x2E), o.s16(0x30), o.s16(0x32), o.s32(0x34)};
        m.course.target_record = o.u8(0x38);
        m.stopped = (o.u16(0) & 0x400) && !(o.u16(0) & 0x1000);
        return m;
    };
    const auto shot_from = [&](const Line& l, int slot) {
        const std::vector<std::uint8_t> b(l.shots.begin() + slot * 0x80, l.shots.begin() + slot * 0x80 + 0x40);
        const Record o{b};
        game::Shot s;
        const auto flags = o.u16(0x26);
        s.kind = (flags & 0x2000) ? game::Shot::Kind::flare : (flags & 0x1000) ? game::Shot::Kind::chaff : game::Shot::Kind::round;
        s.x = o.s16(0x30) * 0x400000 + o.s32(0);
        s.y = o.s32(4);
        s.z = o.s16(0x32) * 0x400000 + o.s32(8);
        s.vx = o.s32(0xC); s.vy = o.s32(0x10); s.vz = o.s32(0x14);
        s.life = o.s16(0x28);
        s.moved = o.s16(0x24) != 0;
        s.hit = flags & 2;
        s.ex = o.s32(0x18); s.ey = o.s32(0x1C); s.ez = o.s32(0x20);
        return std::pair{s, (flags & 1) != 0};
    };
    int compared = 0, matched = 0;
    for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
        const auto& now = lines[k];
        const auto& next = lines[k + 1];
        if (next.frame.number != now.frame.number + 1) continue;
        std::string diff;
        const auto check = [&](const std::string& name, long got, long expected) {
            if (got != expected) diff += " " + name + " " + std::to_string(got) + "/" + std::to_string(expected);
        };
        const auto player = aircraft_from(next.frame);
        const Record before{now.frame.object}, after{next.frame.object};
        int compared_here = 0;
        for (int slot = 0; slot < 3; ++slot) {
            const bool was = now.objs[std::size_t(slot * 0x200 + 1)] & 0x40, is = next.objs[std::size_t(slot * 0x200 + 1)] & 0x40;
            if (!is) continue;
            game::Missile m;
            if (was) {
                m = missile_from(now, slot);
            } else {
                const bool amraam = (before.u8(0x63) & 0xF0) == 0x30;
                const int left = amraam ? (after.u8(0x5F) & 0xF) : (after.u8(0x5F) >> 4);
                m = weapons.launched(player, amraam ? game::Weapon::amraam : game::Weapon::sidewinder, left);
            }
            game::FlightRules rules;
            // The radar's lock, on a recorded record: its place as the frame
            // starts (the missiles step before it).
            const auto lock = Record{now.frame.globals}.s16(0x13ACB0 - 0x13A9E0);
            std::optional<game::Aircraft> locked;
            if (lock > 0 && now.recs.count(lock / 0x200)) locked = aircraft_from(Frame{0, now.recs.at(lock / 0x200), now.frame.globals});
            game::Weapons::fly_missile(m, rules, tables, locked ? &*locked : nullptr, locked ? lock / 0x200 : -1, &autopilot);
            // Below the ground ($11B6A2's $10, when it touched nothing):
            // it blows up there; on land it leaves smoke in the first free
            // slot.
            if (was && !m.fading) {
                const game::Aircraft from = missile_from(now, slot).body;
                std::array<std::int32_t, 3> smoke_at{};
                bool smoke = false;
                if (game::Weapons::hit_ground(m, {from.x, from.y, from.z}, &world, smoke_at, smoke) && !now.smoke.empty() &&
                    !next.smoke.empty()) {
                    int free = -1;
                    for (int k = 0; k < 16 && free < 0; ++k)
                        if (!(now.smoke[std::size_t(32 * k + 1)] & 0x40)) free = k;
                    const std::string n = "m" + std::to_string(slot + 1) + "smoke";
                    check(n, smoke && free >= 0 ? free : -1,
                          free >= 0 && (next.smoke[std::size_t(32 * free + 1)] & 0x40) ? free : -1);
                    if (smoke && free >= 0) {
                        const Record want{std::vector<std::uint8_t>(next.smoke.begin() + 32 * free, next.smoke.begin() + 32 * free + 32)};
                        const int type = (free & 1) ? 0x12 : 0x13;
                        check(n + "type", type, want.u16(2));
                        check(n + "timer", 0x7F, want.s16(4));
                        // (+6 / +8 the cells, +$C / +$E / +$10 the fine place.)
                        check(n + "x", smoke_at[0] >> 8, want.s16(6) * 0x4000 + want.s16(0xC));
                        check(n + "y", smoke_at[1] >> 8, want.s32(0x10));
                        check(n + "z", smoke_at[2] >> 8, want.s16(8) * 0x4000 + want.s16(0xE));
                    }
                }
            }
            // Its touch ($11B6A2 after its move, from its place before
            // it) against the recorded records as the frame started.
            if (!now.recs.empty() && was) {
                const game::Aircraft from = missile_from(now, slot).body;
                std::vector<game::Aircraft> others;
                std::vector<int> numbers;
                for (const auto& [r, bytes] : now.recs) {
                    others.push_back(aircraft_from(Frame{0, bytes, now.frame.globals}));
                    numbers.push_back(r);
                }
                std::vector<game::Body> bodies{{&m.body, 0, true}};
                for (std::size_t i = 0; i < others.size(); ++i)
                    bodies.push_back({&others[i], std::uint8_t(numbers[i]), (others[i].state & 0x40) != 0});
                bool near = false;
                const bool hit = game::touch_bodies(bodies, 0, {from.x, from.y, from.z}, near) == game::touch::object;
                const bool gone = !(next.objs[std::size_t(slot * 0x200 + 1)] & 0x40);
                check("m" + std::to_string(slot + 1) + "hit", hit, gone);
                for (std::size_t i = 0; i < others.size(); ++i) {
                    const auto want_o = aircraft_from(Frame{0, next.recs.at(numbers[i]), next.frame.globals});
                    const std::string r = "r" + std::to_string(numbers[i]);
                    check(r + "near", others[i].motion & 1, want_o.motion & 1);
                    check(r + "spin", others[i].handling & 2, want_o.handling & 2);
                    check(r + "role", hit ? others[i].role : want_o.role, want_o.role);
                }
                if (gone) continue;
            }
            const auto want = missile_from(next, slot);
            const std::string n = "m" + std::to_string(slot + 1) + (was ? "." : "+");
            check(n + "x", m.body.x, want.body.x); check(n + "y", m.body.y, want.body.y); check(n + "z", m.body.z, want.body.z);
            check(n + "stopped", m.stopped, want.stopped);
            check(n + "water", m.body.surface & 2, want.body.surface & 2);
            if (want.stopped) { // (the rest of its record no longer counts)
                check(n + "life", m.life, want.life);
                ++compared_here;
                continue;
            }
            check(n + "vx", m.body.vx, want.body.vx); check(n + "vy", m.body.vy, want.body.vy); check(n + "vz", m.body.vz, want.body.vz);
            check(n + "speed", m.body.speed, want.body.speed); check(n + "life", m.life, want.life);
            check(n + "fading", m.fading, want.fading);
            check(n + "pitch", m.body.attitude.pitch, want.body.attitude.pitch);
            check(n + "heading", m.body.attitude.heading, want.body.attitude.heading);
            check(n + "pilot", m.pilot, want.pilot); check(n + "26", m.body.ground_drag, want.body.ground_drag);
            ++compared_here;
        }
        // The rounds' hit test against record 4 when it is in use (the
        // practice's lead, `gun1.jsonl`), as the frame starts.
        std::vector<game::WorldObject> targets;
        if (now.objs[std::size_t(3 * 0x200 + 1)] & 0x40) {
            game::WorldObject w;
            const std::vector<std::uint8_t> bytes = record(now, 3);
            w.craft = aircraft_from(Frame{0, bytes, now.frame.globals});
            w.craft.player = false;
            w.record = 4;
            w.shape_type = 3; // (the lead's F-16 shape, `$11D0AA`)
            w.scale = Record{bytes}.u8(0x7D) & 15;
            w.damage = Record{bytes}.u8(0x3C);
            targets.push_back(w);
        }
        int cannon_hits = 0;
        for (int slot = 0; slot < 10; ++slot) {
            auto [was, was_on] = shot_from(now, slot);
            const auto [want, is_on] = shot_from(next, slot);
            if (!is_on || want.life < 1) continue;
            game::Shot s;
            if (was_on && was.life >= 1) {
                s = was;
                --s.life;
            } else {
                // (The spread reads the frame counter as the frame started.)
                s = weapons.fired(want.kind, player, std::uint16_t(now.frame.number));
            }
            if (!targets.empty() && !s.hit && shot_hits.test(s, targets, cannon_hits, {player.x, player.y, player.z})) {
                s.hit = true;
                s.life = 2;
            } else {
                game::Weapons::fly_shot(s);
            }
            const std::string n = "s" + std::to_string(slot) + ((was_on && was.life >= 1) ? "." : "+");
            check(n + "x", s.x, want.x); check(n + "y", s.y, want.y); check(n + "z", s.z, want.z);
            check(n + "vx", s.vx, want.vx); check(n + "vy", s.vy, want.vy); check(n + "vz", s.vz, want.vz);
            check(n + "life", s.life, want.life); check(n + "hit", s.hit, want.hit);
            ++compared_here;
        }
        if (!targets.empty() && (next.objs[std::size_t(3 * 0x200 + 1)] & 0x40)) {
            const Record want{record(next, 3)};
            check("r4damage", targets[0].damage & 15, want.u8(0x3C) & 15);
            {
                // The level the drawer took: its model's byte +1 against +$7B.
                const auto model = shot_hits.drawn_model(targets[0], {player.x, player.y, player.z});
                check("r4hitbyte", model ? assets.program.u8(model + 1) : 0xFF, Record{record(now, 3)}.u8(0x7B));
            }
            if (!(Record{record(now, 3)}.u16(0) & 0x0600)) check("r4crash", targets[0].craft.state & 0x0200, want.u16(0) & 0x0200);
            ++compared_here;
        }
        if (!now.smoke.empty() && !next.smoke.empty()) {
            // $116B82: the slots in use run down a frame.
            for (int k = 0; k < 16; ++k) {
                if (!(now.smoke[std::size_t(32 * k + 1)] & 0x40)) continue;
                const Record a{std::vector<std::uint8_t>(now.smoke.begin() + 32 * k, now.smoke.begin() + 32 * k + 32)};
                const Record b{std::vector<std::uint8_t>(next.smoke.begin() + 32 * k, next.smoke.begin() + 32 * k + 32)};
                check("smoke" + std::to_string(k), std::int16_t(a.s16(4) - 1), b.s16(4));
            }
            ++compared_here;
        }
        if (compared_here == 0) continue;
        ++compared;
        if (diff.empty()) ++matched;
        else std::printf("frame %d:%s\n", next.frame.number, diff.c_str());
    }
    std::printf("%d of %d frames match\n", matched, compared);
    return matched == compared ? 0 : 1;
}

// --- trailcheck ----------------------------------------------------------------

// Research: the smoke trail's ring (`$1450B8`-`$145103`, a recording made
// with `flightcap.py --recs N --mem 1450B8:50`) taken frame by frame from
// the recorded record N after its step (`$11AD0A` as Trail::update, for a
// record active as the step began), and
// which record the trail follows (`$1450BA`: the one newly burning, +2 bit
// 4). Compares the ring, its next slot and count, and the record's +$3D.
int trail_check(const std::filesystem::path& content, const std::filesystem::path& capture, int number) {
    using namespace interceptor;
    (void)content;
    struct Line {
        Frame frame;
        std::vector<std::uint8_t> rec, mem;
    };
    std::vector<Line> lines;
    std::ifstream in(capture);
    for (std::string text; std::getline(in, text);) {
        if (text.empty()) continue;
        Line l;
        l.frame.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
        l.frame.globals = hex_field(text, "glob");
        l.rec = hex_field(text, std::to_string(number));
        l.mem = hex_field(text, "mem");
        lines.push_back(std::move(l));
    }
    const auto ring_of = [](const std::vector<std::uint8_t>& mem) {
        const Record m{mem};
        game::Trail t;
        t.count = m.u8(0);
        t.next = m.u8(1);
        t.record = m.s16(2) / 0x200;
        for (int s = 0; s < 6; ++s)
            for (int k = 0; k < 3; ++k) t.places[std::size_t(s)][std::size_t(k)] = m.s32(4 + 12 * s + 4 * k);
        return t;
    };
    int compared = 0, matched = 0, updates = 0;
    for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
        const auto& now = lines[i];
        const auto& next = lines[i + 1];
        if (next.frame.number != now.frame.number + 1) continue;
        auto t = ring_of(now.mem);
        const auto want = ring_of(next.mem);
        auto a = aircraft_from(Frame{0, next.rec, next.frame.globals});
        const auto before = aircraft_from(Frame{0, now.rec, now.frame.globals});
        std::string diff;
        const auto check = [&](const std::string& name, long got, long expected) {
            if (got != expected) diff += " " + name + " " + std::to_string(got) + "/" + std::to_string(expected);
        };
        // The record newly burning is the trail's.
        if ((a.motion & 0x1000) && !(before.motion & 0x1000)) t.record = number;
        check("record", t.record, want.record);
        const bool waiting = Record{next.frame.globals}.u8(0x13AA9E - 0x13A9E0);
        if (t.record == number && !waiting && (before.state & 0x40)) { // (also the step that takes it off)
            a.trail = before.trail;
            t.update(a);
            ++updates;
            check("+3D", a.trail, Record{next.rec}.u8(0x3D));
        }
        check("count", t.count, want.count);
        check("next", t.next, want.next);
        for (int s = 0; s < 6; ++s)
            for (int k = 0; k < 3; ++k)
                check("p" + std::to_string(s) + "." + std::to_string(k), t.places[std::size_t(s)][std::size_t(k)],
                      want.places[std::size_t(s)][std::size_t(k)]);
        ++compared;
        if (diff.empty()) ++matched;
        else std::printf("frame %d:%s\n", next.frame.number, diff.c_str());
    }
    std::printf("%d updates; %d of %d frames match\n", updates, matched, compared);
    return matched == compared ? 0 : 1;
}

// --- effectshot ----------------------------------------------------------------

// Research: the port's view of a recorded frame (`flightcap.py --objs
// --smoke`): the original's camera (`$13AD6C` and the view matrix
// `$13AEC8`), the aircraft itself when the view is outside, the missile
// records 1-3 and recorded records (blowing up ones with their explosion,
// the trail's record with its trail) and the smoke slots,
// in the flight palette, low-res pixels doubled as harness/screen.py shows
// the original.
// Research: the analog stick's ramp - the stick held at `deflection` back
// for 30 ticks, then eased to a quarter: the pitch input each tick.
int stick_check() {
    using namespace interceptor;
    game::Aircraft a;
    a.player = true;
    for (int tick = 0; tick < 40; ++tick) {
        game::FlightControls c;
        c.stick = std::array<float, 2>{0.0f, tick < 30 ? 0.5f : 0.25f};
        game::read_controls(a, c);
        game::ramp_inputs(a);
        std::printf("%d", a.pitch_input);
        std::printf(tick + 1 < 40 ? " " : "\n");
    }
    return 0;
}

// The port's outside view from a chosen place (world units), heading
// (degrees, clockwise) and pitch (degrees, down), with or without high
// detail: for looking at the scenery's drawing anywhere.
int scene_shot(const std::filesystem::path& content, const std::filesystem::path& out, double x, double y, double z,
               double heading, double pitch, bool high) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto world = data::read_world(assets.program);
    game::Scene scene(assets, world);
    scene.set_high_detail(high);
    game::Camera camera;
    camera.position = {x, y, z};
    const double h = heading * 3.14159265358979 / 180, p = pitch * 3.14159265358979 / 180;
    const double f[3] = {-std::sin(h) * std::cos(p), -std::sin(p), std::cos(h) * std::cos(p)};
    const double r[3] = {std::cos(h), 0, std::sin(h)};
    const double u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    for (std::size_t k = 0; k < 3; ++k) {
        camera.matrix[k] = r[k] * 0xA8 / 256.0;
        camera.matrix[3 + k] = u[k] * 0xFC / 256.0;
        camera.matrix[6 + k] = f[k] * 0x80 / 256.0;
    }
    if (const char* file = std::getenv("F18_CAMERA")) {
        // (The original's camera instead: `$13AD6C` place and `$13AEC8`
        // matrix as read from the machine, 12 numbers.)
        std::ifstream in(file);
        for (auto& v : camera.position) in >> v;
        for (auto& v : camera.matrix) in >> v;
    }
    game::IndexedImage view(320, 168);
    scene.draw(view, 159, 167, camera);
    const auto& palette = assets.palettes.at(data::flight_palette);
    platform::Image image(640, 336);
    for (int py = 0; py < 336; ++py)
        for (int px = 0; px < 640; ++px) {
            const auto c = palette[view.pixels[std::size_t(py / 2 * 320 + px / 2)] & 31];
            image.put(px, py, {std::uint8_t((c >> 8 & 15) * 17), std::uint8_t((c >> 4 & 15) * 17), std::uint8_t((c & 15) * 17)});
        }
    platform::save_png(image, out);
    return 0;
}

int effect_shot(const std::filesystem::path& content, const std::filesystem::path& capture, int index,
                const std::filesystem::path& out) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto world = data::read_world(assets.program);
    const game::Scene scene(assets, world);
    std::ifstream in(capture);
    std::string text;
    for (int i = 0; std::getline(in, text) && i < index; ++i) {}
    Frame f;
    f.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
    // (With a follow step the picture is the aircraft's own place, before
    // the poke: "moved".)
    f.object = hex_field(text, text.find("\"moved\"") != std::string::npos ? "moved" : "obj");
    f.globals = hex_field(text, "glob");
    const auto objs = text.find("\"objs\"") != std::string::npos ? hex_field(text, "objs") : std::vector<std::uint8_t>{};
    const auto smoke = text.find("\"smoke\"") != std::string::npos ? hex_field(text, "smoke") : std::vector<std::uint8_t>{};
    const auto player = aircraft_from(f);
    const Record g{f.globals};
    game::Camera camera;
    for (int k = 0; k < 9; ++k) camera.matrix[std::size_t(k)] = g.s16(0x13AEC8 - 0x13A9E0 + 2 * k) / 256.0;
    // (`$13AD6C`: the camera's place, 32-bit units.)
    for (std::size_t k = 0; k < 3; ++k) camera.position[k] = g.s32(0x13AD6C - 0x13A9E0 + 4 * int(k)) / 256.0;
    std::vector<game::Vehicle> vehicles;
    // The outside view, or a look past the cockpit (`$13AA97` 4-8, 12,
    // 13): the aircraft itself.
    const int look = g.u8(0x13AA97 - 0x13A9E0);
    const bool outside = g.u8(0x13AA75 - 0x13A9E0);
    if (!outside) camera.water_from = std::array<double, 3>{player.x / 256.0, player.y / 256.0, player.z / 256.0};
    if (outside || (look >= 4 && look <= 8) || look >= 12) {
        game::Vehicle self;
        self.shadow = outside;
        self.type = player.type == 0x10 ? 3 : 4;
        self.position = {player.x / 256.0, player.y / 256.0, player.z / 256.0};
        self.body = player.body;
        vehicles.push_back(self);
    }
    for (int slot = 0; slot < 3 && !objs.empty(); ++slot) {
        const std::vector<std::uint8_t> bytes(objs.begin() + slot * 0x200, objs.begin() + (slot + 1) * 0x200);
        const Record o{bytes};
        if (!(o.u16(0) & 0x40)) continue;
        const auto a = aircraft_from(Frame{0, bytes, f.globals});
        game::Vehicle v;
        v.type = o.u8(0x62);
        v.position = {a.x / 256.0, a.y / 256.0, a.z / 256.0};
        v.body = a.body;
        const int timer = o.s16(0x4C);
        if ((o.u16(0) & 0x400) && timer >= 0 && timer <= 15) {
            v.explosion_age = 15 - timer;
            v.explosion_kind = (o.u8(4) & 2) ? 2 : 1;
            v.model = timer > 12;
        }
        vehicles.push_back(v);
    }
    // Recorded records (`--recs`), drawn with the shape type +$7A, their
    // explosion and, for the trail's record (`--mem 1450B8:50`), its trail.
    const auto mem = text.find("\"mem\"") != std::string::npos ? hex_field(text, "mem") : std::vector<std::uint8_t>{};
    for (int number = 5; number < 16; ++number) {
        if (text.find("\"" + std::to_string(number) + "\": \"") == std::string::npos) continue;
        const auto bytes = hex_field(text, std::to_string(number));
        const Record o{bytes};
        if (!(o.u16(0) & 0x40)) continue;
        const auto a = aircraft_from(Frame{0, bytes, f.globals});
        game::Vehicle v;
        v.type = o.u8(0x7A);
        v.position = {a.x / 256.0, a.y / 256.0, a.z / 256.0};
        v.body = a.body;
        if ((a.state & 0x400) && a.timer >= 0 && a.timer <= 15) {
            v.explosion_age = 15 - a.timer;
            v.explosion_kind = a.type == 0x15 ? 3 : (a.surface & 2) ? 2 : 1;
            v.model = a.timer > 12;
        }
        if (!mem.empty() && Record{mem}.s16(2) == number * 0x200 && a.trail > 0) {
            const Record m{mem};
            game::Vehicle::Trail t;
            t.length = a.trail;
            t.spinning = a.handling & 2;
            t.move = {a.vx / 256.0, a.vy / 256.0, a.vz / 256.0};
            const auto add = [&](int slot) {
                t.slots.push_back(slot);
                t.places.push_back({m.s32(4 + 12 * slot) / 256.0, m.s32(8 + 12 * slot) / 256.0, m.s32(12 + 12 * slot) / 256.0});
            };
            if (m.u8(0) >= 6) {
                for (int k = 0; k <= a.trail; ++k) add((m.u8(1) + k) % 6);
            } else {
                for (int k = 0; k < a.trail; ++k) add(k);
            }
            v.trail = t;
        }
        vehicles.push_back(v);
    }
    std::vector<game::SmokeColumn> columns;
    for (int k = 0; k < 16 && !smoke.empty(); ++k) {
        const Record o{std::vector<std::uint8_t>(smoke.begin() + 32 * k, smoke.begin() + 32 * k + 32)};
        if (!(o.u16(0) & 0x40)) continue;
        columns.push_back({o.u16(2) + 16, o.s16(4),
                           {double(o.s16(6) * 0x4000 + o.s16(0xC)), double(o.s32(0x10)), double(o.s16(8) * 0x4000 + o.s16(0xE))}});
    }
    game::IndexedImage view(320, 168);
    scene.draw(view, 159, 167, camera, vehicles, {}, columns);
    const auto& palette = assets.palettes.at(data::flight_palette);
    platform::Image sheet(640, 168);
    for (int y = 0; y < 168; ++y) {
        for (int x = 0; x < 640; ++x) {
            const auto c = palette[view.pixels[std::size_t(y * 320 + x / 2)] & 31];
            sheet.put(x, y, {std::uint8_t((c >> 8 & 15) * 17), std::uint8_t((c >> 4 & 15) * 17), std::uint8_t((c & 15) * 17)});
        }
    }
    platform::save_png(sheet, out);
    std::printf("frame %d: %zu vehicles, %zu smoke slots\n", f.number, vehicles.size(), columns.size());
    return 0;
}

// --- democheck -----------------------------------------------------------------

// Research: flies the demo's recording (text/textply, text/textctl) from the
// deck of the carrier its object list places, through the flight model, the
// carrier's deck and the key events that change the aircraft (gear, hook,
// speed brake), and
// compares each frame's aircraft with a recording of the original's demo
// (`flightcap.py` from the menu's item 1: the flight's frames 1, 2, ...).
int demo_check(const std::filesystem::path& content, const std::filesystem::path& capture) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    game::Carrier carrier(assets.program);
    // The demo's list ($7F): the carrier first, turned towards its target.
    const game::ObjectLists lists(assets.program);
    game::ObjectChoice choice;
    choice.mode = 0x7F;
    const auto spawned = lists.spawn(choice, tables);
    for (const auto& o : spawned.objects) {
        if ((o.craft.type & 0xF0) == 0x20) carrier.place(o.craft, tables);
    }
    std::ifstream in(capture);
    std::vector<Frame> frames;
    // With `--recs 10`: the bogey's record each frame, and the one before
    // the flight (its start).
    std::vector<std::vector<std::uint8_t>> bogey;
    std::vector<std::uint8_t> bogey_start, globals_start;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Frame f;
        f.number = std::stoi(line.substr(line.find("\"frame\": ") + 9));
        f.object = hex_field(line, "obj");
        f.globals = hex_field(line, "glob");
        const bool recs = line.find("\"10\": \"") != std::string::npos;
        // (Only the flight's frames: `$13ABBC` bit 6.)
        if (!(Record{f.globals}.u8(0x13ABBD - 0x13A9E0) & 0x40)) {
            if (frames.empty() && recs) {
                bogey_start = hex_field(line, "10");
                globals_start = f.globals;
            }
            continue;
        }
        if (!frames.empty() && f.number != frames.back().number + 1) break;
        if (recs) bogey.push_back(hex_field(line, "10"));
        frames.push_back(std::move(f));
    }
    if (frames.empty()) throw std::runtime_error("no flight frames");
    // The bogey first (the one the check compares), then the carrier.
    std::vector<game::WorldObject> objects;
    for (const auto& o : spawned.objects) {
        if ((o.craft.type & 0xF0) == 0x10) objects.push_back(o);
    }
    for (const auto& o : spawned.objects) {
        if ((o.craft.type & 0xF0) == 0x20) objects.push_back(o);
    }
    if (!bogey_start.empty()) {
        // The spawn against the original's record before the flight.
        const auto want = world_object_from(10, bogey_start, globals_start);
        const auto& got = objects.front();
        std::printf("spawn: x %d/%d z %d/%d y %d/%d heading %d/%d target %d,%d/%d,%d\n", got.craft.x, want.craft.x,
                    got.craft.z, want.craft.z, got.craft.y, want.craft.y, got.craft.attitude.heading,
                    want.craft.attitude.heading, got.course.target.cell_x, got.course.target.cell_z,
                    want.course.target.cell_x, want.course.target.cell_z);
        objects.front() = want;
    }
    const game::Autopilot autopilot(assets.program, tables);
    const game::ObjectStep step(assets.program, tables, lists);
    int bogey_matched = 0;
    game::Replay replay(assets.demo_controls, assets.demo_keys);
    replay.start();
    auto a = carrier.start_on_deck(assets.program, tables);
    game::FlightRules rules;
    std::uint16_t pending = 0;
    int matched = 0, compared = 0;
    // The first recorded frame is the state after the flight's first frame;
    // a recording that starts later starts from its first frame's state.
    std::size_t first = 0;
    game::Radar radar;
    std::int16_t player_distance = 0;
    int range = 0xD;
    if (frames.front().number > 1) {
        for (int n = 1; n <= frames.front().number; ++n) pending = replay.next().game;
        a = aircraft_from(frames.front());
        if (!bogey.empty()) objects.front() = world_object_from(10, bogey.front(), frames.front().globals);
        first = 1;
        const Record g{frames.front().globals}, p{frames.front().object};
        const auto lock = g.s16(0x13ACB0 - 0x13A9E0);
        radar.set_state(g.s8(0x13AB27 - 0x13A9E0), lock < 0 ? -1 : lock / 0x200, g.s8(0x13AB5F - 0x13A9E0));
        player_distance = p.s16(0x4A);
        range = p.u8(0x63) & 15;
    }
    int radar_matched = 0;
    game::SoundDriver driver(assets);
    game::Sounds sounds(driver);
    game::Messages messages(assets.program);
    game::Weapons weapons(assets.program, tables);
    weapons.reset();
    for (std::size_t k = first; k < frames.size(); ++k) {
        if (pending & (1u << game::replay_key::gear)) {
            if (!(a.motion & game::flag::on_ground)) a.gear ^= 0x80;
        }
        if (pending & (1u << game::replay_key::hook)) a.motion ^= 0x8000;
        if (pending & (1u << game::replay_key::speed_brake)) a.state ^= game::flag::speed_brake;
        if (pending & (1u << game::replay_key::target)) radar.request_target();
        if (pending & (1u << game::replay_key::fire)) weapons.trigger(true);
        if (pending & (1u << game::replay_key::fire_up)) weapons.trigger(false);
        if (pending & (1u << game::replay_key::weapon)) weapons.select(sounds);
        if (pending & (1u << game::replay_key::radar_range)) range = range == 0x9 ? 0xD : range == 0xB ? 0x9 : 0xB;
        const auto f = replay.next();
        pending = f.game;
        game::FlightControls c;
        c.recorded = f.control;
        rules.world = nullptr;
        if (!bogey.empty()) {
            for (auto& o : objects) o.craft.motion &= ~0x0001; // (as the module's)
            // $1183E2 (the player's step, before its move).
            const game::Aircraft* locked = nullptr;
            for (const auto& o : objects) {
                if (o.record == radar.locked() && radar.locked() > 0) locked = &o.craft;
            }
            step.measure_player(a, player_distance, locked);
        }
        const auto before = a;
        game::fly(a, c, rules, tables);
        if (carrier.touch(a, before)) a.state |= game::flag::crashed;
        if (!bogey.empty()) {
            {
                // The player's missiles: homing on the lock, their touches.
                const game::Aircraft* locked = nullptr;
                for (const auto& o : objects) {
                    if (radar.locked() > 0 && o.record == radar.locked()) locked = &o.craft;
                }
                auto& slots = weapons.missiles();
                std::array<std::optional<std::array<std::int32_t, 3>>, 3> from;
                for (std::size_t s = 0; s < 3; ++s) {
                    if (slots[s] && !slots[s]->fresh) from[s] = std::array<std::int32_t, 3>{slots[s]->body.x, slots[s]->body.y, slots[s]->body.z};
                }
                weapons.move_missiles(a, rules, sounds, locked, radar.locked(), &autopilot);
                for (std::size_t s = 0; s < 3; ++s) {
                    if (!slots[s] || !from[s]) continue;
                    std::vector<game::Body> list{{&a, 0, true}, {&slots[s]->body, 0, true}};
                    for (auto& o : objects) list.push_back({&o.craft, o.group, o.active()});
                    bool near = false;
                    if (game::touch_bodies(list, 1, *from[s], near) == game::touch::object) slots[s].reset();
                }
                weapons.move_shots(a, false, std::uint16_t(frames[k].number - 1), sounds);
            }
            step_objects(objects, a, step, autopilot, tables, 3, 0, spawned.first_entry, player_distance, radar.locked());
            for (auto& o : objects) { // ($1169D0)
                if (!o.active() || !(o.craft.state & 0x0200)) continue;
                o.craft.state = std::uint16_t((o.craft.state & ~0x0200) | 0x0400);
                o.craft.timer = 15;
                if (radar.locked() == o.record) radar.unlock();
            }
            // The radar after the scene and in the panel, by the views the
            // recording shows (outside `$13AA75`, the look `$13AC76`; a
            // change draws the panel anew).
            // (A recording from the flight's first frame: the one before
            // it is the last frame before the launch.)
            const Record g{frames[k].globals},
                gp{k > 0 ? frames[k - 1].globals : globals_start.empty() ? frames[k].globals : globals_start};
            const bool outside = g.u8(0x13AA75 - 0x13A9E0) != 0;
            const int look = g.s16(0x13AC76 - 0x13A9E0);
            if (outside != (gp.u8(0x13AA75 - 0x13A9E0) != 0) || look != gp.s16(0x13AC76 - 0x13A9E0)) radar.redraw_panel();
            game::Records records{&a, player_distance, &objects};
            const auto counter = std::uint16_t(frames[k].number - 1);
            radar.after_scene(records, counter, outside, tables);
            if (!outside) {
                radar.panel(records, counter, look, range, 3);
                radar.end_panel(counter);
            }
            const auto lock = g.s16(0x13ACB0 - 0x13A9E0);
            const int want_lock = lock < 0 ? -1 : lock / 0x200;
            const auto want_confirm = g.s8(0x13AB5F - 0x13A9E0);
            const auto want_distance = Record{frames[k].object}.s16(0x4A);
            const auto want_threats = g.u8(0x13AB5E - 0x13A9E0);
            if (radar.locked() == want_lock && radar.confirmation() == want_confirm && player_distance == want_distance &&
                radar.threats() == want_threats) {
                ++radar_matched;
            } else if (int(k - first) - radar_matched < 10) {
                std::printf("frame %d radar: lock %d/%d confirm %d/%d distance %d/%d redraw %d/%d threats %d/%d\n",
                            frames[k].number, radar.locked(), want_lock, radar.confirmation(), want_confirm, player_distance,
                            want_distance, radar.redraw(), g.s8(0x13AB27 - 0x13A9E0), radar.threats(), want_threats);
            }
            const auto w = world_object_from(10, bogey[k], frames[k].globals);
            const auto& o = objects.front();
            std::string bd;
            const auto check = [&](const char* name, long got, long expected) {
                if (got != expected) bd += std::string(" ") + name + " " + std::to_string(got) + "/" + std::to_string(expected);
            };
            check("role", o.craft.role, w.craft.role); check("ctl", o.craft.controls, w.craft.controls);
            check("x", o.craft.x, w.craft.x); check("y", o.craft.y, w.craft.y); check("z", o.craft.z, w.craft.z);
            check("heading", o.craft.attitude.heading, w.craft.attitude.heading); check("speed", o.craft.speed, w.craft.speed);
            check("tx", o.course.target.cell_x, w.course.target.cell_x); check("t38", o.course.target_record, w.course.target_record);
            check("state", o.craft.state & ~0x4010, w.craft.state & ~0x4010);
            if (w.craft.state & 0x1000) check("vy", o.craft.vy, w.craft.vy); // (a stopped record keeps its last)
            check("pitch", o.craft.attitude.pitch, w.craft.attitude.pitch);
            check("prate", o.craft.pitch_rate, w.craft.pitch_rate); check("motion", o.craft.motion, w.craft.motion);
            check("h20", o.craft.handling & ~0x40, w.craft.handling & ~0x40); check("26", o.craft.ground_drag, w.craft.ground_drag); // (bit 4: the drawing's mark, $10F9D8) check("timer", o.craft.timer, w.craft.timer);
            if (bd.empty()) ++bogey_matched;
            else if (int(k - first) - bogey_matched < 10) std::printf("frame %d bogey:%s\n", frames[k].number, bd.c_str());
        }
        const auto want = aircraft_from(frames[k]);
        std::string diff;
        const auto check = [&](const char* name, long got, long expected) {
            if (got != expected) diff += std::string(" ") + name + " " + std::to_string(got) + "/" + std::to_string(expected);
        };
        check("x", a.x, want.x); check("y", a.y, want.y); check("z", a.z, want.z);
        check("speed", a.speed, want.speed); check("pitch", a.attitude.pitch, want.attitude.pitch);
        check("heading", a.attitude.heading, want.attitude.heading); check("roll", a.attitude.roll, want.attitude.roll);
        check("throttle", a.throttle, want.throttle); check("gear", a.gear, want.gear);
        ++compared;
        if (diff.empty()) {
            ++matched;
        } else {
            check("surface", a.surface, want.surface); check("motion", a.motion, want.motion);
            check("pitch_rate", a.pitch_rate, want.pitch_rate);
            std::printf("frame %d:%s\n", frames[k].number, diff.c_str());
            if (compared - matched > 20) break;
        }
    }
    std::printf("%d of %d frames match\n", matched, compared);
    if (!bogey.empty()) std::printf("the bogey: %d of %d frames match\nthe radar: %d of %d frames match\n", bogey_matched, compared, radar_matched, compared);
    return matched == compared && (bogey.empty() || bogey_matched == compared) ? 0 : 1;
}

// --- trainercheck ---------------------------------------------------------------

// Research: steps record 4, the maneuvers' trainer, from each frame of a
// `flightcap.py --objs` recording of the maneuvers' demo: its timer, the
// next maneuver's first role where the original started one (`$11756A`),
// the autopilot and the flight step, and compares the next frame.
int trainer_check(const std::filesystem::path& content, const std::filesystem::path& capture) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    const game::Autopilot autopilot(assets.program, tables);
    struct Line {
        Frame frame;
        std::vector<std::uint8_t> trainer, player;
    };
    std::vector<Line> lines;
    std::ifstream in(capture);
    for (std::string text; std::getline(in, text);) {
        if (text.empty()) continue;
        Line l;
        l.frame.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
        l.frame.globals = hex_field(text, "glob");
        const auto objs = hex_field(text, "objs");
        l.trainer.assign(objs.begin() + 3 * 0x200, objs.begin() + 4 * 0x200);
        l.player = hex_field(text, "obj");
        l.frame.object = l.trainer;
        lines.push_back(std::move(l));
    }
    int matched = 0, compared = 0, touches = 0, touches_agree = 0;
    std::optional<game::Aircraft> lead_hit;
    for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
        const auto& now = lines[k];
        const auto& next = lines[k + 1];
        if (next.frame.number != now.frame.number + 1) continue;
        const Record o{now.trainer}, want_o{next.trainer}, g{now.frame.globals}, gn{next.frame.globals};
        if (!(o.u8(1) & 0x40)) continue;
        auto a = aircraft_from(now.frame);
        a.player = false;
        std::uint8_t pilot = o.u8(0x64);
        const bool practice = g.u8(0x13AB96 - 0x13A9E0) == 0x7D;
        if (practice) {
            // The player's touch this frame ($11B6A2 after its move): from
            // its position before the move against the lead as it stands.
            Frame pf{now.frame.number, now.player, now.frame.globals};
            auto player = aircraft_from(pf);
            auto lead = aircraft_from(now.frame);
            std::vector<game::Body> list{{&player, 0, true}, {&lead, 4, true}};
            bool near = false;
            const bool hit = game::touch_bodies(list, 0, {player.x, player.y, player.z}, near) == game::touch::object;
            const bool was_hit = want_o.u8(5) == 1 && o.u8(5) != 1;
            if (hit) lead_hit = lead;
            ++touches;
            if (hit == was_hit) ++touches_agree;
            else std::printf("frame %d: touch %d, original %d\n", next.frame.number, hit, was_hit);
        }
        if (practice && !(a.motion & 0x0100)) {
            // $1172AA ($7D): the lead starts with the player's first move.
            a.role = 0;
            const Record player{next.player}; // (the player has moved this frame)
            if (player.s16(0x6E) != 0) a.motion |= 0x0100;
        }
        // $1172AA: a new maneuver's first role.
        if ((a.motion & 0x0100) && a.role == 0 && want_o.u8(5) != 0 && want_o.u8(5) != 6) {
            const int m = gn.u8(0x13AA89 - 0x13A9E0);
            a.role = std::uint8_t(assets.program.u32(0x11756A + std::uint32_t(8 * m) + 4) & 0xFF);
        }
        if (lead_hit) {
            // (Hit by the player this frame: crashed, spinning, role 1.)
            a.state = lead_hit->state;
            a.handling = lead_hit->handling;
            a.motion = lead_hit->motion;
            a.role = lead_hit->role;
        }
        a.timer = std::int16_t(a.timer - 1); // ($1179A4)
        if (a.state & 0x0200) { // ($1169D0: crashed becomes exploding)
            a.timer = 15;
            a.state = std::uint16_t((a.state & ~0x0200) | 0x0400);
        }
        if ((a.handling & 0x02) && (a.state & 0x1000)) { // ($1179C0)
            a.speed = std::int16_t(a.speed > 0x1200 ? std::max(a.speed - 0x90, 0x1200) : std::min(a.speed + 0x90, 0x1200));
            a.airspeed = a.speed;
            a.motion &= 0xDFF7;
        }
        game::PilotWorld world;
        world.mode = g.u8(0x13AB96 - 0x13A9E0);
        world.practice_ready = std::int8_t(g.u8(0x13AA8A - 0x13A9E0)) < 0;
        world.practice = g.u8(0x13AA83 - 0x13A9E0) != 0;
        autopilot.fly(a, pilot, world);
        game::FlightRules rules;
        rules.stall_model = g.u8(0x13AA74 - 0x13A9E0) != 0;
        rules.bank_turns = g.u8(0x13AA7F - 0x13A9E0) != 0;
        rules.flying = (g.u16(0x13ABBC - 0x13A9E0) & 0x40) != 0;
        rules.ceiling = g.s32(0x13A9EA - 0x13A9E0);
        game::FlightControls c;
        c.recorded = a.controls;
        const auto roll_before = a.attitude.roll;
        if (a.state & 0x1000) game::fly(a, c, rules, tables);
        game::Autopilot::level_out(a, pilot, roll_before);
        if (a.state & 0x0400) a.state &= ~0x1000;
        lead_hit.reset();
        const auto want = aircraft_from(next.frame);
        std::string diff;
        const auto check = [&](const char* name, long got, long expected) {
            if (got != expected) diff += std::string(" ") + name + " " + std::to_string(got) + "/" + std::to_string(expected);
        };
        check("role", a.role, want.role); check("timer", a.timer, want.timer);
        check("ctl", a.controls, want.controls); check("pilot", pilot, want_o.u8(0x64));
        check("x", a.x, want.x); check("y", a.y, want.y); check("z", a.z, want.z);
        check("pitch", a.attitude.pitch, want.attitude.pitch); check("heading", a.attitude.heading, want.attitude.heading);
        check("roll", a.attitude.roll, want.attitude.roll); check("speed", a.speed, want.speed);
        check("h20", a.handling, want.handling);
        ++compared;
        if (diff.empty()) ++matched;
        else std::printf("frame %d role %d:%s\n", next.frame.number, o.u8(5), diff.c_str());
    }
    if (touches) std::printf("the player's touches: %d of %d frames agree\n", touches_agree, touches);
    std::printf("%d of %d frames match\n", matched, compared);
    return matched == compared ? 0 : 1;
}

// --- objectcheck ---------------------------------------------------------------

// One recorded record's step in a frame as objectcheck and areacheck run
// it: the object step (`$11797E`), the trigger areas' turn for home
// (`$11D60C`, by the frame counter), the autopilot and the flight step,
// with the globals of the frame (`g`).
void step_recorded(interceptor::game::WorldObject& o, interceptor::game::Records& all, const Record& g, std::uint32_t list, int frame,
                   const interceptor::game::ObjectStep& step, const interceptor::game::TriggerAreas& areas,
                   const interceptor::game::Autopilot& autopilot, const interceptor::game::FlightTables& tables) {
    using namespace interceptor;
    game::StepWorld world;
    world.mode = g.u8(0x13AB96 - 0x13A9E0);
    world.level = g.s8(0x13AB97 - 0x13A9E0);
    const auto lock = g.s16(0x13ACB0 - 0x13A9E0);
    world.locked = lock < 0 ? -1 : lock / 0x200;
    world.attack_now = g.u8(0x13AAA7 - 0x13A9E0) != 0;
    world.list = list;
    auto& a = o.craft;
    if (a.state & 0x0200) { // ($1169D0: crashed becomes exploding)
        a.timer = 15;
        a.state = std::uint16_t((a.state & ~0x0200) | 0x0400);
    }
    step.before_flight(o, all, world);
    if ((frame & 0x1F) == o.record) areas.send_home(o);
    game::PilotWorld pw;
    pw.mode = world.mode;
    pw.level = world.level;
    autopilot.fly(a, o.pilot, pw, &o.course);
    game::FlightRules rules;
    rules.stall_model = g.u8(0x13AA74 - 0x13A9E0) != 0;
    rules.bank_turns = g.u8(0x13AA7F - 0x13A9E0) != 0;
    rules.flying = true;
    rules.ceiling = g.s32(0x13A9EA - 0x13A9E0);
    game::FlightControls c;
    c.recorded = a.controls;
    const auto roll_before = a.attitude.roll;
    if (a.state & 0x1000) game::fly(a, c, rules, tables);
    game::Autopilot::level_out(a, o.pilot, roll_before);
}

// The fields objectcheck compares ("" when equal).
std::string record_diff(const interceptor::game::WorldObject& o, const interceptor::game::WorldObject& want) {
    const auto& a = o.craft;
    const auto& w = want.craft;
    std::string diff;
    const auto check = [&](const char* name, long got, long expected) {
        if (got != expected) diff += std::string(" ") + name + " " + std::to_string(got) + "/" + std::to_string(expected);
    };
    check("role", a.role, w.role); check("timer", a.timer, w.timer);
    check("ctl", a.controls, w.controls); check("pilot", o.pilot, want.pilot);
    check("x", a.x, w.x); check("y", a.y, w.y); check("z", a.z, w.z);
    check("pitch", a.attitude.pitch, w.attitude.pitch); check("heading", a.attitude.heading, w.attitude.heading);
    check("roll", a.attitude.roll, w.attitude.roll); check("speed", a.speed, w.speed);
    check("throttle", a.throttle, w.throttle);
    check("h20", a.handling & ~0x40, w.handling & ~0x40); // (bit 6: on the radar, `$1260CC`)
    check("tx", o.course.target.cell_x, want.course.target.cell_x); check("tz", o.course.target.cell_z, want.course.target.cell_z);
    check("tfx", o.course.target.fine_x, want.course.target.fine_x); check("tfz", o.course.target.fine_z, want.course.target.fine_z);
    check("ty", o.course.target.height, want.course.target.height); check("t38", o.course.target_record, want.course.target_record);
    check("dist", o.course.distance, want.course.distance); check("b7a", o.course.behaviour, want.course.behaviour);
    check("s39", a.throttle_steps, w.throttle_steps); check("surf", a.surface, w.surface);
    return diff;
}

// Research: steps the given records (other aircraft of a mission) from each
// frame of a `flightcap.py --recs` recording through the object step
// (`$11797E`), the autopilot and the flight step, and compares the next
// frame's record.
int object_check(const std::filesystem::path& content, const std::filesystem::path& capture, const std::string& which) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    const game::Autopilot autopilot(assets.program, tables);
    const game::ObjectLists lists(assets.program);
    const game::ObjectStep step(assets.program, tables, lists);
    const game::TriggerAreas areas(lists);
    std::vector<int> records;
    for (std::size_t at = 0; at < which.size();) {
        const auto comma = which.find(',', at);
        records.push_back(std::stoi(which.substr(at, comma - at)));
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    struct Line {
        int number{};
        std::vector<std::uint8_t> globals, player;
        std::map<int, std::vector<std::uint8_t>> recs;
        std::uint32_t list{};
    };
    std::vector<Line> lines;
    std::ifstream in(capture);
    for (std::string text; std::getline(in, text);) {
        if (text.empty()) continue;
        Line l;
        l.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
        l.globals = hex_field(text, "glob");
        l.player = hex_field(text, "obj");
        l.list = list_start_of(text, Record{l.globals});
        for (int r = 1; r < 16; ++r) {
            if (text.find("\"" + std::to_string(r) + "\": \"") != std::string::npos) l.recs[r] = hex_field(text, std::to_string(r));
        }
        lines.push_back(std::move(l));
    }
    const auto object_from = world_object_from;
    int matched = 0, compared = 0;
    for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
        const auto& now = lines[k];
        const auto& next = lines[k + 1];
        const Record g{now.globals};
        if (next.number != now.number + 1 || !(Record{next.globals}.u8(0x13ABBD - 0x13A9E0) & 0x40)) continue;
        for (const int r : records) {
            if (!now.recs.count(r) || !(Record{now.recs.at(r)}.u8(1) & 0x40)) continue;
            std::vector<game::WorldObject> objects;
            for (const auto& [n, bytes] : now.recs) objects.push_back(object_from(n, bytes, now.globals));
            // (The player's record steps first in a frame.)
            auto player = aircraft_from(Frame{0, next.player, next.globals});
            game::Records all{&player, Record{next.player}.s16(0x4A), &objects};
            auto it = std::find_if(objects.begin(), objects.end(), [&](const game::WorldObject& o) { return o.record == r; });
            step_recorded(*it, all, g, now.list, now.number, step, areas, autopilot, tables);
            const std::string diff = record_diff(*it, object_from(r, next.recs.at(r), next.globals));
            ++compared;
            if (diff.empty()) ++matched;
            else if (compared - matched < 40) std::printf("frame %d rec %d role %d:%s\n", next.number, r, Record{now.recs.at(r)}.u8(5), diff.c_str());
        }
    }
    std::printf("%d of %d record frames match\n", matched, compared);
    return matched == compared ? 0 : 1;
}

// --- areacheck ----------------------------------------------------------------

// Research: the trigger areas (`$11D17A`) on each frame of a `flightcap.py
// --recs` recording: from the frame's records, globals and the player's
// cell, the areas' walk; records it spawns step in the same frame; then
// the bits `$13AA8D`, the count `$13AB99` and every recorded record that
// is active in either are compared with the next frame.
int area_check(const std::filesystem::path& content, const std::filesystem::path& capture) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    const game::Autopilot autopilot(assets.program, tables);
    const game::ObjectLists lists(assets.program);
    const game::ObjectStep step(assets.program, tables, lists);
    struct Line {
        int number{};
        std::vector<std::uint8_t> globals, player;
        std::map<int, std::vector<std::uint8_t>> recs;
        std::uint32_t list{};
    };
    std::vector<Line> lines;
    std::ifstream in(capture);
    for (std::string text; std::getline(in, text);) {
        if (text.empty()) continue;
        Line l;
        l.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
        l.globals = hex_field(text, "glob");
        l.player = hex_field(text, "obj");
        l.list = list_start_of(text, Record{l.globals});
        for (int r = 1; r < 16; ++r) {
            if (text.find("\"" + std::to_string(r) + "\": \"") != std::string::npos) l.recs[r] = hex_field(text, std::to_string(r));
        }
        lines.push_back(std::move(l));
    }
    const auto G = [](std::uint32_t address) { return address - 0x13A9E0; };
    int walks = 0, spawns = 0, matched = 0, compared = 0;
    for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
        const auto& now = lines[k];
        const auto& next = lines[k + 1];
        if (next.number != now.number + 1) continue;
        const Record g{now.globals}, gn{next.globals};
        std::vector<game::WorldObject> objects;
        for (const auto& [n, bytes] : now.recs) objects.push_back(world_object_from(n, bytes, now.globals));
        game::TriggerAreas areas(lists);
        areas.set_inside(g.u8(G(0x13AA8D)));
        int count = g.u8(G(0x13AB99));
        const int limit = g.u8(G(0x13AB9A));
        std::vector<int> spawned;
        if ((now.number & 0xF) == 3 && !g.u8(G(0x13AA80))) {
            ++walks;
            const Record p{now.player};
            game::ObjectChoice choice;
            choice.mode = g.u8(G(0x13AB96));
            choice.level = g.s8(G(0x13AB97));
            choice.variant_random = g.u16(G(0x13ADE8));
            areas.check(p.s16(6), p.s16(8), objects, [&](const game::ObjectLists::TriggerArea& area, int index) {
                std::vector<bool> was;
                for (const auto& o : objects) was.push_back(o.active());
                game::ObjectLists::Spawned offsets;
                const int result = lists.spawn_area(area, index, objects, count, limit, choice, offsets, tables);
                for (std::size_t i = 0; i < objects.size(); ++i) {
                    if (objects[i].active() && !was[i]) spawned.push_back(objects[i].record);
                }
                return result;
            });
        }
        auto player = aircraft_from(Frame{0, next.player, next.globals});
        game::Records all{&player, Record{next.player}.s16(0x4A), &objects};
        for (const int r : spawned) {
            ++spawns;
            auto it = std::find_if(objects.begin(), objects.end(), [&](const game::WorldObject& o) { return o.record == r; });
            step_recorded(*it, all, g, now.list, now.number, step, areas, autopilot, tables);
        }
        std::string diff;
        if (areas.inside() != gn.u8(G(0x13AA8D)))
            diff += " aa8d " + std::to_string(areas.inside()) + "/" + std::to_string(gn.u8(G(0x13AA8D)));
        if (count != gn.u8(G(0x13AB99))) diff += " ab99 " + std::to_string(count) + "/" + std::to_string(gn.u8(G(0x13AB99)));
        for (const auto& [n, bytes] : next.recs) {
            const auto want = world_object_from(n, bytes, next.globals);
            auto it = std::find_if(objects.begin(), objects.end(), [&](const game::WorldObject& o) { return o.record == n; });
            const bool spawned_now = std::find(spawned.begin(), spawned.end(), n) != spawned.end();
            if (it == objects.end()) continue;
            if (it->active() != want.active()) {
                diff += " rec " + std::to_string(n) + " active " + std::to_string(it->active()) + "/" + std::to_string(want.active());
                continue;
            }
            if (!spawned_now) continue;
            const auto d = record_diff(*it, want);
            if (!d.empty()) diff += " rec " + std::to_string(n) + ":" + d;
            if (it->craft.type != want.craft.type || it->area != want.area || (it->craft.state & ~0x6000) != (want.craft.state & ~0x6000))
                diff += " rec " + std::to_string(n) + " type/area/state " + std::to_string(it->craft.type) + "," +
                        std::to_string(it->area) + "," + std::to_string(it->craft.state) + " / " +
                        std::to_string(want.craft.type) + "," + std::to_string(want.area) + "," + std::to_string(want.craft.state);
        }
        ++compared;
        if (diff.empty()) ++matched;
        else if (compared - matched < 30) std::printf("frame %d:%s\n", next.number, diff.c_str());
    }
    std::printf("%d walks, %d spawns; %d of %d frames match\n", walks, spawns, matched, compared);
    return matched == compared ? 0 : 1;
}

// --- missioncheck --------------------------------------------------------------

// Research: a mission's world in lockstep with a `flightcap.py --recs`
// recording of the original: from each frame's records and counters the
// port's ObjectWorld runs the frame as the module does (the trigger areas
// every 16th frame, the "near" bits, the records' step after the player's
// recorded move, the objective while `$13AA88` is 0), then the counters,
// the outcome and every recorded record are compared with the next frame.
int mission_check(const std::filesystem::path& content, const std::filesystem::path& capture) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto world_map = data::read_world(assets.program);
    const auto tables = game::read_flight_tables(assets.program);
    const game::Autopilot autopilot(assets.program, tables);
    const game::ObjectLists lists(assets.program);
    const game::ObjectStep step(assets.program, tables, lists);
    struct Line {
        int number{};
        std::vector<std::uint8_t> globals, player;
        std::map<int, std::vector<std::uint8_t>> recs;
        std::uint32_t list{};
        std::vector<std::uint8_t> moved; // (the player before a follow poke)
        std::vector<std::uint8_t> shots; // (`--objs`: the projectile slots, for the decoys)
    };
    std::vector<Line> lines;
    std::ifstream in(capture);
    for (std::string text; std::getline(in, text);) {
        if (text.empty()) continue;
        Line l;
        l.number = std::stoi(text.substr(text.find("\"frame\": ") + 9));
        l.globals = hex_field(text, "glob");
        l.player = hex_field(text, "obj");
        l.list = list_start_of(text, Record{l.globals});
        if (text.find("\"moved\": \"") != std::string::npos) l.moved = hex_field(text, "moved");
        if (text.find("\"shots\": \"") != std::string::npos) l.shots = hex_field(text, "shots");
        for (int r = 1; r < 16; ++r) {
            if (text.find("\"" + std::to_string(r) + "\": \"") != std::string::npos) l.recs[r] = hex_field(text, std::to_string(r));
        }
        lines.push_back(std::move(l));
    }
    const auto G = [](std::uint32_t address) { return address - 0x13A9E0; };
    const auto counters_of = [&](const Record& g) {
        game::ObjectWorld::Counters c;
        c.aircraft = g.u8(G(0x13AB99));
        c.aircraft_limit = g.u8(G(0x13AB9A));
        c.destroyed = g.u8(G(0x13AB9B));
        c.escort = g.s16(G(0x13AA72));
        c.outcome_wait = g.u8(G(0x13AA9E));
        c.inside = g.u8(G(0x13AA8D));
        c.list_start = 0;
        return c;
    };
    int matched = 0, compared = 0, outcomes = 0, turns = 0;
    std::map<int, int> record_frames;
    for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
        const auto& now = lines[k];
        const auto& next = lines[k + 1];
        const Record g{now.globals}, gn{next.globals};
        // (The frame counter stands in a frame that ends with an outcome
        // awaited, `$1014A6`: such a frame keeps its number. Once the flight
        // stops, its last line repeats.)
        const int advance = gn.u8(G(0x13AA9E)) ? 0 : 1;
        if (next.number != now.number + advance || !(g.u8(G(0x13ABBD)) & 0x40) || !(gn.u8(G(0x13ABBD)) & 0x40)) continue;
        if (advance == 0 && now.globals == next.globals && now.player == next.player) continue;
        // (After an outcome the texts begin: `$1031C8` stops the objects
        // and the flight; the check ends there.)
        if (g.u8(G(0x13AA88)) && !g.u8(G(0x13AA9E)) && gn.u8(G(0x13AA9E))) break;
        // Mission 6's rescue pod: the missile record `$13ABB2` points at
        // (record its own here).
        const int pod_record = g.u16(G(0x13ABB2)) / 0x200;
        std::vector<game::WorldObject> objects;
        std::optional<game::WorldObject> pod;
        for (const auto& [n, bytes] : now.recs) {
            auto o = world_object_from(n, bytes, now.globals);
            o.shape_type = Record{bytes}.u8(0x62) == 0x31 ? 11 : 0;
            if (n == pod_record && pod_record > 0) pod = o;
            else if (n > 3) objects.push_back(o);
        }
        game::ObjectWorld world(tables, lists, step, autopilot);
        auto counters = counters_of(g);
        counters.list_start = now.list;
        world.set(counters, std::move(objects), pod);
        const int mode = g.u8(G(0x13AB96));
        const int level = g.s8(G(0x13AB97));
        if ((now.number & 0xF) == 3 && !g.u8(G(0x13AA80))) {
            const auto before = aircraft_from(Frame{0, now.player, now.globals});
            world.trigger_areas(before, mode, level, [&] { return int(g.u16(G(0x13ADE8))); });
        }
        world.clear_near();
        // (The player as its step left it: before a follow poke when one
        // was made.)
        const auto& moved = next.moved.empty() ? next.player : next.moved;
        auto player = aircraft_from(Frame{0, moved, next.globals});
        game::ObjectWorld::Frame f;
        f.player = &player;
        f.player_distance = Record{moved}.s16(0x4A);
        const auto lock = g.s16(G(0x13ACB0));
        f.locked = lock < 0 ? -1 : lock / 0x200;
        f.mode = std::uint8_t(mode);
        f.level = std::int8_t(level);
        f.counter = now.number;
        f.world = &world_map;
        f.bodies = {{&player, 0, true}};
        f.ecm = g.u8(G(0x13ABA5)) != 0;
        // The decoys (`$13AB7C` flare, `$13AB7D` chaff: a projectile slot,
        // 64-byte units, plus 1) as the frame starts.
        const auto decoy = [&](std::uint32_t at) -> std::optional<std::array<std::int32_t, 3>> {
            const int n = g.u8(G(at));
            if (n == 0 || now.shots.size() < std::size_t(n) * 64) return std::nullopt;
            const Record s{std::vector<std::uint8_t>(now.shots.begin() + (n - 1) * 64, now.shots.begin() + n * 64)};
            return std::array<std::int32_t, 3>{s.s16(0x30) * 0x400000 + s.s32(0), s.s32(4), s.s16(0x32) * 0x400000 + s.s32(8)};
        };
        f.flare = decoy(0x13AB7C);
        f.chaff = decoy(0x13AB7D);
        // (A recording without the missile records cannot tell a free one
        // from one in flight: no launches then.)
        f.launches = now.recs.count(9) && now.recs.count(11) && now.recs.count(13);
        {
            // The player's touch, in its step before the records' (the
            // module's `player_touches`): what it hits crashes.
            // (From where its step started: the logged place, after any
            // follow poke.)
            const auto from = aircraft_from(Frame{0, now.player, now.globals});
            std::vector<game::Body> list{{&player, 0, true}};
            for (auto& o : world.objects()) list.push_back({&o.craft, o.group, o.active()});
            bool near = false;
            // (Not while an outcome is awaited: `$11A364` skips the record
            // step, the player's too; and only while the logged record is
            // the player's, `$13ABCE` 0 - the camera's turn points it at
            // record 4.)
            if (!g.u8(G(0x13AA9E)) && g.u16(G(0x13ABCE)) == 0 && gn.u16(G(0x13ABCE)) == 0)
                game::touch_bodies(list, 0, {from.x, from.y, from.z}, near);
        }
        world.step(f);
        std::string diff;
        if (g.u8(G(0x13AA88)) == 0 && mode >= 3 && mode <= 8) {
            const auto outcome = world.objective(mode, player, level, gn.s8(G(0x13AB5F)));
            const int result = outcome.result;
            const int got = result == 1 ? 0xFF : result == -2 ? 0xFE : result == -3 ? 0xFD : 0;
            const int want = gn.u8(G(0x13AA88));
            if (want) ++outcomes;
            if (got != want) diff += " aa88 " + std::to_string(got) + "/" + std::to_string(want);
            // The camera's turn (`$13AAAE` bit 7 set, position 7, the
            // outside view's distance `$13AB23`) and the view following
            // record 4 (`$13ABCC`, the texts' extra frames `$13ABB0`).
            // (The view is kept even when record 4 is gone; the turn is
            // made only while it is in use.)
            const bool in_use = next.recs.count(4) && (Record{next.recs.at(4)}.u8(1) & 0x40);
            const bool turned = (gn.u8(G(0x13AAAE)) & 0x80) && !(g.u8(G(0x13AAAE)) & 0x80) && in_use &&
                                gn.u8(G(0x13ABA2)) == 7;
            if (outcome.turn != turned) diff += std::string(" turn ") + (outcome.turn ? "1/0" : "0/1");
            if (outcome.turn && turned && (gn.u16(G(0x13ABB6)) & 2) && gn.s8(G(0x13AB23)) != outcome.turn_distance)
                diff += " ab23 " + std::to_string(outcome.turn_distance) + "/" + std::to_string(gn.s8(G(0x13AB23)));
            if (outcome.turn) ++turns;
            const bool viewed = gn.s16(G(0x13ABCC)) == 4 && g.s16(G(0x13ABCC)) != 4;
            if (outcome.view != viewed) diff += std::string(" view ") + (outcome.view ? "1/0" : "0/1");
            if (outcome.view && viewed && gn.s16(G(0x13ABB0)) != outcome.extra)
                diff += " abb0 " + std::to_string(outcome.extra) + "/" + std::to_string(gn.s16(G(0x13ABB0)));
        }
        const auto c = world.counters(), w = counters_of(gn);
        const auto check = [&](const char* name, long got, long expected) {
            if (got != expected) diff += std::string(" ") + name + " " + std::to_string(got) + "/" + std::to_string(expected);
        };
        check("ab99", c.aircraft, w.aircraft); check("ab9a", c.aircraft_limit, w.aircraft_limit);
        check("ab9b", c.destroyed, w.destroyed); check("aa72", c.escort, w.escort);
        check("aa9e", c.outcome_wait, w.outcome_wait); check("aa8d", c.inside, w.inside);
        if (pod_record > 0 && world.pod() && next.recs.count(pod_record)) {
            const auto want = world_object_from(pod_record, next.recs.at(pod_record), next.globals);
            const auto& p = world.pod()->craft;
            const auto& w = want.craft;
            std::string d;
            const auto pcheck = [&](const char* name, long got, long expected) {
                if (got != expected) d += std::string(" ") + name + " " + std::to_string(got) + "/" + std::to_string(expected);
            };
            pcheck("x", p.x, w.x); pcheck("y", p.y, w.y); pcheck("z", p.z, w.z);
            pcheck("vx", p.vx, w.vx); pcheck("vy", p.vy, w.vy); pcheck("vz", p.vz, w.vz);
            pcheck("timer", p.timer, w.timer); pcheck("state", p.state & ~0x6010, w.state & ~0x6010);
            if (!d.empty()) diff += " pod:" + d;
            else ++record_frames[pod_record];
        }
        for (const auto& [n, bytes] : next.recs) {
            if (n <= 3) continue; // (the player's missiles; the pod above)
            const auto want = world_object_from(n, bytes, next.globals);
            const auto* got = world.record(n);
            if (!got) continue;
            if (got->active() != want.active()) {
                diff += " rec " + std::to_string(n) + " active " + std::to_string(got->active()) + "/" + std::to_string(want.active());
                continue;
            }
            if (!want.active()) continue;
            if ((want.craft.type & 0xF0) == 0) { // a missile record (an enemy's)
                ++record_frames[n];
                const auto& a = got->craft;
                const auto& w = want.craft;
                std::string d;
                const auto mcheck = [&](const char* name, long got_v, long expected) {
                    if (got_v != expected) d += std::string(" ") + name + " " + std::to_string(got_v) + "/" + std::to_string(expected);
                };
                mcheck("x", a.x, w.x); mcheck("y", a.y, w.y); mcheck("z", a.z, w.z);
                mcheck("vx", a.vx, w.vx); mcheck("vy", a.vy, w.vy); mcheck("vz", a.vz, w.vz);
                mcheck("speed", a.speed, w.speed); mcheck("life", a.timer, w.timer);
                mcheck("pitch", a.attitude.pitch, w.attitude.pitch); mcheck("heading", a.attitude.heading, w.attitude.heading);
                mcheck("state", a.state & ~0x6010, w.state & ~0x6010); mcheck("type", a.type, w.type);
                mcheck("tx", got->course.target.cell_x, want.course.target.cell_x);
                mcheck("tfx", got->course.target.fine_x, want.course.target.fine_x);
                mcheck("ty", got->course.target.height, want.course.target.height);
                mcheck("t38", got->course.target_record, want.course.target_record);
                if (!d.empty()) diff += " missile " + std::to_string(n) + ":" + d;
                continue;
            }
            if ((want.craft.type & 0xF0) != 0x10) continue;
            ++record_frames[n];
            auto d = record_diff(*got, want);
            if (got->stores != want.stores) d += " s5f " + std::to_string(got->stores) + "/" + std::to_string(want.stores);
            // (State bit 4: the drawer's mark, cleared as its map scan lists
            // the record, `$110496`.)
            if ((got->craft.state & ~0x6010) != (want.craft.state & ~0x6010))
                d += " state " + std::to_string(got->craft.state) + "/" + std::to_string(want.craft.state);
            if (!d.empty()) diff += " rec " + std::to_string(n) + ":" + d;
        }
        ++compared;
        if (diff.empty()) ++matched;
        else if (compared - matched < 40) std::printf("frame %d:%s\n", next.number, diff.c_str());
    }
    std::printf("records stepped:");
    for (const auto& [n, frames] : record_frames) std::printf(" %d (%d)", n, frames);
    std::printf("\n%d outcome frames, %d camera turns; %d of %d frames match\n", outcomes, turns, matched, compared);
    return matched == compared ? 0 : 1;
}

// --- hitprobe ------------------------------------------------------------------

// Research: mission 3's bogey as spawned; a round stepped along each axis
// through it, printing where the hit test finds it inside.
int hit_probe(const std::filesystem::path& content) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    const auto tables = game::read_flight_tables(assets.program);
    const game::ObjectLists lists(assets.program);
    game::ObjectChoice choice;
    choice.mode = 3;
    auto objects = lists.spawn(choice, tables).objects;
    std::vector<game::WorldObject> bogey;
    for (auto& o : objects) if ((o.craft.type & 0xF0) == 0x10) bogey.push_back(o);
    if (bogey.empty()) return 1;
    const game::ShotHits hits(assets.program, tables);
    for (int axis = 0; axis < 3; ++axis) {
        std::string line;
        for (int d = -400; d <= 400; d += 16) {
            game::Shot s;
            s.x = bogey[0].craft.x + (axis == 0 ? d << 8 : 0);
            s.y = bogey[0].craft.y + (axis == 1 ? d << 8 : 0);
            s.z = bogey[0].craft.z + (axis == 2 ? d << 8 : 0);
            s.x -= 0; // (eighths of zero: tested where it stands, eight times)
            auto copy = bogey;
            int n = 0;
            line += hits.test(s, copy, n, {bogey[0].craft.x, bogey[0].craft.y, bogey[0].craft.z - (200 << 8)}) ? '#' : '.';
        }
        std::printf("axis %d: %s\n", axis, line.c_str());
    }
    return 0;
}

// --- hudcheck ------------------------------------------------------------------

// Research: draws the cockpit for given readings and compares the head-up
// display's glass (colours 10 and 13, rows 40-135, columns 86-232) with a
// colour-number dump of the original's screen (`screen.py` F18_INDEX, two
// hex digits a low-res pixel, rows 1 apart).
int hud_check(const std::filesystem::path& content, const std::filesystem::path& dump, int feet, int knots,
              int heading80, int g_display, bool f16, int weapon = 0, int count = 0,
              std::optional<std::array<int, 2>> pipper = {}) {
    using namespace interceptor;
    const auto assets = data::load_assets(content);
    game::Cockpit cockpit(assets);
    game::IndexedImage image(320, 200);
    game::CockpitReadings r;
    r.feet = feet; r.knots = knots; r.heading80 = heading80; r.g_display = g_display; r.f16 = f16;
    r.weapon = weapon; r.weapon_count = count; r.pipper = pipper;
    cockpit.draw(image, 0, r);
    std::ifstream in(dump);
    std::vector<std::string> rows;
    for (std::string line; std::getline(in, line);) rows.push_back(line);
    int differ = 0;
    for (int y = 40; y <= 135; ++y) {
        std::string port, original;
        for (int x = 86; x <= 232; ++x) {
            const int ours = image.pixels[std::size_t(y * 320 + x)];
            const int theirs = std::stoi(rows[std::size_t(y)].substr(std::size_t(2 * x), 2), nullptr, 16);
            const auto mark = [](int c) { return c == 13 ? 'd' : c == 10 ? 'a' : '.'; };
            port += mark(ours);
            original += mark(theirs);
        }
        if (port != original) {
            for (std::size_t i = 0; i < port.size(); ++i) differ += port[i] != original[i];
            std::printf("%3d port %s\n    orig %s\n", y, port.c_str(), original.c_str());
        }
    }
    std::printf("%d pixels differ\n", differ);
    return differ == 0 ? 0 : 1;
}

int fly_script(const std::filesystem::path& content, const std::string& prefix, int argc, char** argv) {
    auto game = interceptor::game::start(content);
    std::map<int, std::string> steps;
    std::vector<int> shots, subshots; // `!sub`: four pictures between the step before and this one (smooth motion)
    std::vector<int> reloads;         // `!reload`: a quick save loaded into a fresh module (the save's check)
    std::vector<int> twins;           // `!twin`: the same, run beside the module and compared with it
    std::unique_ptr<app::GameModule> twin;
    int twin_from = -1, twin_differs = -1;
    std::vector<float> twin_chunk;
    std::vector<std::pair<std::size_t, int>> options;
    bool wav = false;
    constexpr int rate = 44100;
    std::vector<float> sound, chunk;
    double owed = 0;
    int last = 0;
    for (int i = 0; i < argc; ++i) {
        const std::string step = argv[i];
        const auto colon = step.find(':');
        const int tick = std::stoi(step.substr(0, colon));
        const std::string keys = step.substr(colon + 1);
        if (keys == "!png") shots.push_back(tick);
        else if (keys.rfind("!option=", 0) == 0) { // `0:!option=6,1`: an option set before the start
            const auto comma = keys.find(',');
            game->set_option(std::size_t(std::stoi(keys.substr(8, comma - 8))), std::stoi(keys.substr(comma + 1)));
            options.emplace_back(std::size_t(std::stoi(keys.substr(8, comma - 8))), std::stoi(keys.substr(comma + 1)));
        } else if (keys == "!sub") subshots.push_back(tick);
        else if (keys == "!reload") reloads.push_back(tick);
        else if (keys == "!twin") twins.push_back(tick);
        else if (keys == "!wav") wav = true;
        else steps[tick] = keys;
        last = std::max(last, tick);
    }
    std::string held;
    // Touch controls (`?stick=X,Y` held until the next touch step, `?throttle=Y`
    // a slider move, `?gear` / `?view` a tap) and taps on the picture (`?tap=X,Y`).
    std::optional<std::array<float, 2>> stick;
    for (int tick = 0; tick <= last; ++tick) {
        app::GameInput input;
        if (steps.count(tick) && steps[tick].rfind("?", 0) == 0) {
            const auto t = steps[tick];
            if (t.rfind("?stick=", 0) == 0) {
                const auto comma = t.find(',');
                stick = std::array<float, 2>{std::stof(t.substr(7, comma - 7)), std::stof(t.substr(comma + 1))};
            } else if (t.rfind("?throttle=", 0) == 0) {
                platform::TouchValue v;
                v.id = "throttle";
                v.y = std::stof(t.substr(10));
                v.changed = true;
                input.touch_controls.push_back(v);
            } else if (t.rfind("?tap=", 0) == 0) {
                const auto comma = t.find(',');
                platform::PointerEvent e;
                e.kind = platform::PointerEvent::Kind::down;
                e.x = std::stof(t.substr(5, comma - 5));
                e.y = std::stof(t.substr(comma + 1));
                e.touch = true;
                input.pointer_events.push_back(e);
            } else if (t == "?gear" || t == "?view") {
                platform::TouchValue v;
                v.id = t.substr(1);
                v.pressed = v.held = true;
                input.touch_controls.push_back(v);
            }
        } else if (steps.count(tick)) {
            held = steps[tick] == "." ? "" : steps[tick];
        }
        if (stick) {
            platform::TouchValue v;
            v.id = "stick";
            v.x = (*stick)[0];
            v.y = (*stick)[1];
            v.held = true;
            input.touch_controls.push_back(v);
        }
        const auto has = [&](char c) { return held.find(c) != std::string::npos; };
        input.up = has('u'); input.down = has('d'); input.left = has('l'); input.right = has('r');
        input.c64_keys[53] = has('+'); input.c64_keys[43] = has('-');
        input.c64_keys[47] = has('<'); input.c64_keys[44] = has('>');
        input.c64_keys[26] = has('g'); input.c64_keys[28] = has('b'); input.c64_keys[0] = has('s');
        const int digit_keys[10] = {35, 56, 59, 8, 11, 16, 19, 24, 27, 32};
        for (int d = 0; d < 10; ++d) input.c64_keys[std::size_t(digit_keys[d])] = has(char('0' + d));
        // The front end: * Return, ~ Tab (Esc), ^ Ctrl+Tab (Shift-Esc), _ the
        // space bar, capitals the letter keys.
        input.c64_keys[1] = has('*');
        input.c64_keys[63] = has('~');
        input.ctrl_keys[63] = has('^');
        input.ctrl_keys[53] = has('('); // Ctrl+= (nearer, zoom in)
        input.ctrl_keys[43] = has(')'); // Ctrl+- (farther, zoom out)
        input.fire = has('_');
        input.f1 = has('#'); // F1 (the first mission); with ! (Shift) F2
        input.f3 = has('%');
        input.f5 = has('&');
        input.shift = has('!');
        // The keypad's view keys: @ 0, [ 4, ] 6, { 8, } 2.
        input.fire2 = has('@'); input.left2 = has('['); input.right2 = has(']');
        input.up2 = has('{'); input.down2 = has('}');
        static constexpr std::pair<char, int> letters[] = {
            {'A', 10}, {'B', 28}, {'C', 20}, {'D', 18}, {'E', 14}, {'F', 21}, {'G', 26}, {'H', 29}, {'I', 33},
            {'J', 34}, {'K', 37}, {'L', 42}, {'M', 36}, {'N', 39}, {'O', 38}, {'P', 41}, {'Q', 62}, {'R', 17},
            {'S', 13}, {'T', 22}, {'U', 30}, {'V', 31}, {'W', 9},  {'X', 23}, {'Y', 25}, {'Z', 12}};
        for (const auto& [c, k] : letters) input.c64_keys[std::size_t(k)] = input.c64_keys[std::size_t(k)] || has(c);
        if (std::find(reloads.begin(), reloads.end(), tick) != reloads.end()) {
            const auto saved = game->write_state();
            game = interceptor::game::start(content);
            for (const auto& [index, value] : options) game->set_option(index, value);
            game->read_state(saved);
            std::printf("tick %d: reloaded a save of %zu bytes\n", tick, saved.size());
        }
        if (std::find(twins.begin(), twins.end(), tick) != twins.end()) {
            const auto saved = game->write_state();
            twin = interceptor::game::start(content);
            for (const auto& [index, value] : options) twin->set_option(index, value);
            twin->read_state(saved);
            twin_from = tick;
            twin_differs = -1;
        }
        if (twin && twin_differs < 0) {
            const auto a = game->picture(), b = twin->picture();
            const auto same = [](const platform::Color& x, const platform::Color& y) {
                return x.r == y.r && x.g == y.g && x.b == y.b;
            };
            if (a.pixels.size() != b.pixels.size() || !std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(), same))
                twin_differs = tick;
            if (game->write_state() != twin->write_state()) twin_differs = tick;
        }
        if (std::find(shots.begin(), shots.end(), tick) != shots.end()) {
            platform::save_png(game->picture(), prefix + std::to_string(tick) + ".png");
        }
        if (std::find(subshots.begin(), subshots.end(), tick) != subshots.end()) {
            for (int k = 1; k <= 4; ++k) {
                game->set_frame_fraction(k / 4.0);
                platform::save_png(game->picture(), prefix + std::to_string(tick) + "_" + std::to_string(k) + ".png");
            }
            game->set_frame_fraction(1);
        }
        game->tick(input);
        if (twin) {
            twin->tick(input);
            chunk.assign(std::size_t(rate * game->step_seconds()) * 2, 0.0f);
            twin_chunk.assign(chunk.size(), 0.0f);
            game->audio(chunk, rate);
            twin->audio(twin_chunk, rate);
            if (chunk != twin_chunk && twin_differs < 0) twin_differs = tick;
        }
        if (wav) {
            owed += rate * game->step_seconds();
            const auto frames = std::size_t(owed);
            owed -= double(frames);
            chunk.assign(frames * 2, 0.0f);
            game->audio(chunk, rate);
            sound.insert(sound.end(), chunk.begin(), chunk.end());
        }
    }
    if (twin) {
        if (twin_differs < 0) std::printf("twin from tick %d: pictures, sound and state equal to the end\n", twin_from);
        else std::printf("twin from tick %d: differs at tick %d\n", twin_from, twin_differs);
    }
    if (wav) {
        // 16-bit stereo PCM.
        std::vector<std::uint8_t> out;
        const auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i))); };
        const auto put16 = [&](std::uint16_t v) { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); };
        const auto text = [&](const char* s) { out.insert(out.end(), s, s + 4); };
        const auto bytes = std::uint32_t(sound.size() * 2);
        text("RIFF"); put32(36 + bytes); text("WAVE"); text("fmt "); put32(16); put16(1); put16(2);
        put32(rate); put32(rate * 4); put16(4); put16(16); text("data"); put32(bytes);
        for (float s : sound) put16(std::uint16_t(std::int16_t(std::clamp(s, -1.0f, 1.0f) * 32767)));
        platform::write_file(prefix + ".wav", out);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    using namespace interceptor::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = interceptor::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
            const auto out = platform::utf8_path(argv[3]);
            for (const auto& file : result.files) {
                std::filesystem::create_directories((out / file.name).parent_path());
                platform::write_file(out / file.name, file.bytes);
            }
            std::printf("%s: %zu files\n", result.release.c_str(), result.files.size());
            return 0;
        }
        if (command == "export" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (const auto& picture : assets.pictures) {
                platform::save_png(picture_image(picture.picture), out / (file_name(picture.file) + ".png"));
            }
            platform::save_png(text_screen(assets, {6, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109}, 0, 12),
                               out / "menu.png");
            for (const auto& block : assets.texts) {
                std::printf("text %3d @%06X row %3d col %2d\n", block.number, block.address, block.row, block.column);
                for (const auto& line : block.lines) {
                    std::printf("    flags %02X colour %2d speed %2d delay %3d |%s|\n", line.flags, line.colour,
                                line.speed, line.delay, line.text.c_str());
                }
                char name[32];
                std::snprintf(name, sizeof name, "text%03d.png", block.number);
                platform::save_png(text_screen(assets, {block.number}, 0), out / name);
            }
            for (std::size_t i = 0; i < assets.messages.size(); ++i) {
                std::printf("message %2zu |%s|\n", i, assets.messages[i].c_str());
            }
            platform::save_png(message_sheet(assets), out / "messages.png");
            platform::save_png(font_sheet(assets), out / "fonts.png");
            platform::save_png(palette_sheet(assets), out / "palettes.png");
            for (const auto& shape : read_vehicle_shapes(assets.program)) {
                double span = 0, length = 0, height = 0;
                for (const auto& f : shape.faces)
                    for (const auto& c : f.corners) {
                        span = std::max(span, std::abs(c.x));
                        height = std::max(height, std::abs(c.y));
                        length = std::max(length, std::abs(c.z));
                    }
                std::printf("shape %-45s model $%06X levels %d faces %3zu lines %3zu scale %g extent x %.0f y %.0f z %.0f\n",
                            shape_title(shape).c_str(), shape.model, shape.detail_levels, shape.faces.size(),
                            shape.lines.size(), shape.scale, span, height, length);
                for (const auto& problem : shape.problems) std::printf("    stopped: %s\n", problem.c_str());
            }
            for (int type = 0; type < 160; ++type) {
                if (shape_family(assets.program, type) != ShapeFamily::ground) continue;
                for (const auto& level : read_ground_levels(assets.program, type)) {
                    if (!level.shape) continue;
                    std::printf("ground %3d nearer than %8.0f faces %3zu lines %3zu\n", type, level.nearer_than,
                                level.shape->faces.size(), level.shape->lines.size());
                    for (const auto& problem : level.shape->problems) std::printf("    stopped: %s\n", problem.c_str());
                }
            }
            for (const auto& sound : assets.sounds) {
                const int rate = int(pal_clock / sound.period + 0.5);
                std::printf("sound %-14s slot %2d period %3d (%5d Hz) %6zu bytes\n", sound.file.c_str(), sound.slot,
                            sound.period, rate, sound.samples.size());
                save_wav(out / (file_name(sound.file) + ".wav"), sound.samples, rate);
            }
            return 0;
        }
        if (command == "fly" && argc >= 5) {
            return fly_script(platform::utf8_path(argv[2]), argv[3], argc - 4, argv + 4);
        }
        if (command == "trainercheck" && argc == 4) {
            return trainer_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "hitprobe" && argc == 3) {
            return hit_probe(platform::utf8_path(argv[2]));
        }
        if (command == "objectcheck" && argc == 5) {
            return object_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), argv[4]);
        }
        if (command == "areacheck" && argc == 4) {
            return area_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "missioncheck" && argc == 4) {
            return mission_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "democheck" && argc == 4) {
            return demo_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "hudcheck" && (argc == 9 || argc == 13)) {
            // (feet knots heading80 g f16 [weapon count pipper_x pipper_y])
            std::optional<std::array<int, 2>> pipper;
            if (argc == 13) pipper = std::array<int, 2>{std::stoi(argv[11]), std::stoi(argv[12])};
            return hud_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), std::stoi(argv[4]),
                             std::stoi(argv[5]), std::stoi(argv[6]), std::stoi(argv[7]), std::stoi(argv[8]) != 0,
                             argc == 13 ? std::stoi(argv[9]) : 0, argc == 13 ? std::stoi(argv[10]) : 0, pipper);
        }
        if (command == "trailcheck" && argc == 5) {
            return trail_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), std::stoi(argv[4]));
        }
        if (command == "stickcheck") return stick_check();
        if (command == "sceneshot" && argc == 10) {
            return scene_shot(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), std::stod(argv[4]),
                              std::stod(argv[5]), std::stod(argv[6]), std::stod(argv[7]), std::stod(argv[8]),
                              std::string(argv[9]) == "high");
        }
        if (command == "effectshot" && argc == 6) {
            return effect_shot(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), std::stoi(argv[4]),
                               platform::utf8_path(argv[5]));
        }
        if (command == "weaponcheck" && argc == 4) {
            return weapon_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "flightcheck" && argc == 4) {
            return flight_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "soundcheck" && argc == 4) {
            return sound_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]));
        }
        if (command == "viewcheck" && argc == 5) {
            return view_check(platform::utf8_path(argv[2]), platform::utf8_path(argv[3]), platform::utf8_path(argv[4]));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: fa_18_interceptor_assets import <image.adf> <content> | export <content> <out>\n");
    return 2;
}
