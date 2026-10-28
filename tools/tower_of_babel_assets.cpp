// Tower of Babel research tool: imports the disk image into a content folder
// and exports the decoded data (.analysis/tower_of_babel_amiga/formats.md).
//
//   tower_of_babel_assets import <image.adf> <content folder>
//   tower_of_babel_assets export <content folder> <output folder>
//   tower_of_babel_assets models <content folder> <out.png>   (contact sheet of the shapes)
//   tower_of_babel_assets animcheck <content folder> <animrec.txt>   (harness/animrec.py)
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

int main(int argc, char** argv) {
    using namespace babel::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
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
