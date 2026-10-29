#pragma once
#include "gg_engine/data/assets.hpp"
#include "triple_trouble_gg/data/rom.hpp"
#include <filesystem>

// Everything the game needs from the cartridge (gg_engine/data/assets.hpp).
namespace triple_trouble_gg {

using gg_engine::Assets;
using gg_engine::Table;

// The installed asset pack in `directory`.
inline Assets load_assets(const std::filesystem::path& directory) {
    return Assets::load(cartridge, directory);
}

} // namespace triple_trouble_gg
