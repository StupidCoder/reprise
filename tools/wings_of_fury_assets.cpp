// Wings of Fury research tool: imports the disk image into a content folder
// and exports the decoded data (.analysis/wings_of_fury_amiga/formats.md).
//
//   wings_of_fury_assets import <image.adf> <content folder>
//   wings_of_fury_assets export <content folder> <output folder>
//
// The export writes PNG pictures, the samples as 8-bit WAV files (at 8,000 Hz:
// the playback periods are not read yet), each library's shape headers as a
// text file, and a summary on stdout (palettes, maps, texts, high scores).

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/data/preview.hpp"
#include "wings_of_fury_amiga/import/disk_import.hpp"
#include <cstdio>
#include <exception>
#include <string>

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(std::uint8_t(v));
    out.push_back(std::uint8_t(v >> 8));
}

std::vector<std::uint8_t> wav(const wings::data::Sample& sample, std::uint32_t rate) {
    std::vector<std::uint8_t> out = {'R', 'I', 'F', 'F'};
    put32(out, std::uint32_t(36 + sample.data.size()));
    for (char c : std::string("WAVEfmt ")) out.push_back(std::uint8_t(c));
    put32(out, 16);
    put16(out, 1);
    put16(out, 1);
    put32(out, rate);
    put32(out, rate);
    put16(out, 1);
    put16(out, 8);
    for (char c : std::string("data")) out.push_back(std::uint8_t(c));
    put32(out, std::uint32_t(sample.data.size()));
    for (const auto v : sample.data) out.push_back(std::uint8_t(v + 128)); // WAV 8-bit is unsigned
    return out;
}

std::string shape_table(const wings::data::ShapeLibrary& library) {
    std::string text = "name  w   h  off_x off_y sheet_x sheet_y clear set planes\n";
    char line[160];
    for (const auto& shape : library.shapes) {
        std::snprintf(line, sizeof line, "%-4s %3d %3d %6d %5d %7d %7d   %02X  %02X ", shape.name.c_str(), shape.width(),
                      shape.height, shape.offset_x, shape.offset_y, shape.sheet_x, shape.sheet_y,
                      shape.cleared_planes, shape.set_planes);
        text += line;
        for (const auto& plane : shape.planes) {
            std::snprintf(line, sizeof line, " %02X", plane.bitplanes);
            text += line;
        }
        text += '\n';
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    using namespace wings::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = wings::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
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
                std::printf("picture %-12s %3d x %3d, %d planes, %zu colours\n", picture.name.c_str(),
                            picture.picture.width, picture.picture.height, picture.picture.planes,
                            picture.picture.palette.size());
            }
            for (const auto& library : assets.libraries) {
                const auto& palette = find_palette(assets, preview_palette_for(library.name));
                platform::save_png(library_sheet(library.library, palette), out / (library.name + ".png"));
                platform::write_text(out / (library.name + ".txt"), shape_table(library.library));
                std::printf("library %-15s %3zu shapes\n", library.name.c_str(), library.library.shapes.size());
            }
            for (const auto& palette : assets.palettes) {
                platform::save_png(palette_image(palette.colours), out / ("palette-" + palette.name + ".png"));
                std::printf("palette %-13s", palette.name.c_str());
                for (const auto colour : palette.colours) std::printf(" %03X", colour);
                std::printf("\n");
            }
            for (const auto& sample : assets.samples) {
                platform::write_file(out / (sample.name + ".wav"), wav(sample, 8000));
                platform::save_png(sample_image(sample), out / (sample.name + ".png"));
                std::printf("sample %-14s %5zu bytes\n", sample.name.c_str(), sample.data.size());
            }
            for (const auto& map : assets.maps) {
                std::printf("map %s length %u, %zu words\n", map.name.c_str(), map.length, map.words.size());
            }
            std::printf("ranks:");
            for (const auto& rank : assets.rank_names) std::printf(" [%s]", rank.c_str());
            std::printf("\nstory:\n");
            for (const auto& line : assets.story) std::printf("  |%s\n", line.c_str());
            std::printf("high scores:");
            for (const auto& score : assets.high_scores) std::printf(" %u/%d/%s", score.score, score.rank, score.name.c_str());
            std::printf("\n");
            return 0;
        }
        std::fprintf(stderr, "usage: wings_of_fury_assets import <adf> <content> | export <content> <out>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "wings_of_fury_assets: %s\n", error.what());
        return 1;
    }
}
