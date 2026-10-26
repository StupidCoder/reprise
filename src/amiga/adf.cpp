#include "amiga/adf.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace amiga {
namespace {

// Block types (long 0) and secondary types (long 127) of AmigaDOS.
constexpr std::uint32_t t_header = 2;
constexpr std::uint32_t t_data = 8;
constexpr std::uint32_t t_list = 16;
constexpr std::int32_t st_root = 1;
constexpr std::int32_t st_userdir = 2;
constexpr std::int32_t st_file = -3;

constexpr std::size_t longs_per_block = block_size / 4;
constexpr std::size_t table_size = longs_per_block - 56; // 72 hash or data-block slots

std::uint32_t long_at(std::span<const std::uint8_t> block, std::size_t index) {
    return read_be32(block, index * 4);
}
std::int32_t signed_long_at(std::span<const std::uint8_t> block, std::size_t index) {
    return std::int32_t(long_at(block, index));
}
// Fields counted from the end of a block, as the AmigaDOS manuals list them.
std::uint32_t from_end(std::span<const std::uint8_t> block, std::size_t index) {
    return long_at(block, longs_per_block - index);
}

// Header, list and data blocks: the longs add up to zero (long 5 holds the
// balance).
bool sum_ok(std::span<const std::uint8_t> block) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < longs_per_block; ++i) {
        sum += long_at(block, i);
    }
    return sum == 0;
}

// The boot block's sum over 1024 bytes, adding each carry back in.
bool boot_sum_ok(std::span<const std::uint8_t> boot) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < 2 * block_size; i += 4) {
        const std::uint32_t before = sum;
        sum += read_be32(boot, i);
        if (sum < before) {
            ++sum;
        }
    }
    return sum == 0xFFFFFFFF;
}

// The name field: a length byte and up to 30 characters, 80 bytes from the end.
std::string block_name(std::span<const std::uint8_t> block) {
    const std::size_t at = block_size - 80;
    const std::size_t length = std::min<std::size_t>(block[at], 30);
    return std::string(reinterpret_cast<const char*>(block.data() + at + 1), length);
}

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

[[noreturn]] void damaged(const std::string& what) {
    throw std::runtime_error("The disk image is damaged: " + what + ".");
}

} // namespace

bool is_adf(std::span<const std::uint8_t> image) {
    return image.size() == dd_blocks * block_size;
}

Volume::Volume(std::span<const std::uint8_t> image) : image_(image) {
    if (!is_adf(image)) {
        throw std::runtime_error("This is not a double-density Amiga disk image (ADF).");
    }
    if (image[0] != 'D' || image[1] != 'O' || image[2] != 'S' || image[3] > 1) {
        throw std::runtime_error("This Amiga disk has no AmigaDOS file system.");
    }
    if (!boot_sum_ok(image)) {
        damaged("the boot block's checksum is wrong");
    }
    ffs_ = image[3] == 1;
    // The boot block's root pointer is not used by AmigaDOS for floppies; the
    // root is always the middle block.
    const std::uint32_t root = dd_blocks / 2;
    const auto root_block = block(root);
    if (long_at(root_block, 0) != t_header || signed_long_at(root_block, longs_per_block - 1) != st_root ||
        !sum_ok(root_block)) {
        damaged("the root block is not valid");
    }
    name_ = block_name(root_block);
    walk(root, "", 0);
}

std::span<const std::uint8_t> Volume::block(std::uint32_t number) const {
    if (number >= dd_blocks) {
        damaged("a block pointer lies outside the disk");
    }
    return image_.subspan(std::size_t(number) * block_size, block_size);
}

void Volume::walk(std::uint32_t directory_block, const std::string& prefix, int depth) {
    if (depth > 16) {
        damaged("the directories nest too deeply");
    }
    const auto directory = block(directory_block);
    for (std::size_t slot = 0; slot < table_size; ++slot) {
        std::uint32_t next = long_at(directory, 6 + slot);
        int chain = 0;
        while (next != 0) {
            if (++chain > int(dd_blocks)) {
                damaged("a hash chain loops");
            }
            const auto header = block(next);
            if (long_at(header, 0) != t_header || long_at(header, 1) != next || !sum_ok(header)) {
                damaged("header block " + std::to_string(next) + " is not valid");
            }
            const std::string path = prefix + block_name(header);
            const auto type = signed_long_at(header, longs_per_block - 1);
            if (type == st_userdir) {
                entries_.push_back({.path = path, .directory = true, .header_block = next});
                walk(next, path + "/", depth + 1);
            } else if (type == st_file) {
                entries_.push_back(file_entry(next, path));
            } else {
                damaged("block " + std::to_string(next) + " has an unknown entry type");
            }
            next = from_end(header, 4); // hash chain
        }
    }
}

// A file's data blocks: each header or extension block lists up to 72 of them
// in a table filled from its end; long 2 says how many are used.
DiskEntry Volume::file_entry(std::uint32_t header_number, const std::string& path) const {
    DiskEntry entry{.path = path, .header_block = header_number};
    const auto header = block(header_number);
    entry.size = from_end(header, 47);
    std::uint32_t list = header_number;
    for (int extensions = 0; list != 0; ++extensions) {
        const auto table = block(list);
        if (extensions > 0 && (long_at(table, 0) != t_list || !sum_ok(table))) {
            damaged("an extension block of " + path + " is not valid");
        }
        const std::uint32_t used = long_at(table, 2);
        if (used > table_size) {
            damaged("a block table of " + path + " is too long");
        }
        for (std::uint32_t i = 0; i < used; ++i) {
            entry.data_blocks.push_back(long_at(table, 6 + table_size - 1 - i));
        }
        list = from_end(table, 2); // extension
        if (extensions > int(dd_blocks)) {
            damaged("the extension chain of " + path + " loops");
        }
    }
    return entry;
}

const DiskEntry* Volume::find(std::string_view path) const {
    for (const auto& entry : entries_) {
        if (same_name(entry.path, path)) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<std::uint8_t> Volume::read(std::string_view path) const {
    const auto* entry = find(path);
    if (entry == nullptr || entry->directory) {
        throw std::runtime_error("The disk has no file " + std::string(path) + ".");
    }
    return read(*entry);
}

// OFS data blocks carry a 24-byte header (type, owner, sequence number, bytes
// used, next block, checksum) before 488 bytes of data; FFS blocks are all
// data.
std::vector<std::uint8_t> Volume::read(const DiskEntry& entry) const {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(entry.size);
    std::uint32_t sequence = 1;
    for (const auto number : entry.data_blocks) {
        const auto data = block(number);
        std::size_t remaining = entry.size - bytes.size();
        if (ffs_) {
            const auto take = std::min(remaining, block_size);
            bytes.insert(bytes.end(), data.begin(), data.begin() + std::ptrdiff_t(take));
            continue;
        }
        if (long_at(data, 0) != t_data || long_at(data, 1) != entry.header_block || long_at(data, 2) != sequence ||
            !sum_ok(data)) {
            damaged("data block " + std::to_string(number) + " of " + entry.path + " is not valid");
        }
        const std::size_t used = long_at(data, 3);
        if (used > block_size - 24 || used > remaining) {
            damaged("data block " + std::to_string(number) + " of " + entry.path + " has a wrong size");
        }
        bytes.insert(bytes.end(), data.begin() + 24, data.begin() + 24 + std::ptrdiff_t(used));
        ++sequence;
    }
    if (bytes.size() != entry.size) {
        damaged(entry.path + " is shorter than its header says");
    }
    return bytes;
}

} // namespace amiga
