#include "tower_of_babel_amiga/game/panel.hpp"
#include "tower_of_babel_amiga/front/front_end.hpp"
// Tower of Babel research tool: imports the disk image into a content folder
// and exports the decoded data (.analysis/tower_of_babel_amiga/formats.md).
//
//   tower_of_babel_assets import <image.adf> <content folder>
//   tower_of_babel_assets export <content folder> <output folder>
//   tower_of_babel_assets models <content folder> <out.png>   (contact sheet of the shapes)
//   tower_of_babel_assets animcheck <content folder> <animrec.txt>   (harness/animrec.py)
//   tower_of_babel_assets logiccheck <content> <logicrec.txt>   (harness/logicrec.py; research)
//   tower_of_babel_assets objectcheck <content> <logicrec.txt>  (logicrec.py --objects: moves and objects)
//   tower_of_babel_assets viewcheck <content folder> <memory.bin> <group> <tower> <out.png>
//
// viewcheck (research): takes the camera from a reference machine's chip
// memory dump (harness/viewgrab.py, at $87EE after the view is drawn),
// draws the tower natively and compares the view window with the buffer the
// original drew; also checks the cell flags against the original's grid.
//
// The export writes the title screen and every tower map as PNG, the sampled
// pieces and effect waveforms as WAV (8-bit, at the rate their period gives;
// waveforms at 8,363 Hz), and a tower list on stdout.

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/data/model.hpp"
#include "tower_of_babel_amiga/data/preview.hpp"
#include "ui/draw.hpp"
#include "tower_of_babel_amiga/game/animation.hpp"
#include "tower_of_babel_amiga/game/rules.hpp"
#include "tower_of_babel_amiga/game/tower_state.hpp"
#include "tower_of_babel_amiga/game/tower_map.hpp"
#include "tower_of_babel_amiga/view/overlay.hpp"
#include "tower_of_babel_amiga/view/polygon.hpp"
#include "tower_of_babel_amiga/view/sky.hpp"
#include "tower_of_babel_amiga/view/tower_view.hpp"
#include "tower_of_babel_amiga/import/disk_import.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <tuple>
#include <sstream>
#include <exception>
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

// Research only: the original's memory, read by address.
struct Dump {
    std::vector<std::uint8_t> m;
    int u8(std::uint32_t a) const { return m.at(a); }
    int s16(std::uint32_t a) const { return std::int16_t(m.at(a) << 8 | m.at(a + 1)); }
    std::uint32_t u32(std::uint32_t a) const { return std::uint32_t(s16(a) & 0xFFFF) << 16 | std::uint32_t(s16(a + 2) & 0xFFFF); }
    int var(std::uint32_t offset) const { return s16(0x806 + offset); } // A6-relative word
};

// Research: the group and tower whose name the original has in memory ($D22).
std::pair<int, int> find_tower(const babel::data::Assets& assets, const Dump& d) {
    std::string name;
    for (std::uint32_t a = 0x806 + 0xD22; d.u8(a) >= 0x20 && name.size() < 62; ++a) name.push_back(char(d.u8(a)));
    for (std::size_t g = 0; g < assets.groups.size(); ++g)
        for (std::size_t t = 0; t < assets.groups[g].towers.size(); ++t)
            if (assets.groups[g].towers[t].name == name) return {int(g), int(t)};
    std::printf("no tower named \"%s\"\n", name.c_str());
    return {-1, -1};
}

// Research: the variables A6+$1100.. of one logicrec line, read into the
// native state and written back for comparison.
struct Vars {
    std::vector<std::uint8_t> b;
    // logicrec.py records A6+$E00..$1FFF (older recordings A6+$1100..).
    int base() const { return b.size() == 0xF00 ? 0x1100 : 0xE00; }
    int u8(int a) const {
        const int i = a - base();
        return i >= 0 && std::size_t(i) < b.size() ? b[std::size_t(i)] : 0;
    }
    int s16(int a) const { return std::int16_t(u8(a) << 8 | u8(a + 1)); }
    std::uint32_t u32(int a) const { return std::uint32_t(s16(a) & 0xFFFF) << 16 | std::uint32_t(s16(a + 2) & 0xFFFF); }
};

babel::game::TowerState state_from(const Vars& v) {
    using namespace babel::game;
    TowerState t;
    for (CellIndex i = 0; i < 500; ++i) t.set_cell(i, std::uint8_t(v.u8(0x1C5A + i)));
    for (CellIndex i = 100; i < 500; ++i) t.set_object_at(i, std::int8_t(v.u8(0x1E4E + i - 100)));
    for (int n = 0; n < TowerState::objects_max; ++n) {
        const int r = 0x15DA + n * 32;
        auto& o = t.objects[std::size_t(n)];
        // Cells are kept 100 on (a ground row first); the cameras' +$14 / +$16 are offsets.
        const int shift = n < 4 ? 0 : 100;
        o = {v.s16(r), v.u32(r + 2), v.s16(r + 6), v.s16(r + 8), v.s16(r + 10), v.s16(r + 12), v.s16(r + 14),
             v.s16(r + 16), v.s16(r + 18), v.s16(r + 20) ? v.s16(r + 20) + shift : 0, v.s16(r + 22) ? v.s16(r + 22) + shift : 0,
             v.s16(r + 24), v.s16(r + 26), v.s16(r + 28), v.s16(r + 30)};
        if (o.type == 0xD && o.counter_1c != 0) o.counter_1c += 100; // a Lizard's +$1C is a cell
    }
    for (int n = 0; n < 7; ++n) {
        const int r = 0x156A + n * 16;
        auto& p = t.programs[std::size_t(n)];
        p.command = v.s16(r); p.word2 = v.s16(r + 2); p.mode = v.s16(r + 4); p.position = v.s16(r + 6);
        for (int k = 0; k < 8; ++k) p.commands[std::size_t(k)] = std::uint8_t(v.u8(r + 8 + k));
    }
    t.panel.move = v.s16(0x1558); t.panel.turn = v.s16(0x155C); t.panel.program = v.s16(0x1560);
    const std::uint32_t record = v.u32(0x1564);
    t.panel.program_record = record >= 0x806 + 0x156A ? int((record - 0x806 - 0x156A) / 16) : -1;
    t.active = v.s16(0x1568);
    t.lift_moves = v.s16(0x1216);
    t.turn_flags = v.u8(0x1218);
    t.backwards_allowed = v.s16(0x110E) >= 0;
    t.clock = v.s16(0x11E4);
    t.klondikes = v.s16(0x11D8); t.destroyed = v.s16(0x11DA);
    t.klondikes_needed = v.s16(0xE30); t.destroy_needed = v.s16(0xE32);
    t.end_countdown = v.s16(0x11E0); t.end_reason = v.s16(0x11E2);
    t.blink = v.s16(0xFC0);
    t.frame = v.s16(0xFBE);
    t.footsteps = v.s16(0xA6) == 0;
    t.mine_holes = v.s16(0xE2A) >= 0;
    t.bomb_speed = v.s16(0xE2E);
    t.forcer_frames = v.s16(0x1212); t.forcer_direction = v.s16(0x1214);
    for (int k = 0; k < v.s16(0x1210); ++k) t.forcer_cells.push_back(v.s16(0x11E8 + 2 * k) + 100);
    return t;
}

// `objects`: the recording also covers the objects' own update ($BEE4,
// logicrec.py --objects), which then runs after the moves.
int logic_check(const babel::data::Assets& assets, const std::filesystem::path& file, bool objects) {
    using namespace babel::game;
    const Rules rules(assets.program);
    std::ifstream in(file);
    std::string before_hex, after_hex;
    int frames = 0, bad = 0;
    const auto bytes = [](const std::string& h) {
        std::vector<std::uint8_t> b(h.size() / 2);
        for (std::size_t i = 0; i < b.size(); ++i) b[i] = std::uint8_t(std::stoi(h.substr(2 * i, 2), nullptr, 16));
        return b;
    };
    while (in >> before_hex >> after_hex) {
        ++frames;
        TowerState native = state_from(Vars{bytes(before_hex)});
        rules.step(native);
        if (objects) rules.update(native);
        const TowerState original = state_from(Vars{bytes(after_hex)});
        std::vector<std::string> diffs;
        const auto check = [&](const std::string& what, long a, long b) {
            if (a != b) diffs.push_back(what + " original " + std::to_string(b) + " native " + std::to_string(a));
        };
        for (CellIndex i = 0; i < 500; ++i) {
            check("cell " + std::to_string(i), native.cell(i), original.cell(i));
            if (i >= 100) check("object at " + std::to_string(i), native.object_at(i), original.object_at(i));
        }
        for (int n = 0; n < TowerState::objects_max; ++n) {
            const auto& a = native.objects[std::size_t(n)];
            const auto& b = original.objects[std::size_t(n)];
            const std::string o = "slot " + std::to_string(n) + " ";
            check(o + "type", a.type, b.type); check(o + "x", a.x, b.x); check(o + "y", a.y, b.y); check(o + "z", a.z, b.z);
            check(o + "heading", a.heading, b.heading); check(o + "state", a.state, b.state);
            check(o + "progress", a.progress, b.progress); check(o + "cell", a.cell, b.cell); check(o + "target", a.target, b.target);
            check(o + "flags", a.flags, b.flags); check(o + "1a", a.counter_1a, b.counter_1a); check(o + "1c", a.counter_1c, b.counter_1c);
            if (objects) { check(o + "1e", a.counter_1e, b.counter_1e); check(o + "shape", long(a.shape), long(b.shape)); }
        }
        for (int n = 0; n < 7; ++n) {
            const auto& a = native.programs[std::size_t(n)];
            const auto& b = original.programs[std::size_t(n)];
            const std::string p = "program " + std::to_string(n) + " ";
            check(p + "command", a.command, b.command); check(p + "mode", a.mode, b.mode); check(p + "position", a.position, b.position);
        }
        check("panel move", native.panel.move, original.panel.move);
        check("panel turn", native.panel.turn, original.panel.turn);
        check("panel program", native.panel.program, original.panel.program);
        // $1564 is a redraw request the vertical-blank hook ($AB5A) consumes and
        // clears; it can do so between $BC7A and the recording's stop.
        if (original.panel.program_record != -1)
            check("panel record", native.panel.program_record, original.panel.program_record);
        check("lift moves", native.lift_moves, original.lift_moves);
        check("turn flags", native.turn_flags, original.turn_flags);
        check("forcer frames", native.forcer_frames, original.forcer_frames);
        check("forcer direction", native.forcer_direction, original.forcer_direction);
        check("forcer cells", long(native.forcer_cells.size()), long(original.forcer_cells.size()));
        for (std::size_t k = 0; k < native.forcer_cells.size() && k < original.forcer_cells.size(); ++k)
            check("forcer cell", native.forcer_cells[k], original.forcer_cells[k]);
        check("clock", native.clock, original.clock);
        check("klondikes", native.klondikes, original.klondikes);
        check("destroyed", native.destroyed, original.destroyed);
        check("end countdown", native.end_countdown, original.end_countdown);
        check("end reason", native.end_reason, original.end_reason);
        if (!diffs.empty()) {
            if (++bad <= 5) {
                std::printf("frame %d:", frames);
                for (std::size_t i = 0; i < diffs.size() && i < 6; ++i) std::printf(" [%s]", diffs[i].c_str());
                std::printf("\n");
            }
        }
    }
    std::printf("logic: %d of %d frames differ\n", bad, frames);
    return 0;
}

int view_check(const babel::data::Assets& assets, const Dump& d, int group, int index, const std::filesystem::path& out,
               const Dump* drawn = nullptr) {
    using namespace babel;
    if (group < 0) { // group -1: the tower whose name is in memory
        std::tie(group, index) = find_tower(assets, d);
        if (group < 0) return 1;
    }
    const auto& tower = assets.groups.at(std::size_t(group)).towers.at(std::size_t(index));
    const game::TowerMap map(tower, assets.program);
    int cell_diffs = 0;
    for (int f = -1; f < 4; ++f)
        for (int z = 0; z < 8; ++z)
            for (int x = 0; x < 8; ++x) {
                const int original = d.u8(0x806 + 0x1CBE + std::uint32_t(f * 100 + (z + 1) * 10 + x + 1));
                const int native = f < 0 ? (map.ground_lit(x, z) ? 4 : 0) : map.at(f, x, z);
                if (original != native) {
                    if (++cell_diffs <= 8) std::printf("cell f%d x%d z%d: original %02X native %02X\n", f, x, z, original, native);
                }
            }
    std::printf("cells: %d differ\n", cell_diffs);

    const int active = d.var(0x1568);
    const std::uint32_t spider = 0x806 + 0x15DA + std::uint32_t(active) * 32;
    const auto spider_cam = view::spider_camera(d.s16(spider + 6), d.s16(spider + 8), d.s16(spider + 10), d.s16(spider + 12),
                                         d.var(0xFBC), d.var(0xFB8), d.var(0xFBA));
    // The buffer was drawn with the camera in $FB0-$FB6 (one pose behind the
    // spider when the spider was just moved).
    const view::Camera cam{d.var(0xFB0), d.var(0xFB2), d.var(0xFB4), d.var(0xFB6) & 3};
    std::printf("camera %d %d %d heading %d (from the spider: %d %d %d %d)\n", cam.x, cam.y, cam.z, cam.heading,
                spider_cam.x, spider_cam.y, spider_cam.z, spider_cam.heading);

    const view::Window window{d.var(0xF9E), d.var(0xFA0), d.var(0xFA2), d.var(0xFA4)};
    view::Canvas canvas(320, 200);
    // Lift objects: the object map beside the cells ($1E4E, -1 = none) and
    // the object records' y ($15DA + 32 n + 8).
    const view::LiftHeight lift_height = [&](int f, int x, int z) -> std::optional<int> {
        const int n = std::int8_t(d.u8(0x806 + 0x1E4E + std::uint32_t(f * 100 + (z + 1) * 10 + x + 1)));
        if (n < 0) return std::nullopt;
        return d.s16(0x806 + 0x15DA + std::uint32_t(n) * 32 + 8);
    };
    // Objects: the object map gives each cell's object slot; the slot's
    // record its type, shape pointer, position and moving flag.
    std::map<std::uint32_t, data::Shape> shapes;
    std::vector<view::SceneObject> objects(64);
    for (int n = 0; n < 64; ++n) {
        const std::uint32_t r = 0x806 + 0x15DA + std::uint32_t(n) * 32;
        auto& o = objects[std::size_t(n)];
        o.slot = n;
        o.type = d.s16(r);
        o.x = d.s16(r + 6); o.y = d.s16(r + 8); o.z = d.s16(r + 10);
        o.moving = d.s16(r + 0x10) != 0;
        o.particle_count = d.s16(r + 0x1A);
        o.second_cloud = (d.u8(r + 0x12) & 2) != 0;
        const std::uint32_t shape = d.u32(r + 2);
        if (o.type < 0x1A && assets.main.contains(shape, 2)) {
            try {
                if (!shapes.count(shape)) shapes.emplace(shape, data::decode_shape(assets.main, shape));
                o.shape = &shapes.at(shape);
            } catch (const std::exception&) {
                std::printf("slot %d: no shape at $%X\n", n, shape);
            }
        }
    }
    const view::ObjectAt object_at = [&](int f, int x, int z) -> const view::SceneObject* {
        const int n = std::int8_t(d.u8(0x806 + 0x1E4E + std::uint32_t(f * 100 + (z + 1) * 10 + x + 1)));
        return n < 0 ? nullptr : &objects[std::size_t(n)];
    };
    const bool with_objects = std::getenv("TOB_NO_OBJECTS") == nullptr;
    // Pens as they are in the dump's memory (some object pens change at run time).
    const data::Memory memory(0, d.m);
    const auto pens = view::read_pens(memory);
    const bool full = std::getenv("TOB_FULL") != nullptr; // research: also sky, particles, overlays (framecheck)
    view::SkyState sky_state;
    auto sky = view::sky_settings(tower);
    if (std::getenv("TOB_FULL")) { // research: the settings as the reference has them (pokes)
        sky.stars = d.var(0xE34); sky.sky_type = d.var(0xE38); sky.moon_heading = d.var(0xE3A);
        sky.moon_type = d.var(0xE3C); sky.moon_x = d.var(0xE3E); sky.moon_y = d.var(0xE40);
        sky.moon_rotation = d.var(0xE42); sky.lightning = d.var(0xE26) >= 0; sky.meteors = d.var(0xE28) >= 0;
    }
    std::function<void(view::Canvas&)> draw_sky;
    if (full) {
        const auto v = [&](std::uint32_t o) { return d.var(o); };
        sky_state.moon_frame = v(0xE44);
        sky_state.lightning_wait = v(0xFC4); sky_state.thunder_wait = v(0xFCA); sky_state.lightning_heading = v(0xFC6);
        sky_state.meteor_wait = v(0xFCC); sky_state.meteor_heading = v(0xFCE);
        sky_state.meteor_x = v(0xFD0); sky_state.meteor_y = v(0xFD2); sky_state.meteor_frame = v(0xFD4);
        sky_state.random = std::uint16_t(v(0x890));
        view::SkyEffects::advance(sky_state, sky, cam.heading, v(0x20) == 0);
        draw_sky = [&](view::Canvas& c) { view::draw_sky(c, sky, sky_state, cam.heading, assets.program, assets.main, pens.object); };
    }
    const view::TowerView renderer = full ? view::TowerView(pens, assets.program, assets.main) : view::TowerView(pens);
    renderer.draw(canvas, window, 160, 72, map, cam, lift_height, with_objects ? object_at : view::ObjectAt{}, draw_sky);
    if (full) {
        view::OverlayState o;
        o.ready = d.var(0x11E6) < 0;
        o.blink = d.var(0xFC0); // counted down at the loop's start, before $D468
        const int info = d.var(0x1112), timer = d.var(0x1114);
        o.info = info == 0 ? view::Corner::off : info == 1 ? view::Corner::bottom : view::Corner::top;
        o.timer = timer == 0 ? view::Corner::off : timer == 2 ? view::Corner::top : view::Corner::bottom;
        o.sights = d.var(0x111C) >= 0;
        o.spider = d.var(0x1568) - 4;
        o.heading = cam.heading;
        o.klondikes = d.var(0x11D8); o.klondikes_needed = d.var(0xE30);
        o.destroyed = d.var(0x11DA); o.destroy_needed = d.var(0xE32);
        o.time_limit = d.var(0xE24) >= 0;
        o.time_left = d.var(0x121A) & 0xFFFF;
        // $D7EC: the overlays only for game types below 4 (not in demonstrations).
        if (d.var(0x11DE) < 4) view::draw_overlay(canvas, o, assets.program, assets.main);
        int clouds = 0;
        for (int n = 4; n < 64 && d.s16(0x806 + 0x15DA + std::uint32_t(n) * 32) >= 0; ++n)
            clouds += d.s16(0x806 + 0x15DA + std::uint32_t(n) * 32) == 0x1A;
        std::printf("particle clouds %d\n", clouds);
        std::printf("sky: moon %d meteor %s (%d, %d) bolt %s flash %d random %04X\n", sky_state.moon_frame,
                    sky_state.meteor_visible ? "shown" : "-", sky_state.meteor_x, sky_state.meteor_y,
                    sky_state.bolt_column ? "yes" : "-", sky_state.flash, sky_state.random);
    }

    const Dump& result = drawn != nullptr ? *drawn : d;
    const std::uint32_t buffer = result.u32(0x806 + 0x1A);
    platform::Image image(3 * 320, 200);
    int differ = 0;
    for (int y = window.top; y <= window.bottom; ++y) {
        for (int x = window.left; x <= window.right; ++x) {
            int original = 0;
            for (int p = 0; p < 4; ++p) original |= (result.u8(buffer + std::uint32_t(p * 0x1F40 + y * 40 + x / 8)) >> (7 - x % 8) & 1) << p;
            const int native = canvas.at(x, y);
            const auto colour = [&](int c) { return data::to_color(tower.colours[std::size_t(c)]); };
            image.put(x, y, colour(original));
            image.put(320 + x, y, colour(native));
            image.put(640 + x, y, original == native ? platform::Color{40, 40, 40} : platform::Color{255, 0, 0});
            differ += original != native;
        }
    }
    std::printf("view: %d of %d pixels differ\n", differ, (window.right - window.left + 1) * (window.bottom - window.top + 1));
    platform::save_png(image, out);
    return 0;
}

} // namespace

// Research: the panel poll $ADFC against panelrec.py's recording: the
// button under the recorded pointer (or a click in the view), the native
// poll from the recorded variables, then everything the poll may change.
int panel_check(const babel::data::Assets& assets, const std::filesystem::path& file) {
    using namespace babel::game;
    const Rules rules(assets.program);
    std::ifstream in(file);
    int x = 0, y = 0, b = 0, frames = 0, bad = 0;
    std::string before_hex, after_hex;
    const auto bytes = [](const std::string& h) {
        std::vector<std::uint8_t> v(h.size() / 2);
        for (std::size_t i = 0; i < v.size(); ++i) v[i] = std::uint8_t(std::stoi(h.substr(2 * i, 2), nullptr, 16));
        return v;
    };
    const auto button_at = [&](int px, int py) {
        for (int i = 0; i < buttons; ++i) {
            const std::uint32_t a = 0x31DE + std::uint32_t(i) * 8;
            if (px >= assets.program.u16(a) && px < assets.program.u16(a + 4) && py >= assets.program.u16(a + 2) &&
                py < assets.program.u16(a + 6))
                return i;
        }
        return -1;
    };
    while (in >> x >> y >> b >> before_hex >> after_hex) {
        ++frames;
        const Vars before{bytes(before_hex)}, after{bytes(after_hex)};
        TowerState native = state_from(before);
        Panel panel;
        panel.set_waits({before.s16(0x154C), before.s16(0x154E), before.s16(0x1550), before.s16(0x1552), before.s16(0x1554)});
        Panel::Input input;
        if (b & 2) {
            if (y >= 0x84) input.button = button_at(x, y);
            else input.view_click = true;
        }
        const Panel::Settings settings{before.s16(0x1110) == 0, before.s16(0xE22) < 0};
        panel.poll(native, rules, settings, input, -1);
        const TowerState original = state_from(after);
        std::vector<std::string> diffs;
        const auto check = [&](const std::string& what, long a, long o) {
            if (a != o) diffs.push_back(what + " original " + std::to_string(o) + " native " + std::to_string(a));
        };
        for (CellIndex i = 0; i < 500; ++i) {
            check("cell " + std::to_string(i), native.cell(i), original.cell(i));
            if (i >= 100) check("object at " + std::to_string(i), native.object_at(i), original.object_at(i));
        }
        for (int n = 0; n < TowerState::objects_max; ++n) {
            const auto& p = native.objects[std::size_t(n)];
            const auto& q = original.objects[std::size_t(n)];
            const std::string o = "slot " + std::to_string(n) + " ";
            check(o + "type", p.type, q.type); check(o + "x", p.x, q.x); check(o + "y", p.y, q.y); check(o + "z", p.z, q.z);
            check(o + "heading", p.heading, q.heading); check(o + "state", p.state, q.state);
            check(o + "progress", p.progress, q.progress); check(o + "cell", p.cell, q.cell); check(o + "target", p.target, q.target);
            check(o + "flags", p.flags, q.flags); check(o + "1c", p.counter_1c, q.counter_1c);
        }
        for (int n = 0; n < 7; ++n) {
            const auto& p = native.programs[std::size_t(n)];
            const auto& q = original.programs[std::size_t(n)];
            const std::string s = "program " + std::to_string(n) + " ";
            check(s + "command", p.command, q.command); check(s + "mode", p.mode, q.mode); check(s + "position", p.position, q.position);
            for (int k = 0; k < 8; ++k) check(s + "step " + std::to_string(k), p.commands[std::size_t(k)], q.commands[std::size_t(k)]);
        }
        check("active", native.active, original.active);
        check("panel move", native.panel.move, original.panel.move);
        check("panel turn", native.panel.turn, original.panel.turn);
        check("panel program", native.panel.program, original.panel.program);
        if (original.panel.program_record != -1) check("panel record", native.panel.program_record, original.panel.program_record);
        const auto w = panel.waits();
        check("select wait", w.select, after.s16(0x154C));
        check("turn wait", w.turn, after.s16(0x154E));
        check("move wait", w.move, after.s16(0x1550));
        check("program wait", w.program, after.s16(0x1552));
        check("selected", w.selected, after.s16(0x1554));
        check("started", native.started || before.s16(0x11E6) >= 0, after.s16(0x11E6) >= 0);
        check("pause", panel.pause_requested(), after.s16(0x14EA) >= 0);
        check("klondikes", native.klondikes, original.klondikes);
        check("destroyed", native.destroyed, original.destroyed);
        if (!diffs.empty() && ++bad <= 8) {
            std::printf("frame %d (%d,%d %d):", frames, x, y, b);
            for (std::size_t i = 0; i < diffs.size() && i < 6; ++i) std::printf(" [%s]", diffs[i].c_str());
            std::printf("\n");
        }
    }
    std::printf("panel: %d of %d frames differ\n", bad, frames);
    return 0;
}

// Research: runs the front end headless from a script of lines:
//   frames N | move X Y | click X Y [HOLD] | hold X Y N | key C (ret, bs, esc, space or a character) | shot FILE
int drive(const babel::data::Assets& assets, const std::filesystem::path& script, const std::filesystem::path& out) {
    babel::front::FrontEnd fe(assets);
    babel::front::FrontEnd::Input in;
    in.pointer = true;
    std::ifstream file(script);
    std::string line;
    long frame = 0;
    const auto step = [&](int n) {
        for (int i = 0; i < n; ++i, ++frame) {
            fe.vblank(in);
            in.key = 0;
        }
    };
    std::filesystem::create_directories(out);
    while (std::getline(file, line)) {
        std::istringstream words(line);
        std::string what;
        if (!(words >> what) || what[0] == '#') continue;
        if (what == "frames") { int n = 0; words >> n; step(n); }
        else if (what == "move") { words >> in.x >> in.y; step(1); }
        else if (what == "click" || what == "hold") {
            int hold = 3; words >> in.x >> in.y >> hold;
            in.left = true; step(hold); in.left = false; step(1);
        } else if (what == "key") {
            std::string k; words >> k;
            in.key = k == "ret" ? 13 : k == "bs" ? 8 : k == "esc" ? 27 : k == "space" ? ' ' : k[0];
            step(1);
        } else if (what == "shot") {
            std::string name; words >> name;
            platform::save_png(fe.screen().image(), out / name);
            std::printf("frame %ld: %s\n", frame, name.c_str());
            std::fflush(stdout);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    using namespace babel::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "panelcheck" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return panel_check(assets, platform::utf8_path(argv[3]));
        }
        if (command == "drive" && argc == 5) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            return drive(assets, platform::utf8_path(argv[3]), platform::utf8_path(argv[4]));
        }
        if (command == "import" && argc == 4) {
            const auto result = babel::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (const auto& file : result.files) platform::write_file(out / file.name, file.bytes);
            std::printf("%s: %zu files\n", result.release.c_str(), result.files.size());
            return 0;
        }
        if (command == "export" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            platform::save_png(title_image(assets.title), out / "title.png");
            int number = 0;
            for (const auto& group : assets.groups) {
                for (std::size_t i = 0; i < group.towers.size(); ++i, ++number) {
                    const auto& t = group.towers[i];
                    std::printf("%-14s %c  %-45s %-20s pw %-8s spiders %c%c%c  klondikes %d destroy %d time %s\n",
                                group.name.c_str(), char('A' + int(i % 9)), t.name.c_str(), t.author.c_str(),
                                t.password.c_str(), t.spider_available(0) ? 'Z' : '-', t.spider_available(1) ? 'P' : '-',
                                t.spider_available(2) ? 'G' : '-', t.klondikes, t.objects_to_destroy,
                                t.has_time_limit() ? std::to_string(t.time_limit).c_str() : "none");
                    char name[32];
                    std::snprintf(name, sizeof name, "tower%03d.png", number);
                    platform::save_png(tower_image(t, assets.program), out / name);
                    if (auto view = start_view_image(t, assets.program, assets.main)) {
                        std::snprintf(name, sizeof name, "tower%03d-view.png", number);
                        platform::save_png(*view, out / name);
                    }
                }
            }
            for (const auto& piece : assets.pieces) {
                const int rate = int(pal_clock / piece.period + 0.5);
                std::printf("%s: %zu bytes, period %d (%d Hz), volume %d\n", piece.name.c_str(), piece.samples.size(),
                            piece.period, rate, piece.volume);
                std::string file = piece.name;
                for (auto& c : file) if (c == ' ') c = '_';
                save_wav(out / (file + ".wav"), piece.samples, rate);
            }
            for (const auto& effect : assets.effects) {
                std::printf("effect $%04X: waveform $%05X, %zu bytes\n", effect.record, effect.waveform,
                            effect.samples.size());
            }
            return 0;
        }
        if (command == "models" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto& reference = assets.groups.at(1).towers.at(0);
            const auto kinds = babel::data::kind_shapes(assets.program);
            platform::Image sheet(6 * 160, int((kinds.size() + 5) / 6) * 140);
            int i = 0;
            for (const auto& k : kinds) {
                const int x = (i % 6) * 160, y = (i / 6) * 140;
                try {
                    const auto model = babel::data::shape_model(babel::data::decode_shape(assets.main, k.address),
                                                                assets.program, reference);
                    ui::draw_model(sheet, x, y, 160, 120, *model, 0.6, 0.35);
                } catch (const std::exception&) {}
                ui::text(sheet, std::to_string(k.kind) + " " + object_kind_name(assets.program, k.kind), x + 2, y + 124, 1, {220, 220, 220});
                ++i;
            }
            platform::save_png(sheet, platform::utf8_path(argv[3]));
            return 0;
        }
        if (command == "animcheck" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const babel::game::Animation animation(assets.program);
            std::ifstream in(platform::utf8_path(argv[3]));
            std::string line;
            std::vector<babel::game::AnimationState> before(60);
            std::vector<std::uint32_t> shown(60);
            std::map<int, std::pair<int, int>> per_type; // type -> (checked, differ)
            int frames = 0;
            while (std::getline(in, line)) {
                std::istringstream fields(line);
                int frame, running, phase, switch_state, flag, setting;
                std::string hex;
                fields >> frame >> running >> phase >> switch_state >> flag >> setting >> hex;
                const babel::game::AnimationClock clock{frame, running >= 0, phase, switch_state};
                const auto byte = [&](std::size_t i) { return std::stoi(hex.substr(2 * i, 2), nullptr, 16); };
                const auto word = [&](std::size_t i) { return int(std::int16_t(byte(i) << 8 | byte(i + 1))); };
                bool live = true; // $BEE4 stops at the first record with a negative type
                for (std::size_t n = 0; n < 60; ++n) {
                    const std::size_t r = n * 32;
                    if (word(r) < 0) live = false;
                    const babel::game::AnimationState now{word(r), word(r + 0xC), word(r + 0xE), word(r + 0x10),
                                                          word(r + 0x1A), word(r + 0x1C), word(r + 0x1E)};
                    const auto pointer = std::uint32_t(word(r + 2) & 0xFFFF) << 16 | std::uint32_t(word(r + 4) & 0xFFFF);
                    if (live && frames > 0 && now.type >= 0 && now.type < 0x1A && before[n].type == now.type) {
                        const auto native = animation.shape(now, before[n], clock, int(n));
                        auto& count = per_type[now.type];
                        ++count.first;
                        const auto expected = native ? *native : shown[n];
                        if (expected != pointer) {
                            if (++count.second <= 3)
                                std::printf("frame %d slot %zu type %d: original $%X native $%X (state %d progress %d)\n",
                                            frames, n + 4, now.type, pointer, expected, now.state, now.progress);
                        }
                    }
                    before[n] = now;
                    shown[n] = pointer;
                }
                ++frames;
            }
            for (const auto& [type, count] : per_type)
                std::printf("type %2d: %d of %d frames differ\n", type, count.second, count.first);
            return 0;
        }
        if (command == "framecheck" && argc == 8) { // research: before.bin after.bin (harness/framegrab.py)
            setenv("TOB_FULL", "1", 1);
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const Dump before{platform::read_file(platform::utf8_path(argv[3]))};
            const Dump after{platform::read_file(platform::utf8_path(argv[4]))};
            const int r = view_check(assets, before, std::atoi(argv[5]), std::atoi(argv[6]), platform::utf8_path(argv[7]), &after);
            // The copper: the frame's palette plan against the list the swap selects.
            int group = std::atoi(argv[5]), index = std::atoi(argv[6]);
            if (group < 0) std::tie(group, index) = find_tower(assets, before);
            if (group < 0) return 1;
            const auto& tower = assets.groups.at(std::size_t(group)).towers.at(std::size_t(index));
            const bool flash = after.var(0xFC8) != 0;
            const auto plan = babel::view::frame_palette(tower, assets.program, flash);
            const int list = flash ? 2 : 0; // lists 0 and 1 differ only in their bitplanes
            std::uint32_t at = 0x806 + 0x3356 + std::uint32_t(list) * 0x168;
            int line = -1, mismatches = 0, top = after.var(0x2);
            std::size_t next7 = 0;
            for (int guard = 0; guard < 200; ++guard, at += 4) {
                const int a = after.s16(at) & 0xFFFF, b = after.s16(at + 2) & 0xFFFF;
                if (a == 0xFFFF && b == 0xFFFE) break;
                if (a & 1) { line = (a >> 8) - top; continue; }
                if (a >= 0x180 && a < 0x1A0 && line < 0) {
                    const int reg = (a - 0x180) / 2;
                    if (plan.colours[std::size_t(reg)] != b) { ++mismatches; std::printf("colour %d: copper %03X native %03X\n", reg, b, plan.colours[std::size_t(reg)]); }
                } else if (a == 0x18E && line >= 0 && line < 0x82) {
                    if (next7 >= plan.colour7.size() || plan.colour7[next7].first != line || plan.colour7[next7].second != b) {
                        ++mismatches;
                        std::printf("line %d colour 7: copper %03X native %s\n", line, b,
                                    next7 < plan.colour7.size() ? (std::to_string(plan.colour7[next7].first) + " " + std::to_string(plan.colour7[next7].second)).c_str() : "-");
                    }
                    ++next7;
                }
            }
            std::printf("copper list %d: %d mismatches, %zu of %zu colour-7 changes seen\n", list, mismatches, next7, plan.colour7.size());
            return r;
        }
        if (command == "logiccheck" && argc == 4) {
            return logic_check(load_assets(platform::utf8_path(argv[2])), platform::utf8_path(argv[3]), false);
        }
        if (command == "objectcheck" && argc == 4) {
            return logic_check(load_assets(platform::utf8_path(argv[2])), platform::utf8_path(argv[3]), true);
        }
        if (command == "viewcheck" && argc == 7) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const Dump dump{platform::read_file(platform::utf8_path(argv[3]))};
            return view_check(assets, dump, std::atoi(argv[4]), std::atoi(argv[5]), platform::utf8_path(argv[6]));
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: tower_of_babel_assets import <image.adf> <content> | export <content> <out>\n");
    return 2;
}
