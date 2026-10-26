#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// AmigaDOS load files ("hunk" files), the format LoadSeg reads.
//
// A load file starts with HUNK_HEADER ($3F3): resident library names (none in
// practice), the number of hunks, the first and last hunk number and each
// hunk's size in longs (the top two bits are the memory type: chip, fast).
// Then each hunk follows as a CODE ($3E9), DATA ($3EA) or BSS ($3EB) block
// with optional RELOC32 ($3EC), SYMBOL ($3F0) and DEBUG ($3F1) blocks, ended
// by HUNK_END ($3F2). RELOC32 lists, per target hunk, the offsets of longs in
// this hunk that hold an offset into the target hunk; LoadSeg adds the
// target's load address to each.
namespace amiga {

enum class HunkKind { code, data, bss };

struct Relocation {
    int target_hunk{};
    std::vector<std::uint32_t> offsets;
};

struct Symbol {
    std::string name;
    std::uint32_t offset{};
};

struct Hunk {
    HunkKind kind{};
    std::uint32_t memory_flags{};      // bit 30 chip, bit 31 fast, from the header
    std::uint32_t size{};              // bytes reserved (from the header)
    std::vector<std::uint8_t> bytes;   // stored contents (empty for BSS); zero-padded to size
    std::vector<Relocation> relocations;
    std::vector<Symbol> symbols;
};

struct LoadFile {
    std::vector<Hunk> hunks;
};

// True when the bytes start with HUNK_HEADER.
bool is_load_file(std::span<const std::uint8_t> bytes);

// Parses a load file; throws std::runtime_error when it is malformed.
LoadFile read_load_file(std::span<const std::uint8_t> bytes);

// The hunks laid out one after the other from `base` (each at a long-aligned
// address, `gap` bytes apart), with every RELOC32 applied, as LoadSeg would
// leave them in memory (without its segment-list links). `addresses`
// receives each hunk's address.
std::vector<std::uint8_t> relocate(const LoadFile&, std::uint32_t base, std::vector<std::uint32_t>& addresses,
                                   std::uint32_t gap = 8);

// A load file laid out as LoadSeg would leave it, for reading data that
// holds pointers: every pointer in it is an address in this image.
class LoadedImage {
public:
    LoadedImage() = default;
    LoadedImage(const LoadFile&, std::uint32_t base, std::uint32_t gap = 8);

    std::uint32_t base() const { return base_; }
    std::uint32_t end() const { return base_ + std::uint32_t(bytes_.size()); }
    // Address of hunk n's contents.
    std::uint32_t hunk(std::size_t n) const { return hunks_.at(n); }
    // The first hunk with contents (what a game finds by skipping empty
    // segments of the segment list).
    std::uint32_t first_filled_hunk() const;
    bool contains(std::uint32_t address, std::uint32_t length = 1) const {
        return address >= base_ && address - base_ + length <= bytes_.size();
    }

    // Big-endian reads; throw std::runtime_error outside the image.
    std::uint8_t u8(std::uint32_t address) const;
    std::int8_t s8(std::uint32_t address) const { return std::int8_t(u8(address)); }
    std::uint16_t u16(std::uint32_t address) const;
    std::int16_t s16(std::uint32_t address) const { return std::int16_t(u16(address)); }
    std::uint32_t u32(std::uint32_t address) const;
    std::int32_t s32(std::uint32_t address) const { return std::int32_t(u32(address)); }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint32_t> hunks_;
    std::vector<std::uint32_t> sizes_;
    std::uint32_t base_{};
};

} // namespace amiga
