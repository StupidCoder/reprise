// Rock 'n Roll research tool: imports the disk image into a content folder
// and exports the decoded data (.analysis/rock_n_roll_amiga/formats.md).
//
//   rock_n_roll_assets import <image.adf> <content folder>
//   rock_n_roll_assets export <content folder> <output folder>
//
// The export writes PNG pictures (title, ending, every level at the game's
// scale, each world's graphics bank and palette) and a summary on stdout
// (worlds, levels, music files).

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "rock_n_roll_amiga/data/assets.hpp"
#include "rock_n_roll_amiga/data/preview.hpp"
#include "rock_n_roll_amiga/import/disk_import.hpp"
#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    using namespace rocknroll::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = rocknroll::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (const auto& file : result.files) {
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
                platform::save_png(picture_image(picture.picture), out / (picture.name + ".png"));
            }
            for (const auto& world : assets.worlds) {
                const auto n = std::to_string(world.number);
                std::printf("world %d: graphics %02X extra %02X songs %02X samples %02X\n", world.number,
                            world.files.graphics, world.files.extra, world.files.songs, world.files.samples);
                platform::save_png(bank_image(world.bank, world.tiles.palette), out / ("world" + n + "-bank.png"));
                platform::save_png(palette_image(world.tiles.palette), out / ("world" + n + "-palette.png"));
            }
            for (const auto& level : assets.levels) {
                char name[32];
                std::snprintf(name, sizeof name, "level%02d.png", level.number);
                std::printf("level %2d %-14s file %02X world %d  %3d columns x %3d rows\n", level.number,
                            level.name.c_str(), level.file, level.world, level.columns, level.rows);
                platform::save_png(level_image(level, assets.world(level.world)), out / name);
            }
            for (const auto& music : assets.music) {
                std::printf("music file %02X: %s, %zu bytes\n", music.file, music.song_data ? "TFMX song data" : "samples",
                            music.size);
            }
            return 0;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: rock_n_roll_assets import <image.adf> <content> | export <content> <out>\n");
    return 2;
}
