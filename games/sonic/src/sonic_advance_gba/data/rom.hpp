#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// The supported cartridge image and small file helpers.
//
// Addresses in this module are written the way the ARM code uses them: the
// cartridge is mapped at 0x08000000, so the byte at file offset N has the
// address 0x08000000 + N.
namespace sonic_advance_gba {

using Bytes = std::vector<std::uint8_t>;
Bytes read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

// Sonic Advance (USA) (En,Ja), Game Boy Advance, 8 MiB, CRC32 63F70FD8.
inline constexpr const char* rom_sha256 =
    "afc29e3325baf68a1822e88b25fcb59c8bdc29b8cdc48fa21c56defbd60ca020";
inline constexpr std::size_t rom_size = 8u << 20;
inline constexpr std::uint32_t cartridge_base = 0x08000000;

class Rom {
public:
    // Throws unless `bytes` is the supported revision.
    explicit Rom(Bytes bytes);
    static Rom load(const std::filesystem::path& path);

    std::uint8_t at(std::size_t offset) const { return bytes_.at(offset); }

    const Bytes& bytes() const { return bytes_; }

private:
    Bytes bytes_;
};

} // namespace sonic_advance_gba
