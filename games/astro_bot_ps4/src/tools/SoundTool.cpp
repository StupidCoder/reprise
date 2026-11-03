// beacon-sound: inspects sound banks (research and verification tool).
//   beacon-sound info FILE.sxd            waves and sound names
//   beacon-sound check DIR                decodes every wave of every bank below DIR
//   beacon-sound wav FILE.sxd INDEX OUT.wav
//   beacon-sound interleave FILE.sxd      roughness of stereo HE-VAG waves per interleave size
//   beacon-sound music GAMEDIR            the music catalog (titles, layers, TV copies)
#include "audio/MusicCatalog.hpp"
#include "audio/WaveDecoder.hpp"
#include "data/SoundBank.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

using namespace beacon;

namespace fs = std::filesystem;

namespace {

const char* codecName(data::WaveCodec c) {
    return c == data::WaveCodec::Atrac9 ? "ATRAC9" : "HE-VAG";
}

bool writeWav(const char* path, const audio::Pcm& pcm) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) {
        f.write(reinterpret_cast<const char*>(&v), 4);
    };
    auto u16 = [&](std::uint16_t v) {
        f.write(reinterpret_cast<const char*>(&v), 2);
    };
    const std::uint32_t bytes = std::uint32_t(pcm.samples.size() * 2);
    f.write("RIFF", 4);
    u32(36 + bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(std::uint16_t(pcm.channels));
    u32(pcm.sampleRate);
    u32(pcm.sampleRate * pcm.channels * 2);
    u16(std::uint16_t(pcm.channels * 2));
    u16(16);
    f.write("data", 4);
    u32(bytes);
    f.write(reinterpret_cast<const char*>(pcm.samples.data()), bytes);
    return bool(f);
}

// Mean absolute sample-to-sample difference relative to the signal level: low for continuous audio.
double roughness(const audio::Pcm& pcm) {
    double diff = 0, level = 0;
    for (std::size_t i = pcm.channels; i < pcm.samples.size(); ++i) {
        diff += std::fabs(double(pcm.samples[i]) - pcm.samples[i - pcm.channels]);
        level += std::fabs(double(pcm.samples[i]));
    }
    return level > 0 ? diff / level : 0;
}

}   // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: beacon-sound info FILE | check DIR | wav FILE INDEX OUT | interleave FILE\n");
        return 2;
    }
    std::string cmd = argv[1];
    data::Bytes bytes;
    std::string error;
    if (cmd == "info" || cmd == "wav" || cmd == "interleave") {
        if (!data::loadFile(argv[2], bytes)) {
            std::fprintf(stderr, "cannot read %s\n", argv[2]);
            return 1;
        }
        auto bank = data::loadSoundBank(bytes, &error);
        if (!bank) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        if (cmd == "info") {
            std::printf("bank '%s': %zu waves, %zu names\n", bank->name.c_str(), bank->waves.size(),
                        bank->names.size());
            for (std::size_t i = 0; i < bank->waves.size(); ++i) {
                const auto& w = bank->waves[i];
                std::printf("  wave %3zu %-6s %u ch %u Hz %8.2f s %s\n", i, codecName(w.codec), w.channels,
                            w.sampleRate, double(w.samples) / w.sampleRate, w.loopStart ? "loop" : "");
                for (const auto& u : bank->waveUses[i]) {
                    std::printf("      use: %s%s%s\n", u.name.c_str(), u.state.empty() ? "" : " | ", u.state.c_str());
                }
            }
            for (const auto& n : bank->names) {
                std::printf("  name '%s' -> request %u\n", n.name.c_str(), n.request);
            }
            return 0;
        }
        if (cmd == "wav" && argc >= 5) {
            std::size_t index = std::stoul(argv[3]);
            audio::Pcm pcm;
            if (index >= bank->waves.size() || !audio::decodeWave(bank->waves[index], pcm, &error)) {
                std::fprintf(stderr, "decode failed: %s\n", error.c_str());
                return 1;
            }
            return writeWav(argv[4], pcm) ? 0 : 1;
        }
        if (cmd == "interleave") {
            for (std::size_t i = 0; i < bank->waves.size(); ++i) {
                const auto& w = bank->waves[i];
                if (w.codec != data::WaveCodec::HeVag || w.channels < 2) {
                    continue;
                }
                std::printf("wave %zu:", i);
                const std::size_t half = w.data.size() / w.channels / 16 * 16;   // channels one after another
                for (std::size_t il : {std::size_t(16), std::size_t(256), std::size_t(2048), std::size_t(8192), half}) {
                    audio::Pcm pcm;
                    audio::decodeWave(w, pcm, nullptr, il);
                    std::printf(" %zu=%.3f", il, roughness(pcm));
                    (void)il;
                }
                std::printf("\n");
            }
            return 0;
        }
        return 2;
    }
    if (cmd == "music") {
        data::GameFiles files;
        if (!files.open(argv[2], &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        audio::MusicCatalog catalog;
        catalog.scan(files);
        int named = 0, tv = 0;
        for (const auto& t : catalog.tracks()) {
            named += !t.uses.empty();
            tv += t.tvOnly;
            std::printf("%-60s %-24s #%-2zu %6.1f s%s%s\n", t.title.c_str(), t.bank.c_str(), t.wave, t.seconds(),
                        t.layers.size() > 1 ? (" +" + std::to_string(t.layers.size() - 1) + " layer").c_str() : "",
                        t.tvOnly ? " (TV)" : "");
        }
        std::printf("%zu tracks, %d named, %d TV-only copies\n", catalog.tracks().size(), named, tv);
        return 0;
    }
    if (cmd == "check") {
        std::map<std::string, int> failures;
        int banks = 0, waves = 0;
        double seconds = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (const auto& e : fs::recursive_directory_iterator(argv[2])) {
            if (e.path().extension() != ".sxd") {
                continue;
            }
            data::Bytes b;
            data::loadFile(e.path(), b);
            auto bank = data::loadSoundBank(b, &error);
            if (!bank) {
                ++failures["bank: " + error];
                continue;
            }
            ++banks;
            for (const auto& w : bank->waves) {
                audio::Pcm pcm;
                if (!audio::decodeWave(w, pcm, &error)) {
                    ++failures[error.substr(0, 22)];
                    if (failures.size() < 4 && failures[error.substr(0, 22)] < 6) {
                        std::printf("  %s: %s\n", e.path().filename().string().c_str(), error.c_str());
                    }
                    continue;
                }
                if (pcm.frames() != w.samples) {
                    ++failures["decoded length differs"];
                }
                ++waves;
                seconds += double(w.samples) / w.sampleRate;
            }
        }
        double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("banks %d, waves decoded %d (%.0f s of audio) in %.1f s\n", banks, waves, seconds, took);
        for (const auto& [message, n] : failures) {
            std::printf("failed %d: %s\n", n, message.c_str());
        }
        return 0;
    }
    return 2;
}
