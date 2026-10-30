// Stunt Car Racer research tool: imports the disk image into a content folder
// and exports the decoded data (games/stunt_car_racer_amiga/game.md).
//
//   stunt_car_racer_assets import <image.adf> <content folder>
//   stunt_car_racer_assets export <content folder> <output folder>
//       loading picture, one picture per circuit, and tracks.txt (every
//       segment as decoded, for comparison with the running original)

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/preview.hpp"
#include "stunt_car_racer_amiga/import/disk_import.hpp"
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
            std::ofstream text(std::filesystem::path(argv[3]) / "tracks.txt");
            for (std::size_t t = 0; t < circuits.tracks.size(); ++t) {
                const auto& track = circuits.tracks[t];
                const auto model = stuntcar::data::circuit_model(circuits, track);
                platform::save_png(stuntcar::data::circuit_image(*model),
                                   std::filesystem::path(argv[3]) / ("circuit_" + std::to_string(t) + ".png"));
                text << "track " << t << " " << track.name << " start_height " << track.start_height << "\n";
                // cell, heading, reversed, shape, profiles, wide, kerb phase, edge bases
                for (const auto& s : track.segments) {
                    text << "  " << s.cell_x << " " << s.cell_z << " " << s.heading << " " << s.reversed << " "
                         << s.shape << " " << s.left_profile << " " << s.right_profile << " " << s.wide_profiles
                         << " " << s.kerb_phase << " " << s.left_base << " " << s.right_base << "\n";
                }
            }
            return 0;
        }
        std::fprintf(stderr, "usage: stunt_car_racer_assets import <image.adf> <content> | export <content> <out>\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
