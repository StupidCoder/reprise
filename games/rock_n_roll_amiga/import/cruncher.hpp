#pragma once
#include <cstdint>
#include <span>
#include <vector>

// The cruncher format of Rock 'n Roll's files.
//
// Every file on the disk except one sample bank, and the program `Main`
// behind its self-unpacking stub, is stored crunched
// (games/rock_n_roll_amiga/game.md "Disk and boot"). The layout was
// read from the unpacking routine at the start of Main's code hunk
// (hunk +$0..+$E6):
//
//   +0  packed length (bytes of bit stream that follow the header)
//   +4  unpacked length
//   +8  checksum: the XOR of all bit-stream longs must equal it
//   +12 the bit stream
//
// The stream is read backwards from its last long, one bit at a time from
// the low end of each long, and the output is written backwards from its end.
// A long's highest set bit marks where its bits stop (the first long of the
// stream is taken as it is; every later one is shifted in behind a marker).
// Commands:
//   0 0 nnn            nnn+1 literal bytes (1-8)
//   0 1 oooooooo       copy 2 bytes from 8-bit offset o
//   1 00 o×9           copy 3 bytes from 9-bit offset o
//   1 01 o×10          copy 4 bytes from 10-bit offset o
//   1 10 nnnnnnnn o×12 copy n+1 bytes from 12-bit offset o
//   1 11 nnnnnnnn      n+9 literal bytes
// A copy takes each byte from `offset` bytes above the byte being written.
// Fields are read most significant bit first.
namespace rocknroll::import {

// True when the bytes look like a crunched file (the header's lengths fit).
bool is_crunched(std::span<const std::uint8_t> file);

// Unpacks a crunched file; throws std::runtime_error when the stream is
// malformed or the checksum fails.
std::vector<std::uint8_t> uncrunch(std::span<const std::uint8_t> file);

} // namespace rocknroll::import
