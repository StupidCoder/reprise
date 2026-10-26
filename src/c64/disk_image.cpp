#include "c64/disk_image.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace c64 {
namespace {

// The 1541's 4-to-5 bit group code: the code for each nibble, and back.
constexpr std::uint8_t gcr_code[16] = {0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
                                       0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15};

int gcr_nibble(unsigned code) {
    for (int n = 0; n < 16; ++n) {
        if (gcr_code[n] == code) {
            return n;
        }
    }
    return -1;
}

std::uint32_t word32(std::span<const std::uint8_t> f, std::size_t at) {
    return std::uint32_t(f[at]) | std::uint32_t(f[at + 1]) << 8 | std::uint32_t(f[at + 2]) << 16 |
           std::uint32_t(f[at + 3]) << 24;
}

// Decodes `count` bytes of GCR starting at bit `from`; nullopt when a code is
// not a valid group code.
std::optional<std::vector<std::uint8_t>> decode_gcr(const RawTrack& track, std::size_t from,
                                                    std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count * 2; ++i) {
        unsigned code = 0;
        for (int b = 0; b < 5; ++b) {
            code = code << 1 | unsigned(track.bit(from + i * 5 + std::size_t(b)));
        }
        const int nibble = gcr_nibble(code);
        if (nibble < 0) {
            return std::nullopt;
        }
        out[i / 2] = std::uint8_t(out[i / 2] << 4 | nibble);
    }
    return out;
}

// Bit positions just after each sync mark on the track (once round).
std::vector<std::size_t> syncs(const RawTrack& track) {
    std::vector<std::size_t> marks;
    const std::size_t n = track.bit_count();
    std::size_t ones = 0;
    // Start where a zero bit precedes, so a sync across the seam counts once.
    std::size_t start = 0;
    while (start < n && track.bit(start)) {
        ++start;
    }
    if (start == n) {
        return marks;
    }
    for (std::size_t i = start + 1; i <= start + n; ++i) {
        if (track.bit(i)) {
            ++ones;
        } else {
            if (ones >= 10) {
                marks.push_back(i % n);
            }
            ones = 0;
        }
    }
    return marks;
}

void decode_sectors(int track_number, const RawTrack& track, DiskImage& disk) {
    const auto marks = syncs(track);
    for (std::size_t m = 0; m + 1 <= marks.size(); ++m) {
        const auto header = decode_gcr(track, marks[m], 6);
        if (!header || (*header)[0] != 0x08) {
            continue;
        }
        const auto& h = *header;
        if ((h[2] ^ h[3] ^ h[4] ^ h[5]) != h[1] || h[3] != track_number) {
            continue;
        }
        const auto data = decode_gcr(track, marks[(m + 1) % marks.size()], 258);
        if (!data || (*data)[0] != 0x07) {
            continue;
        }
        std::uint8_t sum = 0;
        for (int i = 1; i <= 256; ++i) {
            sum ^= (*data)[std::size_t(i)];
        }
        if (sum != (*data)[257]) {
            continue;
        }
        Sector sector;
        std::copy(data->begin() + 1, data->begin() + 257, sector.begin());
        disk.sectors.try_emplace({track_number, h[2]}, sector);
    }
}

} // namespace

std::uint8_t RawTrack::byte_at(std::size_t index) const {
    std::uint8_t value = 0;
    for (int b = 0; b < 8; ++b) {
        value = std::uint8_t(value << 1 | int(bit(index + std::size_t(b))));
    }
    return value;
}

std::optional<std::size_t> RawTrack::find(std::span<const std::uint8_t> pattern) const {
    const std::size_t n = bit_count(), length = pattern.size() * 8;
    for (std::size_t at = 0; at < n; ++at) {
        bool match = true;
        for (std::size_t i = 0; i < length && match; ++i) {
            match = bit(at + i) == bool(pattern[i / 8] >> (7 - i % 8) & 1);
        }
        if (match) {
            return (at + length) % n;
        }
    }
    return std::nullopt;
}

bool is_g64(std::span<const std::uint8_t> file) {
    constexpr std::string_view signature = "GCR-1541";
    return file.size() >= signature.size() &&
           std::equal(signature.begin(), signature.end(), file.begin());
}

DiskImage read_g64(std::span<const std::uint8_t> file) {
    if (!is_g64(file) || file.size() < 12) {
        throw std::runtime_error("This is not a G64 disk image.");
    }
    if (file[8] != 0) {
        throw std::runtime_error("This G64 disk image has an unsupported version.");
    }
    const std::size_t slots = file[9];
    if (slots == 0 || slots > 84 || file.size() < 12 + 8 * slots) {
        throw std::runtime_error("This G64 disk image is damaged (bad track table).");
    }
    DiskImage disk;
    for (std::size_t slot = 0; slot < slots; ++slot) {
        const std::size_t offset = word32(file, 12 + 4 * slot);
        if (offset == 0) {
            continue;
        }
        if (offset + 2 > file.size()) {
            throw std::runtime_error("This G64 disk image is damaged (track outside the file).");
        }
        const std::size_t length = file[offset] | std::size_t(file[offset + 1]) << 8;
        if (length == 0 || offset + 2 + length > file.size()) {
            throw std::runtime_error("This G64 disk image is damaged (track outside the file).");
        }
        RawTrack track;
        track.bytes.assign(file.begin() + std::ptrdiff_t(offset + 2),
                           file.begin() + std::ptrdiff_t(offset + 2 + length));
        track.speed_zone = int(word32(file, 12 + 4 * slots + 4 * slot) & 3);
        disk.half_tracks[int(slot) + 2] = std::move(track);
    }
    for (const auto& [half, track] : disk.half_tracks) {
        if (half % 2 == 0) {
            decode_sectors(half / 2, track, disk);
        }
    }
    return disk;
}

std::vector<DirectoryEntry> read_directory(const DiskImage& disk) {
    std::vector<DirectoryEntry> entries;
    std::set<std::pair<int, int>> seen;
    int track = 18, sector = 1;
    while (track != 0 && seen.insert({track, sector}).second) {
        const auto* block = disk.sector(track, sector);
        if (!block) {
            break;
        }
        for (int e = 0; e < 8; ++e) {
            const auto* entry = block->data() + e * 32;
            if (entry[2] == 0) {
                continue;
            }
            DirectoryEntry d{entry[2] & 7, entry[3], entry[4], {}};
            for (int i = 5; i < 21 && entry[i] != 0xA0; ++i) {
                d.name.push_back(char(entry[i]));
            }
            entries.push_back(std::move(d));
        }
        track = (*block)[0];
        sector = (*block)[1];
    }
    return entries;
}

std::vector<std::uint8_t> read_file(const DiskImage& disk, int track, int sector) {
    std::vector<std::uint8_t> bytes;
    std::set<std::pair<int, int>> seen;
    while (true) {
        if (!seen.insert({track, sector}).second) {
            throw std::runtime_error("A file's sector chain on the disk loops.");
        }
        const auto* block = disk.sector(track, sector);
        if (!block) {
            throw std::runtime_error("A sector of a file is missing or damaged on the disk.");
        }
        if ((*block)[0] == 0) {
            const int last = (*block)[1];
            if (last >= 2) {
                bytes.insert(bytes.end(), block->begin() + 2, block->begin() + last + 1);
            }
            return bytes;
        }
        bytes.insert(bytes.end(), block->begin() + 2, block->end());
        track = (*block)[0];
        sector = (*block)[1];
    }
}

} // namespace c64
