// Cannon Fodder research tool: imports the three disk images into a content
// folder and exports the decoded data (.analysis/cannon_fodder_amiga/
// formats.md).
//
//   cannon_fodder_assets import <disk1.adf> <disk2.adf> <disk3.adf> <content folder>
//   cannon_fodder_assets export <content folder> <output folder>
//   cannon_fodder_assets view <content folder> <map> <camera x> <camera y> <out.png> [indices.raw]
//   cannon_fodder_assets lookahead <content folder> <leader x> <leader y> <pointer map x> <pointer map y>
//   cannon_fodder_assets camreplay <content folder> <trace.txt> [--holds]
//   cannon_fodder_assets spawn <content folder> <map> [table.bin]
//   cannon_fodder_assets objects <content folder> <map> <grab prefix> [diff.png]
//   cannon_fodder_assets panel <content folder> <out.png> [sprites.bin]
//
// panel composes the panel for the start of mission 1 (one squad, recruits
// 0 and 1 at rank 0, no ammunition) and, given the original's sprite memory
// from $394 (harness), compares all 48 x 224 pixels.
//
// objects draws the map and the objects of a grabbed object table
// (harness/objgrab.py: prefix.objects, prefix.buffer, prefix.txt) natively
// and compares the visible play area with the buffer the original drew.
//
// spawn prints the native mission set-up (game/objects.hpp) and, given a
// dump of the original's object table right after its spawner
// (harness/atspawn.py), compares every field the spawner writes.
//
// camreplay runs the native camera step from a recording of the original
// (.analysis/cannon_fodder_amiga/harness/camtrace.py, converted by
// tools/camtrace2txt.py): the first frame's state, then each frame's inputs
// (follow flags and steps, target point, hold counters, tick and map size);
// it compares position, velocity and chase speed with the next frame. With
// --holds the reversal holds come from the native tile drawer tick instead
// (snapshot at the start of each second tick).
//
// The export writes every launcher preview as a PNG (maps at full size). The
// view is the play view's map background (game/map_view.hpp) as a PNG and,
// optionally, as raw colour indices for comparison with the original's
// display (harness/display.py).

#include "amiga/adf.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/game/object_view.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include "cannon_fodder_amiga/game/panel.hpp"
#include "cannon_fodder_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <exception>
#include <string>

namespace {

std::string file_name(std::string title) {
    for (auto& c : title) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') c = '_';
    }
    return title;
}

int run(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "import" && argc == 6) {
        const std::filesystem::path out = argv[5];
        for (int i = 2; i < 5; ++i) {
            const auto result = fodder::import::import_disk(platform::read_file(argv[i]));
            std::printf("%s: disk %d of %d, %s, %zu files\n", argv[i], result.part, result.parts,
                        result.release.c_str(), result.files.size());
            for (const auto& file : result.files) {
                std::filesystem::create_directories((out / file.name).parent_path());
                platform::write_file(out / file.name, file.bytes);
            }
        }
        return 0;
    }
    if (command == "export" && argc == 4) {
        const std::filesystem::path out = argv[3];
        std::filesystem::create_directories(out);
        int n = 0;
        for (const auto& preview : fodder::data::previews(argv[2], true)) {
            char prefix[8];
            std::snprintf(prefix, sizeof prefix, "%03d-", n++);
            platform::save_png(preview.image, out / (prefix + file_name(preview.title) + ".png"));
            std::printf("%s%s (%dx%d)\n", prefix, preview.title.c_str(), preview.image.width, preview.image.height);
        }
        return 0;
    }
    if (command == "view" && (argc == 7 || argc == 8)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const int disk = data::map_disk(number);
        const auto map = data::read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
        const game::MapPicture picture(map, data::read_tile_set(content.read(map.base_set, disk)),
                                       data::read_tile_set(content.read(map.sub_set, disk)));
        auto palette_name = map.base_set;
        palette_name.replace(palette_name.size() - 3, 3, "pal");
        const game::Camera camera{std::stoi(argv[4]), std::stoi(argv[5])};
        platform::save_png(game::view_image(picture, camera, data::read_palette(content.read(palette_name, disk))),
                           argv[6]);
        if (argc == 8) {
            platform::write_file(argv[7], game::view_indices(picture, camera));
        }
        return 0;
    }
    if (command == "lookahead" && argc == 7) {
        using namespace fodder;
        const game::GeometryTables tables(data::Content(argv[2]).read("fodderc", 1));
        const game::Point leader{std::stoi(argv[3]), std::stoi(argv[4])};
        const game::Point pointer{std::stoi(argv[5]), std::stoi(argv[6])};
        const auto target = game::look_ahead_target(tables, leader, pointer, false);
        const int dx = pointer.x - leader.x - 24, dy = pointer.y - leader.y;
        std::printf("distance %d direction %d target ", tables.distance(dx, dy), tables.direction(dx, dy, 9).value_or(-1));
        if (target) std::printf("%d %d\n", target->x, target->y); else std::printf("none\n");
        return 0;
    }
    if (command == "panel" && (argc == 4 || argc == 5)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const game::PanelArt art(content.read("fodderc", 1), amiga::read_ilbm(content.read("pstuff.lbm", 1)));
        game::PanelState state;
        state.selected_squad = 0;
        state.soldiers = {2, 0, 0};
        state.icon = {1, 0, 0};
        state.roster = {{0, 0, 0, 0, true}, {1, 0, 0, 0, true}};
        const auto panel = game::compose_panel(art, state);
        const auto palette = data::read_palette(content.read("junbase.pal", 2)); // placeholder colours for the PNG
        platform::Image image(game::panel_width, game::panel_height);
        for (std::size_t i = 0; i < panel.size(); ++i) {
            image.pixels[i] = panel[i] ? data::to_color(palette[panel[i]]) : platform::Color{34, 34, 34, 255};
        }
        platform::save_png(image, argv[3]);
        if (argc == 5) {
            const auto dump = platform::read_file(argv[4]); // sprite memory from $394
            const auto word = [&](std::uint32_t address) { return amiga::read_be16(dump, address - 0x394); };
            const std::uint32_t even[3] = {0x398, 0xb30, 0x12c8}, odd[3] = {0x764, 0xefc, 0x1694};
            int bad = 0;
            for (int y = 0; y < game::panel_height; ++y) {
                for (int x = 0; x < game::panel_width; ++x) {
                    const int c = x / 16, bit = 0x8000 >> (x & 15);
                    const auto at = std::uint32_t(y * 4);
                    const int colour = ((word(even[c] + at) & bit) ? 1 : 0) | ((word(even[c] + at + 2) & bit) ? 2 : 0) |
                                       ((word(odd[c] + at) & bit) ? 4 : 0) | ((word(odd[c] + at + 2) & bit) ? 8 : 0);
                    if (colour != panel[std::size_t(y * game::panel_width + x)]) {
                        if (++bad <= 10) std::printf("  (%d,%d) native %d original %d\n", x, y, panel[std::size_t(y * game::panel_width + x)], colour);
                    }
                }
            }
            std::printf("panel pixels %d, differing %d\n", game::panel_width * game::panel_height, bad);
            return bad == 0 ? 0 : 1;
        }
        return 0;
    }
    if (command == "objects" && (argc == 5 || argc == 6)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const int disk = data::map_disk(number);
        const std::string prefix = argv[4];
        const auto map = data::read_map(content.read("mapm" + std::to_string(number) + ".map", disk));
        const game::MapPicture picture(map, data::read_tile_set(content.read(map.base_set, disk)),
                                       data::read_tile_set(content.read(map.sub_set, disk)));
        const auto land = map.base_set.substr(0, 3);
        const game::ObjectSheets sheets{amiga::read_ilbm(content.read(land + "army.lbm", disk)),
                                        amiga::read_ilbm(content.read(land + "copt.lbm", disk))};
        const game::FrameTables frames(content.read("fodderc", 1));
        // The grabbed table, read into native objects (research input).
        const auto table = platform::read_file(prefix + ".objects");
        game::MissionObjects objects;
        for (int i = 0; i < game::object_slots; ++i) {
            const auto r = std::span<const std::uint8_t>(table).subspan(std::size_t(i) * 0x76, 0x76);
            auto& o = objects.slots[std::size_t(i)];
            const auto first = amiga::read_be16(r, 0);
            if (first == 0x8000 || first == 0xffff) continue;
            o.used = true;
            o.x = std::int32_t(amiga::read_be32(r, 0)); o.y = std::int32_t(amiga::read_be32(r, 4));
            o.graphic = std::int16_t(amiga::read_be16(r, 8)); o.frame = std::int16_t(amiga::read_be16(r, 0xa));
            o.kind = std::int16_t(amiga::read_be16(r, 0x18)); o.removed = amiga::read_be16(r, 0x24) != 0;
            o.height = std::int16_t(amiga::read_be16(r, 0x1e)); o.layer = std::int16_t(amiga::read_be16(r, 0x2c));
            o.hidden_lines = std::int16_t(amiga::read_be16(r, 0x52));
        }
        int camx, camy, snapx, snapy, dirx, diry;
        {
            std::ifstream in(prefix + ".txt");
            in >> camx >> camy >> snapx >> snapy >> dirx >> diry;
        }
        auto view = game::view_indices(picture, {camx, camy});
        const auto order = game::draw_list(objects, {snapx, snapy}, dirx, diry);
        game::draw_objects(view, {camx, camy}, objects, order, frames, sheets);
        const auto buffer = platform::read_file(prefix + ".buffer");
        const auto ring = [&](int x, int y) {
            const int index = ((y * 320 + x) % 81920 + 81920) % 81920;
            const int line = index / 320, column = index % 320;
            int colour = 0;
            for (int p = 0; p < 4; ++p) {
                if (buffer[std::size_t(p * 0x2828 + line * 40 + column / 8)] & (0x80 >> (column & 7))) colour |= 1 << p;
            }
            return colour;
        };
        platform::Image diff(game::view_width, game::view_height);
        int bad = 0, total = 0;
        for (int line = 0; line < game::view_height; ++line) {
            for (int column = 48; column < game::view_width; ++column) {
                const int x = camx + column - game::view_column_of_camera, y = camy + line;
                if (x >= picture.width() || y >= picture.height()) continue;
                ++total;
                const bool same = ring(x, y) == view[std::size_t(line * game::view_width + column)];
                bad += !same;
                diff.put(column, line, same ? platform::Color{30, 90, 30, 255} : platform::Color{255, 60, 60, 255});
            }
        }
        std::printf("draw list:");
        for (const int i : order) std::printf(" %d", i);
        std::printf("\nplay area pixels %d, differing %d\n", total, bad);
        if (argc == 6) platform::save_png(diff, argv[5]);
        return bad == 0 ? 0 : 1;
    }
    if (command == "spawn" && (argc == 4 || argc == 5)) {
        using namespace fodder;
        const data::Content content(argv[2]);
        const int number = std::stoi(argv[3]);
        const auto mission = game::spawn_objects(
            data::read_placements(content.read("mapm" + std::to_string(number) + ".spt", data::map_disk(number))));
        std::printf("enemies %d, kinds $6A/$48 %d\n", mission.enemies, mission.kind_6a_48);
        std::vector<std::uint8_t> dump;
        if (argc == 5) dump = platform::read_file(argv[4]);
        int bad = 0;
        for (int i = 0; i < game::object_slots; ++i) {
            const auto& o = mission.slots[std::size_t(i)];
            if (o.used) {
                std::printf("slot %2d kind %3d at %4d,%4d graphic $%02X squad %d delay %3d side %d\n", i, o.kind,
                            o.x >> 16, o.y >> 16, o.graphic, o.squad, o.start_delay, o.side);
            }
            if (dump.empty()) continue;
            const auto r = std::span<const std::uint8_t>(dump).subspan(std::size_t(i) * 0x76, 0x76);
            const auto w = [&](std::size_t at) { return int(std::int16_t(amiga::read_be16(r, at))); };
            const bool free_slot = amiga::read_be16(r, 0) == 0x8000;
            std::vector<std::pair<const char*, std::pair<int, int>>> fields;
            if (free_slot != !o.used) fields.push_back({"used", {o.used, !free_slot}});
            if (o.used && !free_slot) {
                fields = {{"x", {o.x >> 16, w(0)}}, {"x.frac", {o.x & 0xffff, w(2) & 0xffff}},
                          {"y", {o.y >> 16, w(4)}}, {"graphic", {o.graphic, w(8)}}, {"kind", {o.kind, w(0x18)}},
                          {"side", {o.side, w(0x22)}}, {"home_x", {o.home_x, w(0x26)}}, {"home_y", {o.home_y, w(0x28)}},
                          {"squad", {o.squad, w(0x32)}}, {"delay", {o.start_delay, w(0x4a)}}};
            }
            for (const auto& [name, values] : fields) {
                if (values.first != values.second) {
                    ++bad;
                    std::printf("  slot %d %s: native %d original %d\n", i, name, values.first, values.second);
                }
            }
        }
        if (!dump.empty()) std::printf("%d fields differ\n", bad);
        return bad == 0 ? 0 : 1;
    }
    if (command == "camreplay" && (argc == 4 || argc == 5)) {
        const bool native_holds = argc == 5 && std::string(argv[4]) == "--holds";
        using namespace fodder;
        const game::GeometryTables tables(data::Content(argv[2]).read("fodderc", 1));
        // frame camx camy vx vy ax ay fx fy speed maxspeed tx ty holdx holdy signx signy ticking vbl mapw maph fits
        // tick parity dirx diry drbx drby
        struct Row { long v[28]; };
        std::vector<Row> rows;
        std::ifstream in(argv[3]);
        for (std::string line; std::getline(in, line);) {
            std::istringstream fields(line);
            Row row{};
            for (auto& value : row.v) fields >> value;
            rows.push_back(row);
        }
        if (rows.empty()) throw std::runtime_error("empty trace");
        const auto& first = rows.front().v;
        game::CameraMotion motion;
        motion.x = std::int32_t(first[1]); motion.y = std::int32_t(first[2]);
        motion.velocity_x = std::int32_t(first[3]); motion.velocity_y = std::int32_t(first[4]);
        motion.speed = std::int32_t(first[9]);
        motion.sign_x = int(first[15]); motion.sign_y = int(first[16]);
        motion.hold_x = int(first[13]); motion.hold_y = int(first[14]);
        motion.drawer_direction_x = int(first[24]); motion.drawer_direction_y = int(first[25]);
        motion.drawer_ring_x = int(first[26]); motion.drawer_ring_line = int(first[27]);
        int bad = 0;
        for (std::size_t k = 0; k + 1 < rows.size(); ++k) {
            const auto& r = rows[k].v;
            const auto& next = rows[k + 1].v;
            motion.follow_x_axis = {r[7] != 0, int(r[5] >> 16)};
            motion.follow_y_axis = {r[8] != 0, int(r[6] >> 16)};
            motion.max_speed = int(r[10]);
            if (!native_holds) { motion.hold_x = int(r[13]); motion.hold_y = int(r[14]); }
            game::CameraFrame frame{int(r[19]), int(r[20]), r[21] != 0, r[17] != 0 && r[18] >= 3,
                                    {int(r[11]), int(r[12])}};
            game::step_camera(motion, tables, frame);
            // A tick starts after this frame when the next recording shows a
            // new tick number; on even ticks (parity 0) the drawer runs.
            if (native_holds && next[22] != r[22] && next[23] == 0) {
                game::tile_drawer_tick(motion);
            }
            if (motion.x != next[1] || motion.y != next[2] || motion.velocity_x != next[3] ||
                motion.velocity_y != next[4] || motion.speed != next[9]) {
                if (++bad <= 5) {
                    std::printf("frame %ld: native %d,%d v %d,%d speed %d; original %ld,%ld v %ld,%ld speed %ld\n", next[0],
                                motion.x, motion.y, motion.velocity_x, motion.velocity_y, motion.speed, next[1], next[2],
                                next[3], next[4], next[9]);
                }
                motion.x = std::int32_t(next[1]); motion.y = std::int32_t(next[2]); // resynchronise
                motion.velocity_x = std::int32_t(next[3]); motion.velocity_y = std::int32_t(next[4]);
                motion.speed = std::int32_t(next[9]);
            }
        }
        std::printf("%zu frames, %d differ\n", rows.size() - 1, bad);
        return bad == 0 ? 0 : 1;
    }
    std::fprintf(stderr, "usage: cannon_fodder_assets import <disk1> <disk2> <disk3> <content>\n"
                         "       cannon_fodder_assets export <content> <output folder>\n"
                         "       cannon_fodder_assets view <content> <map> <x> <y> <out.png> [out.raw]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
