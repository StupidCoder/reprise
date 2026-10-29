#pragma once
#include "sonic_advance_gba/data/rom.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <vector>

// Everything the game needs from the cartridge, extracted once at import.
//
// Data (graphics, tile maps, animation scripts, sprite frames, palettes,
// level layouts, songs and every table) keeps its cartridge address, so the
// reconstructed routines find it with the same numbers as the ARM code. The
// pack never contains program code (code_regions.hpp), and the game never
// reads the ROM after import.
namespace sonic_advance_gba {

class Assets {
public:
    // Copies the data bytes of the verified cartridge image.
    static Assets extract(const Rom& rom);
    void save(const std::filesystem::path& directory) const;
    static Assets load(const std::filesystem::path& directory);

    // Little-endian values at a cartridge address. Throw if the address is
    // not data: the code asked for something the reconstruction does not
    // know about.
    std::uint8_t u8(std::uint32_t address) const;

    std::uint16_t u16(std::uint32_t address) const {
        return std::uint16_t(u8(address) | (u8(address + 1) << 8));
    }

    std::uint32_t u32(std::uint32_t address) const {
        return std::uint32_t(u16(address)) | (std::uint32_t(u16(address + 2)) << 16);
    }

    std::int8_t s8(std::uint32_t address) const { return std::int8_t(u8(address)); }

    std::int16_t s16(std::uint32_t address) const { return std::int16_t(u16(address)); }

    std::int32_t s32(std::uint32_t address) const { return std::int32_t(u32(address)); }

    Bytes bytes(std::uint32_t address, std::size_t count) const;

    // True if `address` is a cartridge address holding data.
    bool is_data(std::uint32_t address) const;

private:
    Bytes data_;              // the cartridge image with code bytes zeroed
    std::vector<bool> known_; // which bytes are data
};

} // namespace sonic_advance_gba
