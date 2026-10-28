// Tower of Babel research tool: imports the disk image into a content folder
// and exports the decoded data (.analysis/tower_of_babel_amiga/formats.md).
//
//   tower_of_babel_assets import <image.adf> <content folder>
//   tower_of_babel_assets export <content folder> <output folder>
//
// The export writes the title screen and every tower map as PNG, the sampled
// pieces and effect waveforms as WAV (8-bit, at the rate their period gives;
// waveforms at 8,363 Hz), and a tower list on stdout.

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/data/preview.hpp"
#include "tower_of_babel_amiga/import/disk_import.hpp"
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

} // namespace

int main(int argc, char** argv) {
    using namespace babel::data;
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "import" && argc == 4) {
            const auto result = babel::import::import_disk(platform::read_file(platform::utf8_path(argv[2])));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            for (const auto& file : result.files) platform::write_file(out / file.name, file.bytes);
            std::printf("%s: %zu files\n", result.release.c_str(), result.files.size());
            return 0;
        }
        if (command == "export" && argc == 4) {
            const auto assets = load_assets(platform::utf8_path(argv[2]));
            const auto out = platform::utf8_path(argv[3]);
            std::filesystem::create_directories(out);
            platform::save_png(title_image(assets.title), out / "title.png");
            int number = 0;
            for (const auto& group : assets.groups) {
                for (std::size_t i = 0; i < group.towers.size(); ++i, ++number) {
                    const auto& t = group.towers[i];
                    std::printf("%-14s %c  %-45s %-20s pw %-8s spiders %c%c%c  klondikes %d destroy %d time %s\n",
                                group.name.c_str(), char('A' + int(i % 9)), t.name.c_str(), t.author.c_str(),
                                t.password.c_str(), t.spider_available(0) ? 'Z' : '-', t.spider_available(1) ? 'P' : '-',
                                t.spider_available(2) ? 'G' : '-', t.klondikes, t.objects_to_destroy,
                                t.has_time_limit() ? std::to_string(t.time_limit).c_str() : "none");
                    char name[32];
                    std::snprintf(name, sizeof name, "tower%03d.png", number);
                    platform::save_png(tower_image(t, assets.program), out / name);
                }
            }
            for (const auto& piece : assets.pieces) {
                const int rate = int(pal_clock / piece.period + 0.5);
                std::printf("%s: %zu bytes, period %d (%d Hz), volume %d\n", piece.name.c_str(), piece.samples.size(),
                            piece.period, rate, piece.volume);
                std::string file = piece.name;
                for (auto& c : file) if (c == ' ') c = '_';
                save_wav(out / (file + ".wav"), piece.samples, rate);
            }
            for (const auto& effect : assets.effects) {
                std::printf("effect $%04X: waveform $%05X, %zu bytes\n", effect.record, effect.waveform,
                            effect.samples.size());
            }
            return 0;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::fprintf(stderr, "usage: tower_of_babel_assets import <image.adf> <content> | export <content> <out>\n");
    return 2;
}
