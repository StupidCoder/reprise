// "@MRA" resource archive (data/multi_platformer/odx.ra): the game's object definitions packed
// into one file. Layout, derived from the file:
//   0x00  "@MRA", u32 entry count
//   0x08  entries, 24 bytes each: u32 hash, u32 name offset, u32 name length, u32 data offset,
//         u32 size, u32 zero. The hash is FNV-1a of the lower-case path ("data/.../x.odx").
//   then  the paths (packed, not terminated), then the files (16-byte aligned, stored as is).
#pragma once

#include "data/Bytes.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beacon::data {

class ResourceArchive {
public:
    struct Entry {
        std::uint32_t hash = 0;
        std::string path;   // as stored, e.g. "data/multi_platformer/bg/odx/bamboo.odx"
        std::uint32_t offset = 0, size = 0;
    };

    bool open(const std::filesystem::path& file, std::string* error = nullptr);

    const std::vector<Entry>& entries() const { return entries_; }
    // File contents by stored path (case-insensitive); nullopt if absent.
    std::optional<ByteView> find(std::string_view path) const;
    ByteView contents(const Entry& e) const { return ByteView(bytes_).subspan(e.offset, e.size); }

    static std::uint32_t pathHash(std::string_view path);

private:
    Bytes bytes_;
    std::vector<Entry> entries_;
};

}   // namespace data
