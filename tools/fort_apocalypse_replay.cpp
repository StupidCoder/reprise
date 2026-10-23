// Runs Joyport's Fort Apocalypse with an input script and records its
// pictures in the format of the reference harness (fortref), so the two can
// be compared frame by frame (.analysis/fort_apocalypse_c64/tools/compare_frames.py).
//
// Usage: fort_apocalypse_replay <content folder> <input script> <first frame> <frames> <out dir>
//
// <first frame> is the reference frame whose interrupt runs the native
// module's first tick (20 for the title as reached from the tape); native
// frame n is recorded as frame <first frame> + n and uses that frame's input.
// The input script is fortref's: "<frame> <joy2> <joy1> [keys]".

#include "fort_apocalypse_c64/game/module.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <map>
#include <sstream>
#include <string>

namespace {
std::map<long, app::GameInput> read_script(const std::string& path) {
    std::map<long, app::GameInput> changes;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream fields(line);
        long frame;
        unsigned joy2, joy1;
        if (!(fields >> frame >> joy2 >> joy1)) {
            continue;
        }
        app::GameInput input{bool(joy2 & 1), bool(joy2 & 2), bool(joy2 & 4), bool(joy2 & 8),
                             bool(joy2 & 16)};
        for (std::string key; fields >> key;) {
            input.f1 |= key == "F1";
            input.f3 |= key == "F3";
            input.f5 |= key == "F5";
            input.f7 |= key == "F7";
            input.space |= key == "SPACE";
        }
        changes[frame] = input;
    }
    return changes;
}

app::GameInput input_at(const std::map<long, app::GameInput>& changes, long frame) {
    app::GameInput input;
    for (const auto& [at, value] : changes) {
        if (at > frame) {
            break;
        }
        input = value;
    }
    return input;
}

std::uint32_t fnv(const std::uint8_t* data, std::size_t size) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: fort_apocalypse_replay <content> <inputs> <first frame> "
                             "<frames> <out dir>\n");
        return 2;
    }
    try {
        auto module = fort_apocalypse::game::start(argv[1]);
        auto& game = static_cast<fort_apocalypse::game::Module&>(*module);
        game.game().seed(1);
        // REPLAY_FIXED_ENEMY=1: the game's option for the enemy pilot.
        if (const char* v = std::getenv("REPLAY_FIXED_ENEMY"); v && *v == '1') {
            game.set_option(0, 1);
        }
        // REPLAY_NOISE=<reference noise.bin>: use the original's random numbers,
        // each read site from its own queue (so the order in which the main
        // loop and the interrupts read them does not matter).
        std::map<std::uint16_t, std::vector<std::uint8_t>> noise;
        std::map<std::uint16_t, std::size_t> noise_at;
        std::size_t used = 0, last_used = 0;
        if (const char* path = std::getenv("REPLAY_NOISE")) {
            std::ifstream in(path, std::ios::binary);
            const std::vector<char> raw{std::istreambuf_iterator<char>(in), {}};
            for (std::size_t i = 0; i + 7 <= raw.size(); i += 7) {
                const auto byte = [&](std::size_t k) { return std::uint8_t(raw[i + k]); };
                const long at = long(byte(0) | byte(1) << 8 | byte(2) << 16);
                const std::uint16_t site = std::uint16_t(byte(4) | byte(5) << 8);
                // Reads before the first replayed frame happened during the
                // module's own start, before this source existed.
                if (at >= std::stol(argv[3])) {
                    noise[site].push_back(byte(6));
                }
            }
            std::uint32_t fallback = 0x2545F491;
            game.game().set_noise_source(
                [&noise, &noise_at, &used, fallback](std::uint16_t site) mutable -> std::uint8_t {
                    auto& queue = noise[site];
                    auto& at = noise_at[site];
                    if (at < queue.size()) {
                        ++used;
                        return queue[at++];
                    }
                    fallback ^= fallback << 13;
                    fallback ^= fallback >> 17;
                    fallback ^= fallback << 5;
                    return std::uint8_t(fallback >> 24);
                });
        }
        const auto script = read_script(argv[2]);
        // REPLAY_STEPS=<reference watch.txt of the main loop's step ends and
        // its wait loop, FORTREF_WATCH=8BC6,8BC9,8BCC,8BCF,8BD2,8BD5,8BD8,8BDB,
        // 8C05,9B99>: run the steps of the main loop's passes in the frames
        // and frame parts (before raster $76, before $F9, after) the original
        // finished them. A step that waited ($9B99 between its start and its
        // end) or ran long (the level build) runs where it started; its end
        // only lets the pass go on. Passes outside play (a jump to $8BDB,
        // then $8C05) are left out: the game runs those itself, one a frame.
        using Steps = fort_apocalypse::game::Game::Steps;
        std::map<long, Steps> steps_early, steps_before, steps_after;
        const bool scheduled = std::getenv("REPLAY_STEPS") != nullptr;
        if (scheduled) {
            std::ifstream in(std::getenv("REPLAY_STEPS"));
            auto steps_at = [&](long at, long raster) -> Steps& {
                return raster < 0x76 ? steps_early[at] : raster < 0xF9 ? steps_before[at] : steps_after[at];
            };
            std::string pc, last = "8c05";
            long at, raster, cycle, last_at = 0, last_raster = 0;
            bool waited = false, outside_play = false;
            while (in >> pc >> at >> raster >> cycle) {
                if (pc == "9b99") {
                    waited = true;
                    continue;
                }
                const bool long_step = (at - last_at) * 312 + raster - last_raster > 312 * 3 / 2;
                if (pc == "8bdb" && last != "8bd8") {
                    outside_play = true;
                } else if (outside_play) {
                    outside_play = pc != "8c05";
                } else if (waited || long_step) {
                    ++steps_at(last_at, last_raster).runs; // the step ran from here
                    ++steps_at(at, raster).returns;        // and returned here
                } else {
                    ++steps_at(at, raster).runs;
                }
                waited = false;
                last = pc;
                last_at = at;
                last_raster = raster;
            }
        }
        // REPLAY_POKE="frame:addr=value/addr=value;frame:...", hex addresses
        // and values, set before that frame's tick (fortref's FORTREF_POKE).
        std::multimap<long, std::pair<std::uint16_t, std::uint8_t>> pokes;
        if (const char* list = std::getenv("REPLAY_POKE")) {
            std::stringstream all(list);
            for (std::string group; std::getline(all, group, ';');) {
                const auto colon = group.find(':');
                const long at = std::stol(group.substr(0, colon));
                std::stringstream items(group.substr(colon + 1));
                for (std::string item; std::getline(items, item, '/');) {
                    const auto eq = item.find('=');
                    pokes.emplace(at, std::pair{std::uint16_t(std::stoul(item.substr(0, eq), nullptr, 16)),
                                                std::uint8_t(std::stoul(item.substr(eq + 1), nullptr, 16))});
                }
            }
        }
        const long first = std::stol(argv[3]), frames = std::stol(argv[4]);
        const std::string out = argv[5];
        std::ofstream log(out + "/frames.txt");
        std::ofstream pictures(out + "/pictures.bin", std::ios::binary);
        log << "# frame | stopped | picture hash\n";
        std::map<std::uint32_t, bool> stored;
        // REPLAY_AUDIO=<file.wav>: the game's sound, 48 kHz 16-bit stereo.
        std::vector<std::int16_t> sound;
        double sample_credit = 0;
        for (long n = 0; n < frames && game.stopped().empty(); ++n) {
            const long frame = first + n;
            if (scheduled) {
                game.game().set_next_steps(steps_early[frame], steps_before[frame], steps_after[frame]);
            }
            for (auto [it, end] = pokes.equal_range(frame); it != end; ++it) {
                if (!game.game().debug_poke(it->second.first, it->second.second)) {
                    std::fprintf(stderr, "cannot poke %04X\n", it->second.first);
                    return 1;
                }
            }
            game.tick(input_at(script, frame));
            if (std::getenv("REPLAY_AUDIO")) {
                sample_credit += game.step_seconds() * 48000;
                std::vector<float> stereo(std::size_t(sample_credit) * 2);
                sample_credit -= double(stereo.size() / 2);
                game.audio(stereo, 48000);
                for (float v : stereo) {
                    sound.push_back(std::int16_t(std::clamp(v, -1.0f, 1.0f) * 32767));
                }
            }
            const auto& picture = game.display();
            std::vector<std::uint8_t> packed(32001);
            for (std::size_t i = 0; i < picture.pixels.size(); i += 2) {
                packed[i / 2] = std::uint8_t(picture.pixels[i] << 4 | picture.pixels[i + 1]);
            }
            packed.back() = picture.border;
            const auto hash = fnv(packed.data(), packed.size());
            if (!stored[hash]) {
                stored[hash] = true;
                const std::uint8_t le[4] = {std::uint8_t(hash), std::uint8_t(hash >> 8),
                                            std::uint8_t(hash >> 16), std::uint8_t(hash >> 24)};
                pictures.write(reinterpret_cast<const char*>(le), 4);
                pictures.write(reinterpret_cast<const char*>(packed.data()), 32001);
            }
            char line[64];
            std::snprintf(line, sizeof line, "%6ld %d %08x\n", frame, !game.stopped().empty(),
                          hash);
            log << line;
            if (std::getenv("REPLAY_OBJECTS") && std::stol(std::getenv("REPLAY_OBJECTS")) <= frame &&
                frame < std::stol(std::getenv("REPLAY_OBJECTS")) + 8) {
                std::printf("%ld %s\n", frame, game.game().debug_objects().c_str());
            }
            if (std::getenv("REPLAY_STATE")) {
                std::printf("%ld %s\n", frame, game.game().debug_line().c_str());
            }
            if (std::getenv("REPLAY_NOISE_LOG") && used != last_used) {
                std::printf("frame %ld: %zu noise reads\n", frame, used - last_used);
                last_used = used;
            }
            if (std::getenv("REPLAY_SCREEN") && std::stol(std::getenv("REPLAY_SCREEN")) == frame) {
                const auto& screen = game.game().screen();
                for (int r = 6; r < 25; ++r) {
                    std::printf("%2d", r);
                    for (int c = 0; c < 40; ++c) {
                        std::printf(" %02X", screen[std::size_t(r * 40 + c)]);
                    }
                    std::printf("\n");
                }
            }
        }
        if (!game.stopped().empty()) {
            std::printf("%s\n", game.stopped().c_str());
        }
        if (const char* path = std::getenv("REPLAY_AUDIO")) {
            std::ofstream wav(path, std::ios::binary);
            auto u32 = [&](std::uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
            auto u16 = [&](std::uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
            const auto bytes = std::uint32_t(sound.size() * 2);
            wav.write("RIFF", 4);
            u32(36 + bytes);
            wav.write("WAVEfmt ", 8);
            u32(16);
            u16(1);
            u16(2);
            u32(48000);
            u32(48000 * 4);
            u16(4);
            u16(16);
            wav.write("data", 4);
            u32(bytes);
            wav.write(reinterpret_cast<const char*>(sound.data()), bytes);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fort_apocalypse_replay: %s\n", e.what());
        return 1;
    }
}
