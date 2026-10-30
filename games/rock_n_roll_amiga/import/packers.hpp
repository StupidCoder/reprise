#pragma once
#include <cstdint>
#include <span>
#include <vector>

// The three packers of the 1991 compilation disk, besides the game's own
// cruncher (cruncher.hpp). Each was read from its unpacking code on the disk
// (games/rock_n_roll_amiga/game.md "Compilation disk"); all three write their
// output backwards from its end.
namespace rocknroll::import {

// The boot program's packer (header marked "FUCK", unpacker $50020-$50164):
// a Huffman-coded byte stream followed by two run-length passes.
//
// `header` is the 36-byte header followed by the code tree (word nodes: a
// value below $400 is the offset of the next node pair, above it a byte;
// the root is in the middle). `stream` must include the long in front of
// the packed data, which the reader fetches ahead at the very end.
//
//   +$00 unpacked length       +$0C Huffman byte count
//   +$04 length after pass 1   +$10 packed length
//   +$08,+$09 pass-2 escapes   +$14 tree size (the root at half of it)
//   +$0A pass-2 fill byte      +$18 load address, +$1C "FUCK"
//   +$0B,+$20..+$23 pass-1 escapes
std::vector<std::uint8_t> unpack_boot(std::span<const std::uint8_t> header, std::span<const std::uint8_t> stream);

// The track loader's packer ($60024-$60158): one LZ stream read backwards
// (bits from longs, most significant first, each long ended by a marker
// bit), its last long the unpacked length.
std::vector<std::uint8_t> unpack_loader(std::span<const std::uint8_t> stream);

// The band intro's DefJam cruncher ($7F000-$7F184 once copied): an LZ stream
// as above whose literals are coded through a table of 264 bytes, then a
// run-length pass (escape $95, a count and a byte: count + 3 copies).
std::vector<std::uint8_t> unpack_defjam(std::span<const std::uint8_t> stream, std::span<const std::uint8_t> literals);

} // namespace rocknroll::import
