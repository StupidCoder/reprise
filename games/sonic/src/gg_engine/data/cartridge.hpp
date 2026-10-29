#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

// The supported cartridge images and small file helpers.
//
// Addresses are written the way the Z80 code uses them: a 16 KiB bank
// number plus a CPU address in the slot the bank is mapped to (0000-3FFF for
// bank 0, 4000-7FFF for the code bank in slot 1 and 8000-BFFF for the
// switchable data/code banks in slot 2). Only the low 14 bits of the address
// select the byte within the bank.
namespace gg_engine {

using Bytes = std::vector<std::uint8_t>;
Bytes read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

// A byte range of the cartridge the original never executed as an
// instruction: (bank, offset in bank, length). Each game lists its own
// (data/data_regions.hpp, generated from coverage of the original).
struct DataRegion {
    std::uint8_t bank;
    std::uint16_t offset;
    std::uint16_t length;
};

// A compressed tile block decoded at import rather than when first used.
struct TileSource {
    std::uint8_t bank;
    std::uint16_t address;
    bool mirrored;
};

// What identifies a supported cartridge and the asset pack made from it.
struct CartridgeInfo {
    const char* pack_name;  // folder and manifest name, e.g. "sonic2-gg"
    const char* title;      // "Sonic the Hedgehog 2"
    const char* short_name; // "Sonic 2"
    const char* region;     // "World"
    const char* crc32;      // "95A18EC7"
    const char* sha256;
    std::size_t size;
    unsigned extractor;      // pack format version; older packs are offered for re-import
    const char* data_magic;  // 8 characters at the start of data.bin
    const char* tiles_magic; // tiles.bin's (blocks decoded at import), or nullptr
    std::span<const DataRegion> data_regions;
    std::span<const TileSource> tile_sources; // decoded at import into tiles.bin
};

class Rom {
public:
    // Throws unless `bytes` is the cartridge `info` describes.
    Rom(const CartridgeInfo& info, Bytes bytes);

    std::uint8_t byte(unsigned bank, unsigned address) const;
    std::uint16_t word(unsigned bank, unsigned address) const;

    std::uint8_t at(std::size_t offset) const { return bytes_.at(offset); }

private:
    Bytes bytes_;
};

// A reader of cartridge bytes by bank and CPU address (the ROM at import, the
// asset pack's data afterwards).
using ByteSource = std::function<std::uint8_t(unsigned bank, unsigned address)>;

// The tile compression both games use for background and sprite patterns
// (Sonic 2: routine 1B00, Triple Trouble: 2DC7). Returns the decoded tiles
// (32 bytes each). With `mirrored` every byte is passed through the
// bit-reversal table at 0100, which mirrors the tiles horizontally.
Bytes decode_tiles(const ByteSource& rom, unsigned bank, unsigned address, bool mirrored);

} // namespace gg_engine
