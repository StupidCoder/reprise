#include "gg_engine/data/content_store.hpp"
#include "platform/sha256.hpp"
#include "gg_engine/data/assets.hpp"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

namespace gg_engine {
namespace fs = std::filesystem;

namespace {
// The manifest names the pack format, the game, the ROM and the extractor
// version; a pack from an older extractor is offered for re-import.
std::string manifest(const CartridgeInfo& info) {
    return std::string("BLUESTREAK_PACK 1\n") + info.pack_name + "\n" + info.sha256 +
           "\nextractor " + std::to_string(info.extractor) + "\n";
}
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

Bytes read_exactly(const CartridgeInfo& info, const fs::path& image) {
    Bytes bytes(info.size);
    std::ifstream input(image, std::ios::binary);
    input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (input.gcount() != std::streamsize(bytes.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("The ROM changed while being read. Try importing a stable copy.");
    }
    return bytes;
}
} // namespace

fs::path pack_path(const CartridgeInfo& info, const fs::path& root) {
    return root / "games" / info.pack_name;
}

bool pack_installed(const CartridgeInfo& info, const fs::path& root) {
    return pack_current(info, pack_path(info, root));
}

bool pack_current(const CartridgeInfo& info, const fs::path& pack) {
    try {
        return file_equals(pack / manifest_file, manifest(info));
    } catch (const std::exception&) {
        return false;
    }
}

void validate_pack(const CartridgeInfo& info, const fs::path& p) {
    Assets::load(info, p);
}

bool is_supported_rom(const CartridgeInfo& info, const fs::path& image) {
    std::error_code ec;
    if (!fs::is_regular_file(image, ec) || fs::file_size(image, ec) != info.size) {
        return false;
    }
    try {
        return platform::sha256(read_exactly(info, image)) == info.sha256;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::pair<std::string, Bytes>> extract_pack(const CartridgeInfo& info, Bytes bytes) {
    if (bytes.size() != info.size || platform::sha256(bytes) != info.sha256) {
        throw std::runtime_error(std::string("This ROM revision is not supported. Expected ") +
                                 info.short_name + " Game Gear (" + info.region + "), CRC32 " +
                                 info.crc32 + ".");
    }
    const Rom rom(info, std::move(bytes));
    auto files = Assets::extract(info, rom).files();
    const auto text = manifest(info);
    files.emplace_back(manifest_file, Bytes(text.begin(), text.end()));
    return files;
}

void import_rom(const CartridgeInfo& info, const fs::path& image, const fs::path& root,
                ExtractProgress progress) {
    // Reject large, truncated and different-revision inputs before decoding.
    if (!fs::is_regular_file(image) || fs::file_size(image) != info.size) {
        throw std::runtime_error(std::string("Choose the supported 512 KiB ") + info.short_name +
                                 " Game Gear ROM. ZIP files and other games are not supported.");
    }
    const auto relative = fs::weakly_canonical(image).lexically_relative(
        fs::weakly_canonical(pack_path(info, root)));
    if (!relative.empty() && *relative.begin() != "..") {
        throw std::runtime_error(
            "Move the source ROM outside the installed asset folder before importing it.");
    }
    auto bytes = read_exactly(info, image);
    if (platform::sha256(bytes) != info.sha256) {
        throw std::runtime_error(std::string("This ROM revision is not supported. Expected ") +
                                 info.short_name + " Game Gear (" + info.region + "), CRC32 " +
                                 info.crc32 + ".");
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
    const Rom rom(info, std::move(bytes));
    const auto assets = Assets::extract(info, rom);
    if (progress) {
        progress(2, 3);
    }
    assets.save(staging.path);
    validate_pack(info, staging.path);
    const auto text = manifest(info);
    write_file(staging.path / manifest_file, Bytes(text.begin(), text.end()));
    const auto destination = pack_path(info, root);
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

} // namespace gg_engine
