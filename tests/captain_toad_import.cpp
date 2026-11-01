// Checks random-access import recognition, hostile container bounds and Library staging.
// Optional ROM/output arguments perform a real import for package-equivalence validation.
#include "captain_toad_3ds/reprise/import.hpp"
#include "captain_toad_3ds/src/importer/RomImage.hpp"
#include "captain_toad_3ds/src/importer/Archive.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace {
using captain_toad::Bytes;
void require(bool b, const char* why) {
    if (!b) {
        throw std::runtime_error(why);
    }
}
void put(Bytes& b, size_t at, uint64_t v, int n = 4) {
    for (int k = 0; k < n; ++k) {
        b.at(at + k) = uint8_t(v >> (k * 8));
    }
}
Bytes cartridge() {
    Bytes b(0x1000);
    std::memcpy(b.data() + 0x100, "NCSD", 4);
    put(b, 0x120, 1);
    std::memcpy(b.data() + 0x300, "NCCH", 4);
    put(b, 0x318, 0x00040000001cb200ull, 8);
    b[0x38f] = 4;
    put(b, 0x3a0, 1);
    put(b, 0x3a4, 1);
    put(b, 0x3b0, 2);
    std::memcpy(b.data() + 0x600, "IVFC", 4);
    put(b, 0x604, 0x10000);
    put(b, 0x64c, 8);
    put(b, 0x700, 0x28);
    put(b, 0x70c, 0x28);
    put(b, 0x710, 0x18);
    put(b, 0x71c, 0x40);
    put(b, 0x720, 0x28);
    put(b, 0x724, 0x100);
    put(b, 0x730, 0xffffffff);
    put(b, 0x734, 0xffffffff);
    return b;
}
template <class F>
void rejects(F f, const char* why) {
    try {
        f();
    } catch (const captain_toad::ImportError&) {
        return;
    }
    throw std::runtime_error(why);
}
captain_toad::RomImage reader(const Bytes& b, size_t& largest) {
    return captain_toad::RomImage(b.size(), [&](uint64_t at, std::span<uint8_t> out) {
        require(at <= b.size() && out.size() <= b.size() - at, "reader exceeded source");
        largest = std::max(largest, out.size());
        std::copy_n(b.data() + at, out.size(), out.data());
    });
}
void tests() {
    size_t largest = 0;
    auto b = cartridge();
    auto image = reader(b, largest);
    require(image.programId() == 0x00040000001cb200ull, "program identity changed");
    require(largest == 0x200, "reader unexpectedly buffered the entire image");
    rejects([&] { image.romfsFile("missing"); }, "missing file accepted");
    auto bad = b;
    put(bad, 0x64c, 64);
    rejects([&] { reader(bad, largest); }, "undefined block shift accepted");
    bad = b;
    bad[0x38f] = 0;
    rejects([&] { reader(bad, largest); }, "encrypted image accepted");
    bad = b;
    put(bad, 0x734, 0);
    put(bad, 0x744, 0);   // file 0 points to itself
    rejects(
        [&] {
            auto r = reader(bad, largest);
            r.romfsFile("missing");
        },
        "cyclic file table accepted");
    bad = b;
    put(bad, 0x730, 0);
    put(bad, 0x72c, 0);   // root child/sibling point to root
    rejects(
        [&] {
            auto r = reader(bad, largest);
            r.romfsFile("missing/file");
        },
        "cyclic directory table accepted");
    bad = b;
    put(bad, 0x734, 0);
    put(bad, 0x75c, 2);
    bad[0x760] = 'x';
    put(bad, 0x748, ~uint64_t(0), 8);
    rejects(
        [&] {
            auto r = reader(bad, largest);
            r.romfsFile("x");
        },
        "overflowing member offset accepted");
    Bytes code(8);
    put(code, 0, 0x08000008);
    put(code, 4, 0xffffffff);
    rejects([&] { captain_toad::decompressCode(code); }, "unbounded code expansion accepted");
    Bytes sarc(0x14);
    std::memcpy(sarc.data(), "SARC", 4);
    sarc[6] = 255;
    sarc[7] = 254;
    put(sarc, 4, 0xffff, 2);
    rejects([&] { captain_toad::sarc(sarc); }, "out-of-range SARC header accepted");
    const auto temp =
        std::filesystem::temp_directory_path() /
        ("reprise-import-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(temp), "temporary directory already exists");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{temp};
    const auto file = temp / "image.cci", staging = temp / "stage";
    std::filesystem::create_directory(staging);
    auto probe = [&](const Bytes& bytes, bool unrelated) {
        {
            std::ofstream f(file, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        ::platform::FileSource source(file);
        try {
            captain_toad::reprise::import_into(source, staging, {});
        } catch (const app::NotThisGame&) {
            require(unrelated, "recognized image hidden as unrelated");
            return;
        } catch (const captain_toad::ImportError&) {
            require(!unrelated, "unrelated image reported as damaged");
            return;
        }
        throw std::runtime_error("invalid cartridge imported");
    };
    probe(Bytes(7), true);
    bad = b;
    put(bad, 0x318, 123, 8);
    probe(bad, true);
    bad = b;
    bad[0x38f] = 0;
    probe(bad, false);
    probe(b, false);   // recognized, but no supported code payload
    require(std::filesystem::is_empty(staging), "failed recognition modified staging");
    {
        std::ofstream marker(staging / "preserve.txt");
        marker << "existing content";
    }
    probe(b, false);
    require(std::filesystem::file_size(staging / "preserve.txt") == 16, "import changed an occupied staging directory");
    std::puts(
        "PASS: random access, identity/errors, encryption rejection, bounded/cyclic tables, decompression, staging");
}
}   // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            tests();
            return 0;
        }
        require(argc == 3, "usage: captain_toad_import_check [IMAGE NEW_OUTPUT_FOLDER]");
        const std::filesystem::path out(argv[2]);
        require(std::filesystem::create_directory(out), "output must be a new directory");
        ::platform::FileSource source(argv[1]);
        std::string last;
        const auto release = captain_toad::reprise::import_into(source, out, [&](const std::string& phase, double f) {
            require(!phase.empty() && f >= 0 && f <= 1, "invalid progress callback");
            if (phase != last) {
                std::printf("%.1f%% %s\n", f * 100, phase.c_str());
                last = phase;
            }
        });
        std::printf("Imported %s\n", release.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
