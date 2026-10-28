// F/A-18 Interceptor research tool: imports the disk image into a content
// folder and exports the decoded data (.analysis/fa_18_interceptor_amiga/formats.md).
//
//   fa_18_interceptor_assets import <image.adf> <content folder>
//   fa_18_interceptor_assets export <content folder> <output folder>
//
// The export writes the pictures, every screen-text block (and the main
// menu put together), the cockpit messages, fonts and palettes as PNG, the
// sounds as WAV (8-bit, at the rate their load-time period gives), and a
// listing of the texts and sounds on stdout.

#include "fa_18_interceptor_amiga/data/assets.hpp"
#include "fa_18_interceptor_amiga/data/preview.hpp"
#include "fa_18_interceptor_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <exception>
#include <string>

namespace {

constexpr double pal_clock = 3546895.0; // Paula's PAL clock

void save_wav(const std::filesystem::path& file, std::span<const std::int8_t> samples, int rate) {
    std::vector<std::uint8_t> wav;
    const auto put = [&](std::uint32_t v, int n) { for (int k = 0; k < n; ++k) wav.push_back(std::uint8_t(v >> (8 * k))); };
    const auto bytes = std::uint32_t(samples.size());
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'}); put(36 + bytes, 4);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(16, 4); put(1, 2); put(1, 2); put(std::uint32_t(rate), 4); put(std::uint32_t(rate), 4); put(1, 2); put(8, 2);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'}); put(bytes, 4);
    for (auto s : samples) wav.push_back(std::uint8_t(s + 128)); // WAV 8-bit is unsigned
    platform::write_file(file, wav);
}

std::string file_name(std::string path) {
    for (auto& c : path) if (c == '/') c = '_';
    return path;
}

} // namespace

int main(int argc, char** argv) {
    using namespace interceptor::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = interceptor::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
            const auto out = platform::utf8_path(argv[3]);
            for (const auto& file : result.files) {
                std::filesystem::create_directories((out / file.name).parent_path());
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
                platform::save_png(picture_image(picture.picture), out / (file_name(picture.file) + ".png"));
            }
            platform::save_png(text_screen(assets, {6, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109}, 0, 12),
                               out / "menu.png");
            for (const auto& block : assets.texts) {
                std::printf("text %3d @%06X row %3d col %2d\n", block.number, block.address, block.row, block.column);
                for (const auto& line : block.lines) {
                    std::printf("    flags %02X colour %2d speed %2d delay %3d |%s|\n", line.flags, line.colour,
                                line.speed, line.delay, line.text.c_str());
                }
                char name[32];
                std::snprintf(name, sizeof name, "text%03d.png", block.number);
                platform::save_png(text_screen(assets, {block.number}, 0), out / name);
            }
            for (std::size_t i = 0; i < assets.messages.size(); ++i) {
                std::printf("message %2zu |%s|\n", i, assets.messages[i].c_str());
            }
            platform::save_png(message_sheet(assets), out / "messages.png");
            platform::save_png(font_sheet(assets), out / "fonts.png");
            platform::save_png(palette_sheet(assets), out / "palettes.png");
            for (const auto& sound : assets.sounds) {
                const int rate = int(pal_clock / sound.period + 0.5);
                std::printf("sound %-14s slot %2d period %3d (%5d Hz) %6zu bytes\n", sound.file.c_str(), sound.slot,
                            sound.period, rate, sound.samples.size());
                save_wav(out / (file_name(sound.file) + ".wav"), sound.samples, rate);
            }
            return 0;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: fa_18_interceptor_assets import <image.adf> <content> | export <content> <out>\n");
    return 2;
}
