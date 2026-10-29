#pragma once
#include <filesystem>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Installation of the user's ROM as a local asset pack (see assets.hpp).
//
// BlueStreak never ships game data. The player imports their own ROM; it is
// identified by SHA-256, decoded into an asset pack in a staging folder,
// validated, and only then moved into place, so a failed import leaves the
// old pack intact.
namespace sonic_advance_gba {

using ExtractProgress = std::function<void(unsigned, unsigned)>;

// Location of the installed pack below the app's data directory.
std::filesystem::path pack_path(const std::filesystem::path& root);

// True when a pack written by the current extractor version is installed.
bool pack_installed(const std::filesystem::path& root);
// True when the pack in `pack` was written by the current extractor version.
bool pack_current(const std::filesystem::path& pack);

// Loads the pack; throws if it is invalid.
void validate_pack(const std::filesystem::path& pack);

// True if `image` looks like the supported ROM (size and checksum).
bool is_supported_rom(const std::filesystem::path& image);

// The pack's files (its manifest included) made from the bytes of the supported ROM, for an
// application that installs them itself. Throws std::runtime_error for other bytes.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>> extract_pack(
    std::vector<std::uint8_t> rom);

// Verifies `image`, extracts it and atomically replaces the installed pack.
void import_rom(const std::filesystem::path& image, const std::filesystem::path& root,
                ExtractProgress progress = {});

} // namespace sonic_advance_gba
