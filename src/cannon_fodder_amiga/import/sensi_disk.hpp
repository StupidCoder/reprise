#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The file system of Cannon Fodder's disks (.analysis/cannon_fodder_amiga/
// disk.md). It is not AmigaDOS: the game's own loader ("Nufload", file
// `fload`) reads it without the operating system.
//
// - The image holds the disk's 512-byte sectors in order, 11 per track.
// - Track 3 holds the directory: a 32-byte header starting "Sensi disk\0"
//   and then 32-byte entries: name (16 bytes, zero-terminated), 10 bytes the
//   loader does not use (leftovers of the tool that wrote the disk), the
//   first sector as (track ^ 1, sector), and the file size (u32).
// - A file is a chain of sectors: 510 bytes of data, then the next sector as
//   (track ^ 1, sector). The last sector holds only the remaining bytes.
//
// The track byte has its lowest bit flipped (side bit inverted). The boot
// block's own copy of the chain walker shows it (it maps '1' to track $30).
namespace fodder::import {

struct SensiEntry {
    std::string name;             // as stored ("FODDERC", "mapm1.map")
    std::uint32_t size{};         // bytes
    unsigned first_track{}, first_sector{};
};

class SensiDisk {
public:
    // Throws std::runtime_error when the image has no Sensible directory.
    explicit SensiDisk(std::span<const std::uint8_t> image);

    static bool has_directory(std::span<const std::uint8_t> image);

    const std::vector<SensiEntry>& entries() const { return entries_; }

    // The entry with this name, compared without regard to case.
    const SensiEntry* find(std::string_view name) const;

    // Follows the entry's sector chain. Returns nothing when the chain leaves
    // the disk, loops, or ends before the file's size is reached.
    std::optional<std::vector<std::uint8_t>> read(const SensiEntry& entry) const;

private:
    std::span<const std::uint8_t> image_;
    std::vector<SensiEntry> entries_;
};

} // namespace fodder::import
