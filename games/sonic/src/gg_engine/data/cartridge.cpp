// The cartridge image and the tile decompressor.

#include "gg_engine/data/cartridge.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace gg_engine {

Bytes read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot read " + path.string());
    }
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.string());
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    if (!file) {
        throw std::runtime_error("Write failed: " + path.string());
    }
}

Rom::Rom(const CartridgeInfo& info, Bytes bytes) : bytes_(std::move(bytes)) {
    // Every address in a game module refers to this exact revision.
    if (bytes_.size() != info.size || platform::sha256(bytes_) != info.sha256) {
        throw std::runtime_error(std::string("Expected the 512 KiB ") + info.title +
                                 " Game Gear ROM (" + info.region + "), CRC32 " + info.crc32);
    }
}

std::uint8_t Rom::byte(unsigned bank, unsigned address) const {
    return bytes_.at(std::size_t(bank) * 0x4000 + (address & 0x3FFF));
}

std::uint16_t Rom::word(unsigned bank, unsigned address) const {
    return std::uint16_t(byte(bank, address) | (byte(bank, address + 1) << 8));
}

// Sonic 2 1B00-1C36, Triple Trouble 2DE2-2F29. A block starts with a word at
// +2 counting 32-byte tiles and a word at +4 giving the offset of a stream of
// 2-bit tile codes (four per byte, lowest bits first). The tile bytes follow
// from +6:
//   0  an empty tile (copied from a zeroed buffer: S2 D320, TT DC06)
//   1  32 literal bytes
//   2  a 32-bit mask (lowest bit first); a set bit takes the next literal
//      byte, a clear bit gives 0
//   3  as 2, then each of the first 14 bytes of both 16-byte halves is XORed
//      into the byte two places on (S2 1BA9, TT 2E9C; in increasing order)
// The tile count is decremented before testing, so a count of 0 would mean
// 65536 tiles; the cartridges never use that.
Bytes decode_tiles(const ByteSource& rom, unsigned bank, unsigned address, bool mirrored) {
    auto word = [&](unsigned a) {
        return unsigned(rom(bank, a) | (rom(bank, a + 1) << 8));
    };
    const unsigned count = word(address + 2);
    unsigned codes = (address + word(address + 4)) & 0xFFFF;
    unsigned data = address + 6;
    if (count == 0 || count > 512) {
        throw std::runtime_error("Tile block has an implausible size");
    }
    Bytes out;
    out.reserve(std::size_t(count) * 32);
    unsigned code_index = 0; // S2 D340, TT DC26
    for (unsigned tile = 0; tile < count; ++tile) {
        // Advance to the next code byte after four codes (S2 1BDA, TT 2ECD).
        if (code_index == 4) {
            ++codes;
            code_index = 0;
        }
        const unsigned code = (rom(bank, codes) >> (2 * code_index)) & 3;
        ++code_index;
        std::array<std::uint8_t, 32> t{};
        if (code == 1) {
            for (auto& b : t) {
                b = rom(bank, data++);
            }
        } else if (code >= 2) {
            // The mask bytes are E, D, C, B of a 32-bit value rotated right
            // one bit per output byte (S2 1B78, TT 2E6B).
            std::uint32_t mask = rom(bank, data) | (rom(bank, data + 1) << 8) |
                                 (rom(bank, data + 2) << 16) |
                                 (std::uint32_t(rom(bank, data + 3)) << 24);
            data += 4;
            for (auto& b : t) {
                b = (mask & 1) ? rom(bank, data++) : 0;
                mask >>= 1;
            }
            if (code == 3) {
                for (int i = 0; i < 14; i += 2) {
                    t[std::size_t(i + 2)] ^= t[std::size_t(i)];
                    t[std::size_t(i + 3)] ^= t[std::size_t(i + 1)];
                    t[std::size_t(i + 18)] ^= t[std::size_t(i + 16)];
                    t[std::size_t(i + 19)] ^= t[std::size_t(i + 17)];
                }
            }
        }
        for (auto b : t) {
            out.push_back(mirrored ? rom(0, 0x0100 + b) : b);
        }
    }
    return out;
}

} // namespace gg_engine
