#include "sonic_advance_gba/data/content_store.hpp"
#include "platform/sha256.hpp"
#include "sonic_advance_gba/data/assets.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

namespace sonic_advance_gba {
namespace fs = std::filesystem;

namespace {
// The manifest names the pack format, the game, the ROM and the extractor
// version; a pack from an older extractor is offered for re-import.
const std::string manifest_body =
    std::string("sonic-advance-gba\n") + rom_sha256 + "\nextractor 1\n";
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

Bytes read_exactly(const fs::path& image) {
    Bytes bytes(rom_size);
    std::ifstream input(image, std::ios::binary);
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (input.gcount() != std::streamsize(bytes.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("The ROM changed while being read. Try importing a stable copy.");
    }
    return bytes;
}
} // namespace

fs::path pack_path(const fs::path& root) {
    return root / "games" / "sonic-advance-gba";
}

bool pack_installed(const fs::path& root) {
    try {
        return file_equals(pack_path(root) / manifest_file, manifest);
    } catch (const std::exception&) {
        return false;
    }
}

void validate_pack(const fs::path& p) {
    Assets::load(p);
}

bool is_supported_rom(const fs::path& image) {
    std::error_code ec;
    if (!fs::is_regular_file(image, ec) || fs::file_size(image, ec) != rom_size) {
        return false;
    }
    try {
        return platform::sha256(read_exactly(image)) == rom_sha256;
    } catch (const std::exception&) {
        return false;
    }
}

void import_rom(const fs::path& image, const fs::path& root, ExtractProgress progress) {
    // Reject large, truncated and different-revision inputs before decoding.
    if (!fs::is_regular_file(image) || fs::file_size(image) != rom_size) {
        throw std::runtime_error(
            "Choose the supported 8 MiB Sonic Advance Game Boy Advance ROM. ZIP files "
            "and other games are not supported.");
    }
    const auto relative =
        fs::weakly_canonical(image).lexically_relative(fs::weakly_canonical(pack_path(root)));
    if (!relative.empty() && *relative.begin() != "..") {
        throw std::runtime_error(
            "Move the source ROM outside the installed asset folder before importing it.");
    }
    auto bytes = read_exactly(image);
    if (platform::sha256(bytes) != rom_sha256) {
        throw std::runtime_error("This ROM revision is not supported. Expected Sonic Advance "
                                 "Game Boy Advance (USA, En/Ja), CRC32 63F70FD8.");
    }
    fs::create_directories(root / "games");
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    Staging staging{root / "games" / (".import-" + std::to_string(stamp))};
    if (!fs::create_directory(staging.path)) {
        throw std::runtime_error("Could not create import workspace. Try again.");
    }
    staging.owned = true;
    if (progress) {
        progress(1, 3);
    }
    const Rom rom(std::move(bytes));
    const auto assets = Assets::extract(rom);
    if (progress) {
        progress(2, 3);
    }
    assets.save(staging.path);
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
    if (progress) {
        progress(3, 3);
    }
}

} // namespace sonic_advance_gba
