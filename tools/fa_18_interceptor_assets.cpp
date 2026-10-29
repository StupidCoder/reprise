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
#include "fa_18_interceptor_amiga/game/messages.hpp"
#include "fa_18_interceptor_amiga/game/module.hpp"
#include "fa_18_interceptor_amiga/game/sound_driver.hpp"
#include "fa_18_interceptor_amiga/game/sounds.hpp"
#include "fa_18_interceptor_amiga/game/text_screen.hpp"
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
    a.stall_warning = g.u8(0x13AE43 - 0x13A9E0) & 0x80;
    a.fuel_warning = std::uint8_t(g.u8(0x13AE43 - 0x13A9E0) & 0x43);
    a.supersonic = g.u16(0x13ABC2 - 0x13A9E0) & 0x4000;
    a.gear_speed_warned = g.u8(0x13AB36 - 0x13A9E0) != 0;
    return a;
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
        if ((proc == "1039F0" || proc == "103A36") && typing && text.tick()) sounds.click();
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

int fly_script(const std::filesystem::path& content, const std::string& prefix, int argc, char** argv) {
    auto game = interceptor::game::start(content);
    std::map<int, std::string> steps;
    std::vector<int> shots;
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
        else if (keys == "!wav") wav = true;
        else steps[tick] = keys;
        last = std::max(last, tick);
    }
    std::string held;
    for (int tick = 0; tick <= last; ++tick) {
        if (steps.count(tick)) held = steps[tick] == "." ? "" : steps[tick];
        app::GameInput input;
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
        input.fire = has('_');
        // The keypad's view keys: @ 0, [ 4, ] 6, { 8, } 2.
        input.fire2 = has('@'); input.left2 = has('['); input.right2 = has(']');
        input.up2 = has('{'); input.down2 = has('}');
        static constexpr std::pair<char, int> letters[] = {
            {'A', 10}, {'B', 28}, {'C', 20}, {'D', 18}, {'E', 14}, {'F', 21}, {'G', 26}, {'H', 29}, {'I', 33},
            {'J', 34}, {'K', 37}, {'L', 42}, {'M', 36}, {'N', 39}, {'O', 38}, {'P', 41}, {'Q', 62}, {'R', 17},
            {'S', 13}, {'T', 22}, {'U', 30}, {'V', 31}, {'W', 9},  {'X', 23}, {'Y', 25}, {'Z', 12}};
        for (const auto& [c, k] : letters) input.c64_keys[std::size_t(k)] = input.c64_keys[std::size_t(k)] || has(c);
        if (std::find(shots.begin(), shots.end(), tick) != shots.end()) {
            platform::save_png(game->picture(), prefix + std::to_string(tick) + ".png");
        }
        game->tick(input);
        if (wav) {
            owed += rate * game->step_seconds();
            const auto frames = std::size_t(owed);
            owed -= double(frames);
            chunk.assign(frames * 2, 0.0f);
            game->audio(chunk, rate);
            sound.insert(sound.end(), chunk.begin(), chunk.end());
        }
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
                std::printf("shape %-45s model $%06X levels %d faces %3zu lines %3zu\n", shape_title(shape).c_str(),
                            shape.model, shape.detail_levels, shape.faces.size(), shape.lines.size());
                for (const auto& problem : shape.problems) std::printf("    stopped: %s\n", problem.c_str());
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
