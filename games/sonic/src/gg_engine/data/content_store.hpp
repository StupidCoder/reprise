#pragma once
#include "gg_engine/data/cartridge.hpp"
#include <filesystem>
#include <functional>

// Installation of the user's ROM as a local asset pack (see assets.hpp).
//
// BlueStreak never ships game data. The player imports their own ROM; it is
// identified by SHA-256, decoded into an asset pack in a staging folder,
// validated, and only then moved into place, so a failed import leaves the
// old pack intact.
namespace gg_engine {

using ExtractProgress = std::function<void(unsigned, unsigned)>;

// Location of the installed pack below the app's data directory.
std::filesystem::path pack_path(const CartridgeInfo& info, const std::filesystem::path& root);

// True when a pack written by the current extractor version is installed.
bool pack_installed(const CartridgeInfo& info, const std::filesystem::path& root);

// Loads the pack; throws if it is invalid.
void validate_pack(const CartridgeInfo& info, const std::filesystem::path& pack);

// True if `image` looks like the supported ROM (size and checksum).
bool is_supported_rom(const CartridgeInfo& info, const std::filesystem::path& image);

// Verifies `image`, extracts it and atomically replaces the installed pack.
void import_rom(const CartridgeInfo& info, const std::filesystem::path& image,
                const std::filesystem::path& root, ExtractProgress progress = {});

} // namespace gg_engine
