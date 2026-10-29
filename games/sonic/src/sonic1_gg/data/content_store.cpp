#include "sonic1_gg/data/content_store.hpp"
#include "platform/sha256.hpp"
#include "sonic1_gg/audio/sound.hpp"
#include "sonic1_gg/data/flow_assets.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace sonic1_gg {
namespace fs = std::filesystem;

namespace {
// The manifest names the pack format, the game, the ROM and the extractor
// version; a pack from an older extractor is offered for re-import.
const std::string manifest_body = std::string("sonic-gg\n") + rom_sha256 + "\nextractor 4\n";
const std::string manifest = "BLUESTREAK_PACK 1\n" + manifest_body;
constexpr const char* manifest_file = "pack.manifest";

bool file_equals(const fs::path& file, const std::string& expected) {
    std::error_code ec;
    if (fs::file_size(file, ec) != expected.size() || ec) {
        return false;
    }
    const auto data = read_file(file);
    return std::string(data.begin(), data.end()) == expected;
}

struct Staging {
    fs::path path;
    bool owned{};

    ~Staging() {
        std::error_code ec;
        if (owned) {
            fs::remove_all(path, ec);
        }
    }
};
} // namespace

fs::path pack_path(const fs::path& root) {
    return root / "games" / "sonic-gg";
}

bool pack_installed(const fs::path& root) {
    try {
        return file_equals(pack_path(root) / manifest_file, manifest);
    } catch (const std::exception&) {
        return false;
    }
}

void validate_pack(const fs::path& p) {
    load_level(p);
    load_flow(p);
    load_sound(p);
    for (const char* name : {"act2", "act3", "jungle", "jungle2", "jungle3", "bridge1", "bridge2",
                             "bridge3", "labyrinth1", "labyrinth2", "labyrinth3"}) {
        load_level(p / name);
    }
    for (unsigned n : {12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25, 26}) {
        load_level(p / ("stage" + std::to_string(n)));
    }
    for (unsigned n = 1; n <= 8; ++n) {
        load_level(p / ("special" + std::to_string(n)));
    }
}

void import_rom(const fs::path& image, const fs::path& root, ExtractProgress progress) {
    // Reject large, truncated and different-revision inputs before decoding.
    if (!fs::is_regular_file(image) || fs::file_size(image) != 262144) {
        throw std::runtime_error("Choose the supported 256 KiB Sonic 1 Game Gear ROM. ZIP files "
                                 "and other games are not supported.");
    }
    const auto relative =
        fs::weakly_canonical(image).lexically_relative(fs::weakly_canonical(pack_path(root)));
    if (!relative.empty() && *relative.begin() != "..") {
        throw std::runtime_error(
            "Move the source ROM outside the installed asset folder before importing it.");
    }
    Bytes bytes(262144);
    std::ifstream input(image, std::ios::binary);
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (input.gcount() != std::streamsize(bytes.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("The ROM changed while being read. Try importing a stable copy.");
    }
    if (platform::sha256(bytes) != rom_sha256) {
        throw std::runtime_error("This ROM revision is not supported. Expected Sonic 1 Game Gear "
                                 "Japan/USA, CRC32 3E31CB8C.");
    }
    fs::create_directories(root / "games");
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    Staging staging{root / "games" / (".import-" + std::to_string(stamp))};
    if (!fs::create_directory(staging.path)) {
        throw std::runtime_error("Could not create import workspace. Try again.");
    }
    staging.owned = true;
    // Freeze the verified bytes so a changing source file cannot affect extraction.
    auto snapshot = staging.path / "source.gg";
    write_file(snapshot, bytes);
    Rom rom(snapshot);
    fs::remove(snapshot);
    extract_asset_pack(rom, staging.path, progress);
    validate_pack(staging.path);
    write_file(staging.path / manifest_file, Bytes(manifest.begin(), manifest.end()));
    const auto destination = pack_path(root);
    const fs::path backup = staging.path.string() + "-previous";
    bool moved = false;
    try {
        if (fs::exists(destination)) {
            fs::rename(destination, backup);
            moved = true;
        }
        fs::rename(staging.path, destination);
    } catch (...) {
        if (moved && !fs::exists(destination)) {
            fs::rename(backup, destination);
        }
        throw;
    }
    if (moved) {
        std::error_code ec;
        fs::remove_all(backup, ec);
    }
}
} // namespace sonic1_gg
