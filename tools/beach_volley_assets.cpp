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
//   beach_volley_assets match-save <content> <script> <offset> <blanks> <save blank>...
//       runs a one-player match on a script, saves the match at each listed
//       blank, reads it into a new match and runs both on to <blanks>,
//       comparing their screens every blank
//   beach_volley_assets match-trace <content> <venue> <script> <offset> <chance> <blanks> [<png folder>]
//       runs a match on the research harness's input script (lines
//       "<vblank> <port1 dirs|-> <fire 0/1> [<port0 dirs|-> <fire>]", the
//       match's blank 0 being harness vblank <offset>), the chance table
//       starting at <chance>; prints the object table after each pass of
//       the program's loop and optionally saves the pictures

#include "beach_volley_amiga/data/assets.hpp"
#include "beach_volley_amiga/entry.hpp"
#include "beach_volley_amiga/data/preview.hpp"
#include "beach_volley_amiga/front/ending.hpp"
#include "beach_volley_amiga/front/intro.hpp"
#include "beach_volley_amiga/front/venue_card.hpp"
#include "beach_volley_amiga/game/module.hpp"
#include "beach_volley_amiga/match/match.hpp"
#include "beach_volley_amiga/sound/soundtracker.hpp"
#include "beach_volley_amiga/import/disk_import.hpp"
#include "platform/files.hpp"
#include "state/archive.hpp"
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
        if (command == "match-save" && argc >= 7) {
            namespace bm = beach_volley::match;
            const auto data = bm::load_match_data(argv[2], 1);
            std::vector<std::pair<long, bm::MatchInput>> script;
            std::ifstream in(argv[3]);
            for (std::string line; std::getline(in, line);) {
                char d1[16] = "-";
                long v = 0;
                int f1 = 0;
                if (line.empty() || line[0] == '#' || std::sscanf(line.c_str(), "%ld %15s %d", &v, d1, &f1) < 3) continue;
                const std::string s(d1);
                script.push_back({v, {{s.find('u') != std::string::npos, s.find('d') != std::string::npos,
                                       s.find('l') != std::string::npos, s.find('r') != std::string::npos, f1 != 0},
                                      {}}});
            }
            const long offset = std::strtol(argv[4], nullptr, 10), blanks = std::strtol(argv[5], nullptr, 10);
            const auto input_at = [&](long b) {
                bm::MatchInput input;
                for (const auto& [v, i] : script)
                    if (v <= b + offset) input = i;
                return input;
            };
            int failures = 0;
            for (int a = 6; a < argc; ++a) {
                const long at = std::strtol(argv[a], nullptr, 10);
                amiga::PaulaChannels paula1, paula2;
                bm::MatchSound sound1(paula1), sound2(paula2);
                sound1.bind(data);
                sound2.bind(data);
                bm::Match first(data, 1, sound1);
                for (long b = 0; b < at; ++b) first.tick(input_at(b));
                state::Writer w;
                first.persist(w);
                sound1.persist(w);
                paula1.persist(w);
                bm::Match second(data, 1, sound2);
                state::Reader r(w.bytes);
                second.persist(r);
                sound2.persist(r);
                paula2.persist(r);
                r.finish();
                long bad = -1;
                for (long b = at; b < blanks && bad < 0; ++b) {
                    first.tick(input_at(b));
                    second.tick(input_at(b));
                    if (first.screen() != second.screen()) bad = b;
                }
                std::printf("save at %ld (%zu bytes): %s\n", at, w.bytes.size(),
                            bad < 0 ? "identical to the end" : ("differs at blank " + std::to_string(bad)).c_str());
                if (bad >= 0) ++failures;
            }
            return failures ? 1 : 0;
        }
        if (command == "match-trace" && argc >= 8) {
            namespace bm = beach_volley::match;
            const auto data = bm::load_match_data(argv[2], std::atoi(argv[3]));
            struct Event { long vblank; bm::MatchInput input; };
            std::vector<Event> script;
            std::ifstream in(argv[4]);
            for (std::string line; std::getline(in, line);) {
                if (line.empty() || line[0] == '#') continue;
                char d1[16] = "-", d0[16] = "-";
                long v = 0;
                int f1 = 0, f0 = 0;
                if (std::sscanf(line.c_str(), "%ld %15s %d %15s %d", &v, d1, &f1, d0, &f0) < 3) continue;
                const auto stick = [](const char* d, int fire) {
                    const std::string s(d);
                    return bm::Stick{s.find('u') != std::string::npos, s.find('d') != std::string::npos,
                                     s.find('l') != std::string::npos, s.find('r') != std::string::npos, fire != 0};
                };
                script.push_back({v, {stick(d1, f1), stick(d0, f0)}});
            }
            const long offset = std::strtol(argv[5], nullptr, 10);
            amiga::PaulaChannels paula;
            bm::MatchSound sound(paula);
            // Research: BV_AFTER_CHEAT=<chance place> plays the first venue's
            // match first, won at once by the cheat (HELP held), and traces
            // the next match on <venue> as the tour goes on to it.
            const char* after_cheat = std::getenv("BV_AFTER_CHEAT");
            const auto first = after_cheat ? bm::load_match_data(argv[2], 1) : bm::MatchData{};
            sound.bind(after_cheat ? first : data);
            bm::Match match(after_cheat ? first : data, 1, sound);
            if (after_cheat) {
                match.set_chance_place(std::atoi(after_cheat));
                match.set_cheat(true);
                bm::MatchInput help;
                help.help = true;
                while (!match.finished()) match.tick(help);
                std::fprintf(stderr, "first match over at blank %d, chance place %d\n", match.blanks(), match.chance_place());
                sound.bind(data);
                match.restart(data);
                match.set_chance_place(match.chance_place() + 5);
            }
            std::FILE* sound_log = nullptr; // research: BV_SOUND=<file> logs what the sound does per blank
            if (const char* path = std::getenv("BV_SOUND")) sound_log = std::fopen(path, "w");
            if (!after_cheat) match.set_chance_place(std::atoi(argv[6]));
            if (const char* pauses = std::getenv("BV_PAUSES")) { // research: a recording's point pauses
                std::vector<int> list;
                for (const char* p = pauses; *p;) {
                    char* end = nullptr;
                    list.push_back(int(std::strtol(p, &end, 10)));
                    p = *end ? end + 1 : end;
                }
                match.set_point_pauses(list);
            }
            const long blanks = std::strtol(argv[7], nullptr, 10);
            std::size_t k = 0;
            bm::MatchInput input;
            for (long b = 0; b < blanks; ++b) {
                while (k < script.size() && script[k].vblank <= b + offset) input = script[k++].input;
                match.tick(input);
                if (sound_log) {
                    const auto& p = sound.player();
                    std::fprintf(sound_log, "%ld %d %d %d %d %d", b, p.position(), p.row(), p.counter(),
                                 sound.music_on() ? 1 : 0, sound.volume());
                    for (const int h : sound.take_heard()) std::fprintf(sound_log, " %d", h);
                    std::fprintf(sound_log, "\n");
                }
                if (!match.stepped()) continue;
                std::printf("%ld", b);
                std::fprintf(stderr, "%ld %s\n", b, match.debug().c_str());
                for (int n = 0; n < bm::object_count; ++n) {
                    const auto& o = match.object(bm::Object(n));
                    std::printf(" %d %d %x %d %d", o.x, o.y, unsigned(o.image.at + bm::sprite_block_address),
                                o.image.width, o.image.height);
                }
                std::printf("\n");
                if (argc >= 9 && std::string(argv[8]).ends_with(".bin")) {
                    static std::ofstream screens(argv[8], std::ios::binary);
                    const auto px = match.screen();
                    screens.write(reinterpret_cast<const char*>(px.data()), std::streamsize(px.size()));
                } else if (argc >= 9) {
                    char name[64];
                    std::snprintf(name, sizeof name, "/match-%05ld.png", b);
                    platform::save_png(match.picture(), std::string(argv[8]) + name);
                }
            }
            return 0;
        }
        if (command == "tour" && argc == 6) {
            // tour <content> <out dir> <blanks> <png every>: the game with F1
            // held (one player), fire pressed every 40th blank and the win
            // cheat (F7) held: pictures every <png every> blanks (research).
            auto game = beach_volley::game::start(argv[2]);
            game->set_option(1, 1);
            if (std::getenv("BV_WIDE")) { // research: a 21:9 window, widescreen on
                game->set_option(2, 1);
                game->set_canvas(2100, 900);
            }
            if (std::getenv("BV_SMOOTH")) game->set_option(3, 1); // pictures half a blank on
            game->set_frame_fraction(0.5);
            std::filesystem::create_directories(argv[3]);
            const long blanks = std::strtol(argv[4], nullptr, 10), every = std::strtol(argv[5], nullptr, 10);
            std::vector<float> audio(2 * 882);
            for (long blank = 1; blank <= blanks; ++blank) {
                app::GameInput in;
                in.f1 = true;
                in.fire = blank % 40 == 0;
                in.f7 = true;
                game->tick(in);
                game->audio(audio, 44100);
                if (blank % every == 0) {
                    char name[32];
                    std::snprintf(name, sizeof name, "tour-%06ld.png", blank);
                    platform::save_png(game->picture(), std::filesystem::path(argv[3]) / name);
                }
                if (!game->stopped().empty()) {
                    std::printf("stopped at %ld: %s\n", blank, game->stopped().c_str());
                    break;
                }
            }
            return 0;
        }
        if (command == "tour-save" && argc >= 4) {
            // tour-save <content> <blank>...: the tour as `tour` plays it,
            // saved at each blank, read into a fresh game, and both run on
            // for 1,500 blanks: picture and sound must stay equal.
            const auto input_at = [](long blank) {
                app::GameInput in;
                in.f1 = true;
                in.fire = blank % 40 == 0;
                in.f7 = true;
                return in;
            };
            int failures = 0;
            for (int a = 3; a < argc; ++a) {
                const long at = std::strtol(argv[a], nullptr, 10);
                auto game = beach_volley::game::start(argv[2]);
                game->set_option(1, 1);
                std::vector<float> x(2 * 882), y(2 * 882);
                for (long blank = 1; blank <= at; ++blank) {
                    game->tick(input_at(blank));
                    game->audio(x, 44100);
                }
                const auto saved = game->write_state();
                auto copy = beach_volley::game::start(argv[2]);
                copy->read_state(saved);
                long differ = -1;
                for (long blank = at + 1; blank <= at + 1500 && differ < 0; ++blank) {
                    game->tick(input_at(blank));
                    copy->tick(input_at(blank));
                    game->audio(x, 44100);
                    copy->audio(y, 44100);
                    const auto p = game->picture(), q = copy->picture();
                    const bool same = p.width == q.width && std::equal(p.pixels.begin(), p.pixels.end(), q.pixels.begin(),
                                                                       [](const auto& u, const auto& v) {
                                                                           return u.r == v.r && u.g == v.g && u.b == v.b;
                                                                       });
                    if (x != y || !same) {
                        differ = blank;
                        std::printf("  %s differs\n", same ? "sound" : "picture");
                    }
                }
                std::printf("blank %ld: %zu bytes, %s\n", at, saved.size(),
                            differ < 0 ? "same for 1500 blanks" : ("differs at " + std::to_string(differ)).c_str());
                if (differ >= 0) ++failures;
            }
            return failures ? 1 : 0;
        }
        if (command == "smooth-frames" && argc == 6) {
            // smooth-frames <content> <from blank> <blanks> <out dir>: the tour
            // (as `tour`, smooth motion on) pictured at four points of each
            // blank from <from blank> on (research).
            auto game = beach_volley::game::start(argv[2]);
            game->set_option(1, 1);
            game->set_option(3, 1);
            std::filesystem::create_directories(argv[5]);
            const long from = std::strtol(argv[3], nullptr, 10), count = std::strtol(argv[4], nullptr, 10);
            std::vector<float> audio(2 * 882);
            for (long blank = 1; blank < from + count; ++blank) {
                app::GameInput in;
                in.f1 = true;
                in.fire = blank % 40 == 0;
                game->tick(in);
                game->audio(audio, 44100);
                if (blank < from) continue;
                for (int q = 0; q < 4; ++q) {
                    game->set_frame_fraction(q / 4.0);
                    char name[48];
                    std::snprintf(name, sizeof name, "smooth-%06ld-%d.png", blank, q);
                    platform::save_png(game->picture(), std::filesystem::path(argv[5]) / name);
                }
            }
            return 0;
        }
        if (command == "ending-trace" && argc == 5) {
            // ending-trace <content> <blanks> <screens.bin>: the ending from
            // its song's start: per loop frame "blank x y picture x y picture
            // colour8..15 frames" (pictures as addresses), and the screen (4
            // planes of 600 rows) after the scroll and at loop frames 0-3, 10,
            // 100, 799-802, 810, 900, 1000, 1100 (research: harness/final.py).
            namespace bf = beach_volley::front;
            const auto files = bf::load_ending_files(argv[2]);
            amiga::PaulaChannels paula;
            bf::Ending ending(files, paula);
            ending.begin();
            std::ofstream screens(argv[4], std::ios::binary);
            std::set<int> grab{0, 1, 2, 3, 10, 100, 799, 800, 801, 802, 810, 900, 1000, 1100};
            if (const char* list = std::getenv("BV_FRAMES")) { // research: other loop frames to grab
                grab.clear();
                for (const char* p = list; *p;) {
                    char* end = nullptr;
                    grab.insert(int(std::strtol(p, &end, 10)));
                    p = *end ? end + 1 : end;
                }
            }
            const long blanks = std::strtol(argv[3], nullptr, 10);
            int shown = 0, top = -1;
            for (long b = 0; b < blanks; ++b) {
                ending.tick(false);
                if (ending.top_row() == 201 && top != 201) {
                    std::printf("%ld scrolled\n", b);
                    screens.write(reinterpret_cast<const char*>(ending.screen().data()), std::streamsize(ending.screen().size()));
                }
                top = ending.top_row();
                if (ending.frames() == shown) continue;
                shown = ending.frames(); // loop frame shown - 1 drawn this blank
                const auto& f = ending.figures();
                std::printf("%ld %d %d %x %d %d %x", b, f[0].x, f[0].y, f[0].picture + 0x5C290u, f[1].x, f[1].y,
                            f[1].picture + 0x5C290u);
                for (int i = 8; i < 16; ++i) std::printf(" %x", ending.colours()[std::size_t(i)]);
                std::printf(" %d\n", shown);
                if (grab.count(shown - 1)) screens.write(reinterpret_cast<const char*>(ending.screen().data()), std::streamsize(ending.screen().size()));
            }
            return 0;
        }
        if (command == "card-trace" && argc == 6) {
            // card-trace <content> <venue> <blanks> <out.bin>: the venue card's
            // screen buffer (400 x 200 indices), its upper palette (16 words),
            // the split row, the scroll (bytes, pixels) after every blank, with
            // fire held from the start (research: compare with harness/card.py).
            namespace bf = beach_volley::front;
            const auto files = bf::load_card_files(argv[2], std::atoi(argv[3]));
            amiga::PaulaChannels paula;
            bf::VenueCard card(files, paula);
            card.begin();
            std::ofstream out(argv[5], std::ios::binary);
            const long blanks = std::strtol(argv[4], nullptr, 10);
            for (long b = 0; b < blanks && !card.finished(); ++b) {
                card.tick(true);
                const auto& s = card.screen();
                out.write(reinterpret_cast<const char*>(s.data()), std::streamsize(s.size()));
                for (const auto w : card.upper()) {
                    out.put(char(w >> 8));
                    out.put(char(w & 0xFF));
                }
                const std::int16_t extra[4] = {std::int16_t(card.split_row()), std::int16_t(card.pointer_bytes()),
                                               std::int16_t(card.fine_scroll()), std::int16_t(card.at())};
                for (const auto e : extra) {
                    out.put(char(e >> 8));
                    out.put(char(e & 0xFF));
                }
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
