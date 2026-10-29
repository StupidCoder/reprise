#pragma once
#include <cstdint>
#include <span>
#include <vector>

// The decruncher in front of Tower of Babel's program.
//
// The boot block loads a stage-one loader to $60000. It copies its second
// half (from loader +$BE) to $100 and calls it there: a backward LZ unpacker
// followed by a run-length pass, which leave the program at $800
// (.analysis/tower_of_babel_amiga/disk.md, "Boot chain").
//
//   LZ    The packed stream is read from its end towards its start, 32 bits
//         at a time, least significant bit first, with a sentinel bit marking
//         the end of each word. The last word holds the unpacked length; the
//         output grows downwards from (low end + length) to the low end.
//         Literal runs carry a 3-bit count (7 escapes to 2, 8, 12 or 15 more
//         bits); each literal is a 3- to 7-bit index into a byte table that
//         sits in the stage itself. Matches copy 2 to 270 bytes from up to
//         1023 bytes above the write position.
//   RLE   From the LZ output to $800: $A3 escapes, "$A3 0" is a single $A3,
//         "$A3 n b" is n + 3 copies of b.
//
// Both passes work on one model of memory, as the original does, since the
// LZ output overwrites the packed stream while it is being read.
namespace babel::import {

// Unpacks the program from the stage-one loader's bytes (the $CA00 bytes the
// boot block loads to $60000). Returns memory $800 onwards as the program
// sees it at its entry ($7A5E): $18C00 bytes. Throws std::runtime_error when
// the stream does not unpack cleanly.
std::vector<std::uint8_t> unpack_program(std::span<const std::uint8_t> loader);

} // namespace babel::import
