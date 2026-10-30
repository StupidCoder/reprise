// Beach Volley research tool: imports the disk image into a content folder
// and exports the decoded data (games/beach_volley_amiga/game.md).
//
//   beach_volley_assets import <image.adf> <content folder>
//   beach_volley_assets export <content folder> <output folder>
//   beach_volley_assets intro <content folder> <output folder> <blank>...
//       runs the game from its start without input and saves the picture
//       at each listed vertical blank (intro-NNNN.png)
//   beach_volley_assets music-trace <content folder> <blanks>
//       the intro's music player after each vertical blank (position, row,
//       counter, speed; per channel the note and the block it plays)
//   beach_volley_assets save-check <content folder> <blank>...
//       saves at each listed blank, reads the save into a new game and runs
//       both 600 blanks on, comparing picture and sound
//   beach_volley_assets sound <content folder> <out.wav> <blanks>
//       the game's sound from its start for that many vertical blanks

#include "beach_volley_amiga/data/assets.hpp"
#include "beach_volley_amiga/entry.hpp"
#include "beach_volley_amiga/data/preview.hpp"
#include "beach_volley_amiga/front/intro.hpp"
#include "beach_volley_amiga/game/module.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include "beach_volley_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <algorithm>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "import" && argc == 4) {
            const auto image = platform::read_file(argv[2]);
            const auto result = beach_volley::import::import_disk(image);
            std::filesystem::create_directories(argv[3]);
            for (const auto& file : result.files) platform::write_file(std::filesystem::path(argv[3]) / file.name, file.bytes);
            std::printf("%s: %zu files\n", result.release.c_str(), result.files.size());
            return 0;
        }
        if (command == "export" && argc == 4) {
            using namespace beach_volley::data;
            const auto assets = load_assets(argv[2]);
            const std::filesystem::path out = argv[3];
            std::filesystem::create_directories(out);
            for (const auto& court : assets.courts) {
                platform::save_png(to_image(court.picture, court.palette),
                                   out / ("court" + std::to_string(court.number) + ".png"));
            }
            std::printf("%zu courts\n", assets.courts.size());
            // And every preview the launcher shows (preview-NN.png).
            const auto previews = beach_volley::entry().previews(argv[2]);
            for (std::size_t i = 0; i < previews.size(); ++i) {
                char name[32];
                std::snprintf(name, sizeof name, "preview-%02zu.png", i);
                platform::save_png(previews[i].image, out / name);
            }
            std::printf("%zu previews\n", previews.size());
            return 0;
        }
        if (command == "intro" && argc >= 5) {
            std::set<long> blanks;
            for (int i = 4; i < argc; ++i) blanks.insert(std::strtol(argv[i], nullptr, 10));
            auto game = beach_volley::game::start(argv[2]);
            std::filesystem::create_directories(argv[3]);
            for (long blank = 1; blank <= *blanks.rbegin(); ++blank) {
                game->tick({});
                if (!blanks.count(blank)) continue;
                char name[32];
                std::snprintf(name, sizeof name, "intro-%04ld.png", blank);
                platform::save_png(game->picture(), std::filesystem::path(argv[3]) / name);
            }
            return 0;
        }
        if (command == "music-trace" && argc == 4) {
            using namespace beach_volley;
            const auto files = data::load_intro_files(argv[2]);
            auto song = sound::Song::parse(files.song);
            song.halve_volumes();
            sound::Player player(sound::read_player_tables(files.code, front::intro_sample_table,
                                                           front::intro_period_table, files.instruments.size()),
                                 front::intro_note_channels, front::intro_effect_channels);
            amiga::PaulaChannels paula;
            player.start(song, paula);
            const long blanks = std::strtol(argv[3], nullptr, 10);
            for (long blank = 1; blank <= blanks; ++blank) {
                player.tick(paula);
                std::printf("%ld %d %d %d %d", blank, player.position(), player.row(), player.counter(), player.speed());
                for (int n = 0; n < 4; ++n) {
                    const auto& c = player.channel(n);
                    std::printf(" | %04x %02x %02x %x %x %x %x %x %x", c.period, c.command, c.parameter, c.start, c.length,
                                c.repeat_start, c.repeat_length, c.note_period, c.slide_period);
                }
                std::printf("\n");
            }
            return 0;
        }
        if (command == "save-check" && argc >= 4) {
            for (int i = 3; i < argc; ++i) {
                const long at = std::strtol(argv[i], nullptr, 10);
                auto game = beach_volley::game::start(argv[2]);
                std::vector<float> a(1764), b(1764);
                for (long blank = 0; blank < at; ++blank) {
                    game->tick({});
                    game->audio(a, 44100);
                }
                const auto saved = game->write_state();
                auto copy = beach_volley::game::start(argv[2]);
                copy->read_state(saved);
                long differ = -1;
                for (long blank = 0; blank < 600 && differ < 0; ++blank) {
                    game->tick({});
                    copy->tick({});
                    game->audio(a, 44100);
                    copy->audio(b, 44100);
                    const auto p = game->picture(), q = copy->picture();
                    const bool same_picture = std::equal(p.pixels.begin(), p.pixels.end(), q.pixels.begin(),
                                                         [](const auto& x, const auto& y) {
                                                             return x.r == y.r && x.g == y.g && x.b == y.b;
                                                         });
                    if (a != b || !same_picture) differ = blank;
                }
                std::printf("blank %ld: %zu bytes saved, %s\n", at, saved.size(),
                            differ < 0 ? "same for 600 blanks" : ("differs after " + std::to_string(differ)).c_str());
            }
            return 0;
        }
        if (command == "sound" && argc == 5) {
            constexpr int rate = 44100;
            const long blanks = std::strtol(argv[4], nullptr, 10);
            auto game = beach_volley::game::start(argv[2]);
            std::vector<std::int16_t> pcm;
            double credit = 0;
            std::vector<float> frame;
            for (long blank = 0; blank < blanks; ++blank) {
                game->tick({});
                credit += rate / 50.0;
                const int samples = int(credit);
                credit -= samples;
                frame.assign(std::size_t(samples) * 2, 0.0f);
                game->audio(frame, rate);
                for (const float f : frame) pcm.push_back(std::int16_t(std::clamp(f, -1.0f, 1.0f) * 32767));
            }
            std::ofstream out(argv[3], std::ios::binary);
            const auto put = [&](std::uint32_t v, int bytes) {
                for (int i = 0; i < bytes; ++i) out.put(char(v >> (8 * i) & 0xFF));
            };
            const std::uint32_t data = std::uint32_t(pcm.size() * 2);
            out.write("RIFF", 4); put(36 + data, 4); out.write("WAVEfmt ", 8); put(16, 4); put(1, 2); put(2, 2);
            put(rate, 4); put(rate * 4, 4); put(4, 2); put(16, 2); out.write("data", 4); put(data, 4);
            out.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(data));
            return 0;
        }
        std::fprintf(stderr, "usage: beach_volley_assets import <image.adf> <content> | export <content> <out> | "
                             "intro <content> <out> <blank>... | sound <content> <out.wav> <blanks>\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
