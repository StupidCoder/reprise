// Stunt Car Racer research tool: imports the disk image into a content folder
// and exports the decoded data (games/stunt_car_racer_amiga/game.md).
//
//   stunt_car_racer_assets import <image.adf> <content folder>
//   stunt_car_racer_assets export <content folder> <output folder>
//       loading picture, one picture and one start race view per circuit,
//       and tracks.txt (every segment as decoded, for comparison with the
//       running original)
//   stunt_car_racer_assets view <content> <track> <segment> <x> <z> <eye>
//                               <yaw> <pitch> <roll> <out.png>
//       the race view from a camera (plan units, height units, angles in
//       65536ths of a turn), for comparison with the original's screen

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/preview.hpp"
#include "stunt_car_racer_amiga/import/disk_import.hpp"
#include "stunt_car_racer_amiga/render/race_view.hpp"
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
            const auto palette = stuntcar::render::race_palette(assets.program);
            std::ofstream text(std::filesystem::path(argv[3]) / "tracks.txt");
            for (std::size_t t = 0; t < circuits.tracks.size(); ++t) {
                const auto& track = circuits.tracks[t];
                const auto model = stuntcar::data::circuit_model(circuits, track);
                platform::save_png(stuntcar::data::circuit_image(*model),
                                   std::filesystem::path(argv[3]) / ("circuit_" + std::to_string(t) + ".png"));
                const auto view = stuntcar::render::draw_race_view(circuits, scenery, track, track.car_start_segment,
                                                                   stuntcar::render::start_camera(circuits, track));
                platform::save_png(stuntcar::render::to_image(view, palette),
                                   std::filesystem::path(argv[3]) / ("race_view_" + std::to_string(t) + ".png"));
                text << "track " << t << " " << track.name << " start_height " << track.start_height
                     << " car_start " << track.car_start_segment << " start_line " << track.start_line_segment
                     << " header_third " << int(track.header_third) << "\n";
                // cell, heading, reversed, shape, profiles, wide, kerb phase, edge bases
                for (const auto& s : track.segments) {
                    text << "  " << s.cell_x << " " << s.cell_z << " " << s.heading << " " << s.reversed << " "
                         << s.shape << " " << s.left_profile << " " << s.right_profile << " " << s.wide_profiles
                         << " " << s.kerb_phase << " " << s.left_base << " " << s.right_base << "\n";
                }
            }
            return 0;
        }
        if (command == "view" && argc == 12) {
            const auto assets = stuntcar::data::load_assets(argv[2]);
            const auto circuits = stuntcar::data::load_circuits(assets.program);
            const auto& track = circuits.tracks.at(std::size_t(std::stoi(argv[3])));
            stuntcar::render::Camera camera{std::stod(argv[5]), std::stod(argv[6]), std::stod(argv[7]),
                                            std::stoi(argv[8]), std::stoi(argv[9]), std::stoi(argv[10])};
            const auto scenery = stuntcar::data::load_scenery(assets.program);
            const auto view = stuntcar::render::draw_race_view(circuits, scenery, track, std::stoi(argv[4]), camera);
            platform::save_png(stuntcar::render::to_image(view, stuntcar::render::race_palette(assets.program)), argv[11]);
            return 0;
        }
        std::fprintf(stderr, "usage: stunt_car_racer_assets import <image.adf> <content> | export <content> <out> | view ...\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
