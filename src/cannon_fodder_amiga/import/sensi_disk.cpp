#include "cannon_fodder_amiga/import/sensi_disk.hpp"
#include "amiga/adf.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace fodder::import {
namespace {

constexpr std::size_t sector_size = 512;
constexpr unsigned sectors_per_track = 11;
constexpr unsigned tracks = 160;
constexpr std::size_t track_size = sector_size * sectors_per_track;
constexpr std::size_t directory_at = 3 * track_size;
constexpr std::size_t entry_size = 32;
constexpr std::string_view signature{"Sensi disk\0", 11};

bool same_name(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

bool SensiDisk::has_directory(std::span<const std::uint8_t> image) {
    return amiga::is_adf(image) &&
           std::equal(signature.begin(), signature.end(), image.begin() + std::ptrdiff_t(directory_at));
}

SensiDisk::SensiDisk(std::span<const std::uint8_t> image) : image_(image) {
    if (!has_directory(image)) {
        throw std::runtime_error("This disk has no Sensible Software directory.");
    }
    for (std::size_t at = directory_at + entry_size; at + entry_size <= directory_at + track_size; at += entry_size) {
        const auto entry = image.subspan(at, entry_size);
        if (entry[0] == 0) {
            continue;
        }
        SensiEntry e;
        for (std::size_t i = 0; i < 16 && entry[i] != 0; ++i) {
            e.name += char(entry[i]);
        }
        e.first_track = entry[26] ^ 1u;
        e.first_sector = entry[27];
        e.size = amiga::read_be32(entry, 28);
        entries_.push_back(std::move(e));
    }
}

const SensiEntry* SensiDisk::find(std::string_view name) const {
    for (const auto& entry : entries_) {
        if (same_name(entry.name, name)) {
            return &entry;
        }
    }
    return nullptr;
}

std::optional<std::vector<std::uint8_t>> SensiDisk::read(const SensiEntry& entry) const {
    std::vector<std::uint8_t> out;
    out.reserve(entry.size);
    std::vector<bool> visited(tracks * sectors_per_track);
    unsigned track = entry.first_track, sector = entry.first_sector;
    while (out.size() < entry.size) {
        if (track >= tracks || sector >= sectors_per_track) {
            return std::nullopt;
        }
        const auto index = track * sectors_per_track + sector;
        if (visited[index]) {
            return std::nullopt;
        }
        visited[index] = true;
        const auto data = image_.subspan(index * sector_size, sector_size);
        const auto take = std::min<std::size_t>(510, entry.size - out.size());
        out.insert(out.end(), data.begin(), data.begin() + std::ptrdiff_t(take));
        track = data[510] ^ 1u;
        sector = data[511];
    }
    return out;
}

} // namespace fodder::import
