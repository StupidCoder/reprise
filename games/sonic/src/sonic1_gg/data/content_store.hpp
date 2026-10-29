#pragma once
#include "sonic1_gg/data/asset_pack.hpp"
#include <filesystem>

// Installation of the user's ROM as a local asset pack.
//
// BlueStreak never ships game data. The player imports their own ROM; it is
// identified by SHA-256, decoded into an asset pack in a staging folder, validated,
// and only then moved into place, so a failed import leaves the old pack intact.
namespace sonic1_gg {

// The one supported revision: Sonic The Hedgehog (Japan, USA), CRC32 3E31CB8C.
inline constexpr const char* rom_sha256 =
    "dd605c17c8d24b2db9928beff6d8294fd84184b31c2fa92760ae691db84fd569";

// Location of the installed pack below the app's data directory.
std::filesystem::path pack_path(const std::filesystem::path& root);

// True when a pack written by the current extractor version is installed.
bool pack_installed(const std::filesystem::path& root);
// True when the pack in `pack` was written by the current extractor version.
bool pack_current(const std::filesystem::path& pack);

// Loads every level, frontend and sound file of a pack; throws if any is invalid.
void validate_pack(const std::filesystem::path& pack);

// The pack's files (its manifest included) made from the bytes of the supported ROM, for an
// application that installs them itself. Throws std::runtime_error for other bytes.
std::vector<std::pair<std::string, Bytes>> extract_pack(Bytes rom, ExtractProgress progress = {});

// Verifies `image`, extracts it and atomically replaces the installed pack.
void import_rom(const std::filesystem::path& image, const std::filesystem::path& root,
                ExtractProgress progress = {});

} // namespace sonic1_gg
