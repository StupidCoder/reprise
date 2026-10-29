#pragma once
#include "gg_engine/data/cartridge.hpp"

// The supported cartridge (see gg_engine/data/cartridge.hpp for how addresses
// are written).
namespace sonic2_gg {

using gg_engine::ByteSource;
using gg_engine::Bytes;
using gg_engine::decode_tiles;
using gg_engine::read_file;
using gg_engine::Rom;
using gg_engine::write_file;

// Sonic The Hedgehog 2 (World), Game Gear, 512 KiB, CRC32 95A18EC7.
extern const gg_engine::CartridgeInfo cartridge;

} // namespace sonic2_gg
