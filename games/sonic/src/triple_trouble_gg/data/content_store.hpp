#pragma once
#include "gg_engine/data/content_store.hpp"
#include "triple_trouble_gg/data/rom.hpp"

// Installation of the user's ROM as a local asset pack
// (gg_engine/data/content_store.hpp) for this game's cartridge.
namespace triple_trouble_gg {

using gg_engine::ExtractProgress;

inline std::filesystem::path pack_path(const std::filesystem::path& root) {
    return gg_engine::pack_path(cartridge, root);
}

inline bool pack_installed(const std::filesystem::path& root) {
    return gg_engine::pack_installed(cartridge, root);
}

inline void validate_pack(const std::filesystem::path& pack) {
    gg_engine::validate_pack(cartridge, pack);
}

inline bool is_supported_rom(const std::filesystem::path& image) {
    return gg_engine::is_supported_rom(cartridge, image);
}

inline void import_rom(const std::filesystem::path& image, const std::filesystem::path& root,
                       ExtractProgress progress = {}) {
    gg_engine::import_rom(cartridge, image, root, std::move(progress));
}

} // namespace triple_trouble_gg
