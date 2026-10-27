// Cannon Fodder research tool: imports the three disk images into a content
// folder and exports the decoded data (.analysis/cannon_fodder_amiga/
// formats.md).
//
//   cannon_fodder_assets import <disk1.adf> <disk2.adf> <disk3.adf> <content folder>
//   cannon_fodder_assets export <content folder> <output folder>
//   cannon_fodder_assets view <content folder> <map> <camera x> <camera y> <out.png> [indices.raw]
//   cannon_fodder_assets lookahead <content folder> <leader x> <leader y> <pointer map x> <pointer map y>
//   cannon_fodder_assets camreplay <content folder> <trace.txt> [--holds]
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

#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/game/camera.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
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
