#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Amiga floppy images (ADF) and the AmigaDOS file system on them.
//
// An ADF is the disk's logical blocks in order: 80 cylinders x 2 sides x 11
// sectors of 512 bytes (1760 blocks, 901,120 bytes) for a double-density disk.
// Nothing in the file says where a block came from; block n is at n x 512.
//
// The reader understands the original file system (OFS, "DOS\0") and the fast
// one (FFS, "DOS\1"); it checks every block checksum AmigaDOS checks and throws
// std::runtime_error with a message for the user when the disk is damaged.
namespace amiga {

constexpr std::size_t block_size = 512;
constexpr std::size_t dd_blocks = 1760;

// A file or directory in the volume, with its full path ("c/zzz").
struct DiskEntry {
    std::string path;
    bool directory{};
    std::uint32_t header_block{};   // the file's or directory's header block
    std::uint32_t size{};           // bytes (files only)
    std::vector<std::uint32_t> data_blocks; // in file order (files only)
};

class Volume {
public:
    // Parses the boot block and root, and walks every directory.
    explicit Volume(std::span<const std::uint8_t> image);

    const std::string& name() const { return name_; }
    bool fast_file_system() const { return ffs_; }
    // The boot block's code area (bytes 12-1023), for identifying custom boots.
    std::span<const std::uint8_t> boot_block() const { return image_.subspan(0, 2 * block_size); }
    // Every entry, directories before their contents, in hash-table order.
    const std::vector<DiskEntry>& entries() const { return entries_; }

    // The entry with this path (compared without regard to case, as AmigaDOS
    // does), or nullptr.
    const DiskEntry* find(std::string_view path) const;
    // A file's bytes; throws std::runtime_error when the file is missing.
    std::vector<std::uint8_t> read(std::string_view path) const;
    std::vector<std::uint8_t> read(const DiskEntry&) const;

    std::span<const std::uint8_t> block(std::uint32_t number) const;

private:
    void walk(std::uint32_t directory_block, const std::string& prefix, int depth);
    DiskEntry file_entry(std::uint32_t header, const std::string& path) const;

    std::span<const std::uint8_t> image_;
    std::string name_;
    bool ffs_{};
    std::vector<DiskEntry> entries_;
};

// True when the image has the size of a double-density ADF.
bool is_adf(std::span<const std::uint8_t> image);

// Big-endian helpers, used by every Amiga format.
inline std::uint32_t read_be32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t(bytes[at]) << 24 | std::uint32_t(bytes[at + 1]) << 16 |
           std::uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
}
inline std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint16_t(bytes[at] << 8 | bytes[at + 1]);
}

} // namespace amiga
