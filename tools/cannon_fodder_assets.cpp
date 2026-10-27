// Cannon Fodder research tool: imports the three disk images into a content
// folder and exports the decoded data (.analysis/cannon_fodder_amiga/
// formats.md).
//
//   cannon_fodder_assets import <disk1.adf> <disk2.adf> <disk3.adf> <content folder>
//   cannon_fodder_assets export <content folder> <output folder>
//
// The export writes every launcher preview as a PNG (maps at full size).

#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
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
    std::fprintf(stderr, "usage: cannon_fodder_assets import <disk1> <disk2> <disk3> <content>\n"
                         "       cannon_fodder_assets export <content> <output folder>\n");
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
