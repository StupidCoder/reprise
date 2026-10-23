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
#include <cstdio>
#include <fstream>
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
        const auto script = read_script(argv[2]);
        const long first = std::stol(argv[3]), frames = std::stol(argv[4]);
        const std::string out = argv[5];
        std::ofstream log(out + "/frames.txt");
        std::ofstream pictures(out + "/pictures.bin", std::ios::binary);
        log << "# frame | stopped | picture hash\n";
        std::map<std::uint32_t, bool> stored;
        for (long n = 0; n < frames && game.stopped().empty(); ++n) {
            const long frame = first + n;
            const auto picture = game.display();
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
            game.tick(input_at(script, frame));
        }
        if (!game.stopped().empty()) {
            std::printf("%s\n", game.stopped().c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fort_apocalypse_replay: %s\n", e.what());
        return 1;
    }
}
