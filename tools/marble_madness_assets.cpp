// Marble Madness research tool: imports the disk image into a content folder
// and exports the decoded data as PNG pictures (.analysis/marble_madness_amiga).
//
//   marble_madness_assets import <image.adf> <content folder>
//   marble_madness_assets export <content folder> <output folder>

#include "marble_madness_amiga/data/assets.hpp"
#include "marble_madness_amiga/data/preview.hpp"
#include "marble_madness_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <exception>
#include <string>

namespace {

void print_library(const char* name, const marble_madness::data::ImageLibrary& library) {
    int sprites = 0;
    for (const auto& cell : library.cells) {
        sprites += cell.type == 1;
    }
    std::printf("%-14s %3zu cells (%d sprites, %zu scenery)\n", name, library.cells.size(), sprites,
                library.cells.size() - std::size_t(sprites));
}

} // namespace

int main(int argc, char** argv) {
    using namespace marble_madness::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = marble_madness::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
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
            platform::save_png(splash_image(assets), out / "splash.png");
            for (int c = 0; c < course_count; ++c) {
                const auto course = Course(c);
                const auto& map = assets.maps[std::size_t(c)];
                const std::string name = course_name(course);
                std::printf("%-12s %3d rows (%3d stored), %4zu tiles, palette", name.c_str(), map.rows,
                            map.stored_rows(), map.tiles.size());
                for (const auto colour : map.palette) {
                    std::printf(" %03X", colour);
                }
                std::printf("\n");
                platform::save_png(course_image(assets, course), out / (name + "-course.png"));
                platform::save_png(tile_sheet(assets, course), out / (name + "-tiles.png"));
                platform::save_png(library_sheet(assets.course_images[std::size_t(c)], map.palette),
                                   out / (name + "-objects.png"));
                platform::save_png(library_sheet(assets.wall_covers[std::size_t(c)], map.palette, 800),
                                   out / (name + "-covers.png"));
                print_library((name + " ilb").c_str(), assets.course_images[std::size_t(c)]);
                print_library((name + " obsc").c_str(), assets.wall_covers[std::size_t(c)]);
            }
            const auto& palette = assets.maps[0].palette;
            platform::save_png(library_sheet(assets.marble, palette), out / "marble.png");
            platform::save_png(library_sheet(assets.slinky, palette), out / "slinky.png");
            platform::save_png(library_sheet(assets.acid, palette), out / "acid.png");
            platform::save_png(library_sheet(assets.bird, palette), out / "bird.png");
            print_library("marbdat.vlb", assets.marble);
            print_library("slink.vlb", assets.slinky);
            print_library("ooze.vlb", assets.acid);
            print_library("birdink.vlb", assets.bird);
            return 0;
        }
        std::fprintf(stderr, "usage: marble_madness_assets import <adf> <content> | export <content> <out>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "marble_madness_assets: %s\n", error.what());
        return 1;
    }
}
