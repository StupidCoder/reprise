#pragma once
#include "gg_engine/data/assets.hpp"
#include "sonic2_gg/data/rom.hpp"
#include <filesystem>

// Everything the game needs from the cartridge (gg_engine/data/assets.hpp).
namespace sonic2_gg {

using gg_engine::Assets;
using gg_engine::Table;

// The installed asset pack in `directory`.
inline Assets load_assets(const std::filesystem::path& directory) {
    return Assets::load(cartridge, directory);
}

// CPU clocks the original's decoder (1B00) takes for the tile source at
// bank:address, from its first instruction to its RET.
unsigned decode_clocks(const Assets& assets, unsigned bank, unsigned address, bool mirrored);

} // namespace sonic2_gg
