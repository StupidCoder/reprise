#pragma once
#include "c64/tape_image.hpp"
#include <cstdint>
#include <string>
#include <vector>

// The C64's built-in (KERNAL ROM) tape format, used by almost every tape at
// least for its first file: the boot program that then starts a game's own
// fast loader.
//
// Signal: three pulse lengths, short (about 376 cycles), medium (about 528)
// and long (about 696). A byte is a long+medium marker, eight data bits
// (least significant first; 0 = short+medium, 1 = medium+short) and an odd
// parity bit. Long+short ends a block. Each block starts with a countdown of
// nine bytes ($89..$81 for the first recording, $09..$01 for the repeat) and
// ends with an XOR checksum of its payload. Every block is recorded twice.
//
// A file is a 192-byte header block (type, start and end address, 16-byte
// name, and 171 spare bytes) followed by a data block.
namespace c64 {

struct KernalFile {
    enum class Type : std::uint8_t { basic = 1, absolute = 3, data = 4 };

    Type type{};
    std::uint16_t start{}, end{}; // load range from the header (end exclusive)
    std::string name;             // 16 PETSCII characters, trailing spaces removed
    // The header's 171 bytes after the name. The ROM loads the whole header to
    // the cassette buffer, so loaders often hide code here.
    std::vector<std::uint8_t> header_tail;
    std::vector<std::uint8_t> data;
    // Where the file's last recorded block ends, as an index into the pulses.
    // A game's own loader usually starts after this.
    std::size_t end_pulse{};

    // The address the data loads to with LOAD"",1: a BASIC program is
    // relocated to the start of BASIC, others load where the header says.
    std::uint16_t load_address() const { return type == Type::basic ? 0x0801 : start; }
};

// Decodes every KERNAL-format file on the tape, in tape order. For each block
// the first recording is used when its checksum is right, otherwise the
// repeat recording; a file whose header or data has no good recording throws
// std::runtime_error.
std::vector<KernalFile> read_kernal_files(const TapeImage& tape);

} // namespace c64
