#pragma once
#include "gg_engine/data/cartridge.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>

// Everything a game needs from the cartridge, extracted once at import.
//
// Data tables (object scripts, sprite frames, level layouts, collision
// profiles, palettes, music) keep their original bank and address, so the
// reconstructed routines read them with the same numbers as the Z80 code.
// Compressed tile graphics are looked up by the address of the compressed
// block: decoded at import for the blocks the cartridge info lists (kept in
// tiles.bin), otherwise from the data bytes when first used. The pack never
// contains program code, and the game never reads the ROM after import.
namespace gg_engine {

// A table or compressed block in the cartridge: its bank and its address as
// the code sees it (bank 0: 0000-3FFF, bank 1: 4000-7FFF, banked data:
// 8000-BFFF). Each game's rom_tables.hpp names the ones it reads.
struct Table {
    std::uint8_t bank;
    std::uint16_t address;
};

// The table `offset` bytes further in.
constexpr Table operator+(Table t, unsigned offset) {
    return {t.bank, std::uint16_t(t.address + offset)};
}

class Assets {
public:
    // Copies the data bytes of the verified cartridge image (and decodes the
    // listed tile blocks).
    static Assets extract(const CartridgeInfo& info, const Rom& rom);
    void save(const std::filesystem::path& directory) const;
    static Assets load(const CartridgeInfo& info, const std::filesystem::path& directory);

    // A byte of a data table. Throws if the address is not data: the code
    // asked for something the reconstruction does not know about.
    std::uint8_t byte(unsigned bank, unsigned address) const;

    std::uint16_t word(unsigned bank, unsigned address) const {
        return std::uint16_t(byte(bank, address) | (byte(bank, address + 1) << 8));
    }

    Bytes bytes(unsigned bank, unsigned address, std::size_t count) const;

    // The same for a named table, `offset` bytes in.
    std::uint8_t byte(Table t, unsigned offset = 0) const {
        return byte(t.bank, t.address + offset);
    }

    std::uint16_t word(Table t, unsigned offset = 0) const {
        return word(t.bank, t.address + offset);
    }

    Bytes bytes(Table t, std::size_t count, unsigned offset = 0) const {
        return bytes(t.bank, t.address + offset, count);
    }

    // Decoded tiles of the compressed block at bank:address (decode_tiles).
    const Bytes& tiles(unsigned bank, unsigned address, bool mirrored = false) const;

    const Bytes& tiles(Table t, bool mirrored = false) const {
        return tiles(t.bank, t.address, mirrored);
    }

private:
    explicit Assets(const CartridgeInfo& info) : info_(&info) {}

    const CartridgeInfo* info_;
    std::array<Bytes, 32> banks_;             // data bytes; code bytes are zero
    std::array<std::vector<bool>, 32> known_; // which bytes are data
    // Decoded blocks; key: bank << 17 | mirrored << 16 | address.
    mutable std::map<std::uint32_t, Bytes> tiles_;
};

} // namespace gg_engine
