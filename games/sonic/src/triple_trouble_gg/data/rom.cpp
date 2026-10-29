// The supported cartridge.

#include "triple_trouble_gg/data/rom.hpp"
#include "triple_trouble_gg/data/data_regions.hpp"

namespace triple_trouble_gg {

const gg_engine::CartridgeInfo cartridge{
    .pack_name = "triple-trouble-gg",
    .title = "Sonic the Hedgehog - Triple Trouble",
    .short_name = "Triple Trouble",
    .region = "USA, Europe, Brazil",
    .crc32 = "D23A2A93",
    .sha256 = "3e7d3da957f1012cbba02881c8090dceaab8d8abd2ee8bea87b57f8b902ad299",
    .size = 524288,
    // 4: one data region is a byte longer than in packs from BlueStreak 0.14/0.15, which
    // also said 3 (found while connecting Reprise, 2026-10-29).
    .extractor = 4,
    .data_magic = "STTGDATA",
    .tiles_magic = nullptr,
    .data_regions = data_regions,
    .tile_sources = {},
};

} // namespace triple_trouble_gg
