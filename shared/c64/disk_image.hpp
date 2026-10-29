#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Commodore 1541 disks. A G64 file is a raw recording of each track's bit
// stream as the drive's read head delivers it; the importer decodes the GCR
// sectors from it the way the drive's DOS does, and keeps the raw bits for
// loaders that read the disk below the DOS (copy protection).
namespace c64 {

// One track's recorded bit stream (a G64 track slot).
struct RawTrack {
    std::vector<std::uint8_t> bytes; // bit 7 of byte 0 comes first; the track is circular
    int speed_zone{};                // 0-3, as recorded

    std::size_t bit_count() const { return bytes.size() * 8; }
    bool bit(std::size_t index) const {
        index %= bit_count();
        return bytes[index / 8] >> (7 - index % 8) & 1;
    }
    // Eight bits from `index` on (wrapping round the track), first bit highest.
    std::uint8_t byte_at(std::size_t index) const;
    // The bit position just after the first occurrence of `pattern` (raw,
    // not GCR-decoded bytes) anywhere on the track, if any.
    std::optional<std::size_t> find(std::span<const std::uint8_t> pattern) const;
};

using Sector = std::array<std::uint8_t, 256>;

struct DiskImage {
    // Raw tracks by half-track number: 2 is track 1, 3 track 1.5, ...
    std::map<int, RawTrack> half_tracks;
    // Sectors with good header and data checksums, by (track, sector).
    std::map<std::pair<int, int>, Sector> sectors;

    const RawTrack* track(int number) const {
        const auto it = half_tracks.find(number * 2);
        return it == half_tracks.end() ? nullptr : &it->second;
    }
    const Sector* sector(int track, int sector) const {
        const auto it = sectors.find({track, sector});
        return it == sectors.end() ? nullptr : &it->second;
    }
};

// True when the bytes start with the G64 signature.
bool is_g64(std::span<const std::uint8_t> file);

// Parses a G64 file (version 0) and decodes every standard sector on the
// whole tracks. Throws std::runtime_error with a message for the user when
// the file is not a usable G64 image.
//
// Layout: "GCR-1541", version 0, the number of half-track slots, the
// largest track size (16-bit), then a 32-bit offset per slot (0 = empty) and
// a 32-bit speed zone per slot; each track is a 16-bit length and its bytes.
//
// Sectors (1541 GCR): after a sync (10 or more 1 bits) every 4 bytes are
// written as 5 bytes, each nibble as a 5-bit code. A header block is $08,
// checksum, sector, track, ID 2, ID 1 (checksum = XOR of the next four); the
// data block after the next sync is $07, 256 bytes and their XOR.
DiskImage read_g64(std::span<const std::uint8_t> file);

// One entry of a CBM DOS directory (track 18).
struct DirectoryEntry {
    int type{};          // low three bits: 0 DEL, 1 SEQ, 2 PRG, 3 USR, 4 REL
    int track{}, sector{};
    std::string name;    // as stored, up to the first shifted space ($A0)
};

// The directory chain from 18/1. A chain that links back to a sector read
// before ends the walk (protected disks use this).
std::vector<DirectoryEntry> read_directory(const DiskImage&);

// A file's bytes, following its sector chain (byte 0/1 of each sector: next
// track and sector; track 0 marks the last sector, byte 1 then its last used
// byte). Throws std::runtime_error when a sector is missing or the chain
// loops.
std::vector<std::uint8_t> read_file(const DiskImage&, int track, int sector);

} // namespace c64
