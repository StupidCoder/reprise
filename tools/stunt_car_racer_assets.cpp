// Stunt Car Racer research tool: imports the disk image into a content folder
// and exports the decoded data (games/stunt_car_racer_amiga/game.md).
//
//   stunt_car_racer_assets import <image.adf> <content folder>
//   stunt_car_racer_assets export <content folder> <output folder>
//       loading picture, one picture and one start race screen per circuit,
//       and tracks.txt (every segment as decoded, for comparison with the
//       running original)
//   stunt_car_racer_assets view <content> <track> <segment> <x> <z> <eye>
//                               <yaw> <pitch> <roll> <out.png>
//       the race view from a camera (plan units, height units, angles in
//       65536ths of a turn), for comparison with the original's screen
//   stunt_car_racer_assets drive <content> <frames> <inputs> <out_prefix> [every]
//       runs the game module (the practice race) for <frames> frames; <inputs>
//       is a comma list of hex joystick bytes per frame (bits 0 up, 1 down,
//       2 left, 3 right, 4 fire), the last repeated; saves every <every>th
//       picture as <out_prefix>NNNN.png
//   stunt_car_racer_assets screen <content> <state.txt> <out.png>
//       the whole race screen; state.txt holds "key value" lines: track,
//       segment, x, z, eye, yaw, pitch, roll, wheel_frame, right_wheel_y,
//       left_wheel_y, chain_bottom, boost, boost_phase, intact_marks, flag_lamp,
//       right_lamp, speed_bar, distance, lap, lap_minutes, lap_seconds,
//       lap_hundredths, lap_show_hundredths, message_start, message_groups,
//       message_white, message_box, boost_left, lower_minutes, lower_seconds,
//       lower_hundredths, opponent_segment, opponent_step, opponent_across,
//       opponent_lift_rl/_rr/_fl/_fr

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/preview.hpp"
#include "stunt_car_racer_amiga/game/module.hpp"
#include "stunt_car_racer_amiga/import/disk_import.hpp"
#include "stunt_car_racer_amiga/render/race_screen.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"
#include <map>
#include <optional>
#include <cstdio>
#include <fstream>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "import" && argc == 4) {
            const auto image = platform::read_file(argv[2]);
            const auto result = stuntcar::import::import_disk(image);
            std::filesystem::create_directories(argv[3]);
            for (const auto& file : result.files) platform::write_file(std::filesystem::path(argv[3]) / file.name, file.bytes);
            std::printf("%s: %zu files\n", result.release.c_str(), result.files.size());
            return 0;
        }
        if (command == "export" && argc == 4) {
            const auto assets = stuntcar::data::load_assets(argv[2]);
            std::filesystem::create_directories(argv[3]);
            platform::save_png(stuntcar::data::to_image(assets.loading_picture),
                               std::filesystem::path(argv[3]) / "loading_picture.png");
            const auto circuits = stuntcar::data::load_circuits(assets.program);
            const auto scenery = stuntcar::data::load_scenery(assets.program);
            const auto art = stuntcar::data::load_cockpit_art(assets.program);
            std::ofstream text(std::filesystem::path(argv[3]) / "tracks.txt");
            for (std::size_t t = 0; t < circuits.tracks.size(); ++t) {
                const auto& track = circuits.tracks[t];
                const auto model = stuntcar::data::circuit_model(circuits, track);
                platform::save_png(stuntcar::data::circuit_image(*model),
                                   std::filesystem::path(argv[3]) / ("circuit_" + std::to_string(t) + ".png"));
                platform::save_png(stuntcar::render::start_screen(assets.program, circuits, scenery, art, track),
                                   std::filesystem::path(argv[3]) / ("race_screen_" + std::to_string(t) + ".png"));
                text << "track " << t << " " << track.name << " start_height " << track.start_height
                     << " car_start " << track.car_start_segment << " start_line " << track.start_line_segment
                     << " halfway " << track.halfway_segment << "\n";
                // cell, heading, reversed, shape, profiles, wide, kerb phase, edge bases
                for (const auto& s : track.segments) {
                    text << "  " << s.cell_x << " " << s.cell_z << " " << s.heading << " " << s.reversed << " "
                         << s.shape << " " << s.left_profile << " " << s.right_profile << " " << s.wide_profiles
                         << " " << s.kerb_phase << " " << s.left_base << " " << s.right_base << "\n";
                }
            }
            return 0;
        }
        if (command == "view" && (argc == 12 || argc == 13)) {
            const auto assets = stuntcar::data::load_assets(argv[2]);
            const auto circuits = stuntcar::data::load_circuits(assets.program);
            const auto& track = circuits.tracks.at(std::size_t(std::stoi(argv[3])));
            stuntcar::render::Camera camera{std::stod(argv[5]), std::stod(argv[6]), std::stod(argv[7]),
                                            std::stoi(argv[8]), std::stoi(argv[9]), std::stoi(argv[10])};
            const auto scenery = stuntcar::data::load_scenery(assets.program);
            // Optional: the car beside the road, "left" or "right".
            const std::string side = argc == 13 ? argv[12] : "";
            const auto beside = side == "left"    ? stuntcar::render::Beside::left
                                : side == "right" ? stuntcar::render::Beside::right
                                                  : stuntcar::render::Beside::no;
            const auto view =
                stuntcar::render::draw_race_view(circuits, scenery, track, std::stoi(argv[4]), camera, {}, beside);
            platform::save_png(stuntcar::render::to_image(view, stuntcar::render::race_palette(assets.program)), argv[11]);
            return 0;
        }
        if (command == "drive" && argc >= 6) {
            auto game = stuntcar::game::start(argv[2]);
            const int frames = std::stoi(argv[3]), every = argc > 6 ? std::stoi(argv[6]) : 10;
            if (argc > 7 && std::string(argv[7]) == "league") game->set_option(1, 1); // against Jumpin' Jack
            std::vector<int> inputs;
            for (std::string list = argv[4]; !list.empty();) {
                const auto comma = list.find(',');
                const auto item = list.substr(0, comma);
                const auto star = item.find('*');
                const int repeat = star == std::string::npos ? 1 : std::stoi(item.substr(star + 1));
                for (int r = 0; r < repeat; ++r) inputs.push_back(std::stoi(item.substr(0, star), nullptr, 16));
                list = comma == std::string::npos ? "" : list.substr(comma + 1);
            }
            for (int k = 0; k < frames; ++k) {
                const int in = inputs.empty() ? 0 : inputs[std::size_t(std::min<int>(k, int(inputs.size()) - 1))];
                app::GameInput input;
                input.up = in & 1, input.down = in & 2, input.left = in & 4, input.right = in & 8, input.fire = in & 16;
                game->tick(input);
                if (k % every == 0) {
                    char name[16];
                    std::snprintf(name, sizeof name, "%04d.png", k);
                    platform::save_png(game->picture(), std::string(argv[5]) + name);
                }
            }
            return 0;
        }
        if (command == "screen" && argc == 5) {
            const auto assets = stuntcar::data::load_assets(argv[2]);
            const auto circuits = stuntcar::data::load_circuits(assets.program);
            std::map<std::string, double> state;
            std::ifstream in(argv[3]);
            for (std::string key; in >> key;) in >> state[key];
            const auto get = [&](const char* key, double fallback) {
                const auto it = state.find(key);
                return it == state.end() ? fallback : it->second;
            };
            const auto& track = circuits.tracks.at(std::size_t(get("track", 0)));
            stuntcar::render::Camera camera{get("x", 0), get("z", 0), get("eye", 0), int(get("yaw", 0)),
                                            int(get("pitch", 0)), int(get("roll", 0))};
            std::optional<stuntcar::render::Opponent> opponent;
            if (state.count("opponent_segment"))
                opponent = stuntcar::render::Opponent{
                    int(get("opponent_segment", 0)), get("opponent_step", 0), get("opponent_across", 128),
                    {get("opponent_lift_rl", 0x50), get("opponent_lift_rr", 0x50), get("opponent_lift_fl", 0x50),
                     get("opponent_lift_fr", 0x50)}};
            const auto view = stuntcar::render::draw_race_view(circuits, stuntcar::data::load_scenery(assets.program),
                                                               track, int(get("segment", 0)), camera, opponent);
            stuntcar::render::Cockpit cockpit;
            cockpit.wheel_frame = int(get("wheel_frame", 1));
            cockpit.right_wheel_y = int(get("right_wheel_y", 122));
            cockpit.left_wheel_y = int(get("left_wheel_y", 122));
            if (state.count("chain_bottom")) cockpit.chain_bottom = int(get("chain_bottom", 0));
            cockpit.boost = get("boost", 0) != 0;
            cockpit.boost_phase = int(get("boost_phase", 0));
            cockpit.intact_marks = int(get("intact_marks", 10));
            cockpit.flag_lamp = get("flag_lamp", 0) != 0;
            cockpit.speed_bar = int(get("speed_bar", 0));
            cockpit.distance = int(get("distance", 0));
            cockpit.right_lamp = get("right_lamp", 0) != 0;
            cockpit.lap = int(get("lap", 0));
            if (state.count("message_start"))
                cockpit.message = stuntcar::data::decode_message(assets.program, int(get("message_start", 0)),
                                                                 int(get("message_groups", 1)));
            cockpit.message_white = get("message_white", 0) != 0;
            cockpit.message_box = get("message_box", 0) != 0;
            if (state.count("boost_left")) cockpit.boost_left = int(get("boost_left", 0));
            if (state.count("lower_minutes"))
                cockpit.lower_time = stuntcar::render::Cockpit::Time{int(get("lower_minutes", 0)), int(get("lower_seconds", 0)),
                                                                     int(get("lower_hundredths", 0)), true};
            if (state.count("lap_minutes"))
                cockpit.lap_time = stuntcar::render::Cockpit::Time{int(get("lap_minutes", 0)), int(get("lap_seconds", 0)),
                                                                   int(get("lap_hundredths", 0)), get("lap_show_hundredths", 0) != 0};
            platform::save_png(stuntcar::render::draw_race_screen(stuntcar::data::load_cockpit_art(assets.program), view,
                                                                  stuntcar::render::race_palette(assets.program), cockpit),
                               argv[4]);
            return 0;
        }
        std::fprintf(stderr, "usage: stunt_car_racer_assets import <image.adf> <content> | export <content> <out> | view ... | screen <content> <state.txt> <out.png>\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
