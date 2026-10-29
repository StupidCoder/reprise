#include "tower_of_babel_amiga/import/decruncher.hpp"
#include <stdexcept>

namespace babel::import {
namespace {

// Where things are, as the stage's code names them (stage at $100).
constexpr std::size_t stage_in_loader = 0xBE;   // $60068: copied from $600BE
constexpr std::size_t stage_length = 0xC7A0;    // $60070: $31E8 + 1 longs
constexpr std::uint32_t stage_base = 0x100;
constexpr std::uint32_t packed_start = 0x390;   // $102: LEA $390(PC),A1
constexpr std::uint32_t packed_size_at = 0x114; // $11E: ADDA.L $114(PC),A0
constexpr std::uint32_t literal_table = 0x290;  // $130: LEA $290(PC),A4
constexpr std::uint32_t lz_low_end = 0x4FBE;    // $12A: LEA $4FBE,A1
constexpr std::uint32_t rle_end = 0x19000;      // $264: LEA $19000,A2
constexpr std::uint8_t rle_escape = 0xA3;       // $25C: MOVEQ #$A3,D7
constexpr std::size_t program_length = 0x18C00;
constexpr std::size_t memory_size = 0x20000;

void fail() { throw std::runtime_error("The game's program does not unpack cleanly; the disk may be damaged."); }

class Memory {
public:
    Memory() : bytes_(memory_size) {}
    std::uint8_t& at(std::uint32_t address) {
        if (address >= bytes_.size()) fail();
        return bytes_[address];
    }
    std::uint32_t long_at(std::uint32_t address) {
        return std::uint32_t(at(address)) << 24 | std::uint32_t(at(address + 1)) << 16 |
               std::uint32_t(at(address + 2)) << 8 | at(address + 3);
    }

private:
    std::vector<std::uint8_t> bytes_;
};

// The stream's bit reader ($1F6/$210): words are taken from the end towards
// the start; bits come out least significant first; when a word runs dry the
// next one is loaded with a sentinel bit shifted in at the top.
class BitReader {
public:
    BitReader(Memory& memory, std::uint32_t end) : memory_(memory), next_(end) {}

    // The next whole word towards the start (the length word, then the first
    // bit word, which is used without a sentinel).
    std::uint32_t take() {
        if (next_ < 4) fail();
        next_ -= 4;
        return memory_.long_at(next_);
    }
    void prime() { word_ = take(); }
    int bit() {
        int b = int(word_ & 1);
        word_ >>= 1;
        if (word_ == 0) { // the sentinel went out: load the next word
            word_ = take();
            b = int(word_ & 1);
            word_ = (word_ >> 1) | 0x80000000u;
        }
        return b;
    }
    // n bits, the first one read becoming the most significant.
    unsigned bits(int n) {
        unsigned value = 0;
        while (n-- > 0) value = (value << 1) | unsigned(bit());
        return value;
    }

private:
    Memory& memory_;
    std::uint32_t next_;
    std::uint32_t word_{};
};

} // namespace

std::vector<std::uint8_t> unpack_program(std::span<const std::uint8_t> loader) {
    if (loader.size() < stage_in_loader + stage_length) fail();
    Memory memory;
    for (std::size_t i = 0; i < stage_length; ++i) memory.at(stage_base + std::uint32_t(i)) = loader[stage_in_loader + i];

    // The stream is used in place (the stage's own copy-up branch is not taken
    // for this program, since $390 lies below the target $800).
    BitReader reader(memory, packed_start + memory.long_at(packed_size_at));
    std::uint32_t out = lz_low_end + reader.take(); // writes go downwards from here
    reader.prime();

    const auto put = [&](std::uint8_t byte) {
        if (out <= lz_low_end) fail();
        memory.at(--out) = byte;
    };
    while (true) {
        // Literal run ($13A-$1C8).
        unsigned count = reader.bits(3);
        if (count == 7) {
            if (!reader.bit()) {
                count = reader.bits(2) + 7;
            } else if ((count = reader.bits(8)) != 0) {
                count += 10;
            } else if ((count = reader.bits(12)) != 0) {
                count += 0x109;
            } else {
                count = reader.bits(15) + 0x110B;
            }
        }
        for (unsigned i = 0; i < count; ++i) {
            unsigned base, width;
            if (!reader.bit()) { base = 8; width = 6; }
            else if (!reader.bit()) { base = 0; width = 3; }
            else if (!reader.bit()) { base = 0x48; width = 6; }
            else { base = 0x88; width = 7; }
            put(memory.at(literal_table + reader.bits(int(width)) + base));
        }
        if (out <= lz_low_end) break;

        // Match ($1D2-$24E): length classes, then the distance.
        unsigned length, width;
        if (!reader.bit()) {
            if (!reader.bit()) { length = 3; width = 9; }
            else { length = 2; width = 10; }
        } else {
            width = 10;
            switch (reader.bits(2)) {
            case 0: length = 4; break;
            case 1: length = 5 + reader.bits(1); break;
            case 2: length = 7 + reader.bits(3); break;
            default: length = 15 + reader.bits(8); break;
            }
        }
        const unsigned distance = reader.bits(int(width));
        for (unsigned i = 0; i < length; ++i) put(memory.at(out - 1 + distance));
        if (out <= lz_low_end) break;
    }

    // Run-length pass ($25C-$288): its output is the program from $800 on.
    std::vector<std::uint8_t> program;
    program.reserve(program_length);
    for (std::uint32_t at = lz_low_end; at < rle_end;) {
        std::uint8_t byte = memory.at(at++);
        if (byte == rle_escape) {
            const unsigned repeat = memory.at(at++);
            if (repeat != 0) {
                byte = memory.at(at++);
                program.insert(program.end(), repeat + 2, byte);
            }
        }
        program.push_back(byte);
    }
    if (program.size() != program_length) fail();
    return program;
}

} // namespace babel::import
