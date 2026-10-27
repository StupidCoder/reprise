// Mind Walker research tool: imports the disk image into a content folder and
// exports the decoded data (.analysis/mindwalker_amiga/formats.md).
//
//   mindwalker_assets import <image.adf> <content folder>
//   mindwalker_assets export <content folder> <output folder>
//
// The export writes PNG pictures, the samples as 8-bit WAV files (at 8,000 Hz:
// the stages' playback periods are not read yet) and a summary on stdout
// (palettes, words, high scores).

#include "mindwalker_amiga/data/assets.hpp"
#include "mindwalker_amiga/data/preview.hpp"
#include "mindwalker_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cctype>
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

std::vector<std::uint8_t> wav(const mindwalker::data::Sample& sample, std::uint32_t rate) {
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

std::string file_name(std::string text) {
    std::string out;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

void print_palette(const char* name, const mindwalker::data::Palette& palette) {
    std::printf("%-18s", name);
    for (const auto colour : palette) std::printf(" %03X", colour);
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    using namespace mindwalker::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = mindwalker::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
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
            platform::save_png(ham_image(assets.title), out / "title.png");
            for (std::size_t i = 0; i < assets.portraits.size(); ++i) {
                platform::save_png(portrait_image(assets.portraits[i]), out / ("portrait-" + std::to_string(i + 1) + ".png"));
            }
            for (const auto& set : assets.figures) {
                platform::save_png(frame_sheet(set.frames), out / (file_name(set.name) + ".png"));
                std::printf("%-22s %2zu frames, sequence", set.name.c_str(), set.frames.size());
                for (const int f : set.sequence) std::printf(" %d", f);
                std::printf("\n");
            }
            platform::save_png(frame_sheet(assets.icons, {0x000, 0xFFF}), out / "icons.png");
            platform::save_png(frame_sheet({assets.hands[0], assets.hands[1]}, hands_palette(assets)), out / "hands.png");
            platform::save_png(frame_sheet(assets.arena_images, assets.arena_palette), out / "arena-images.png");
            print_palette("landscape (A)", assets.landscape_palette);
            print_palette("tunnel (B)", assets.tunnel_palette);
            print_palette("arena (C)", assets.arena_palette);
            print_palette("psyche (P)", assets.psyche_palette);
            print_palette("title HAM base (T)", assets.title_palette);
            print_palette("title picture (T)", assets.title.palette);
            for (const auto& sample : assets.samples) {
                const auto name = file_name(sample.name);
                platform::write_file(out / (name + ".wav"), wav(sample, 8000));
                platform::save_png(sample_image(sample), out / (name + ".png"));
                std::printf("sample %-20s %5zu bytes\n", sample.name.c_str(), sample.data.size());
            }
            std::printf("words:");
            for (const auto& word : assets.words) std::printf(" %s", word.c_str());
            std::printf("\nhigh scores:");
            for (const auto& score : assets.high_scores) std::printf(" %u %s", score.score, score.initials.c_str());
            std::printf("\n");
            return 0;
        }
        std::fprintf(stderr, "usage: mindwalker_assets import <adf> <content> | export <content> <out>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "mindwalker_assets: %s\n", error.what());
        return 1;
    }
}
