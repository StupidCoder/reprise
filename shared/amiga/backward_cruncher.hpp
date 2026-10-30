#pragma once
#include <cstdint>
#include <span>
#include <vector>

// A backward bit-stream cruncher used by several Amiga games.
//
// The stream is a run of big-endian longs consumed from the last towards the
// first, one bit at a time from the low end of each long. The first long
// taken carries its own end marker (its highest set bit); every later one is
// shifted in behind a marker bit. Output is written from its end down.
// Fields are read most significant bit first. Commands:
//
//   0 0 nnn            nnn+1 literal bytes (1-8)
//   0 1 o×8            copy 2 bytes from offset o
//   1 00 o×9           copy 3 bytes from offset o
//   1 01 o×10          copy 4 bytes from offset o
//   1 10 nnnnnnnn o×12 copy n+1 bytes from offset o
//   1 11 nnnnnnnn      n+9 literal bytes
//
// A copy takes each byte from `offset` bytes above the byte being written.
// The XOR of the checksum and every stream long is zero for an intact file.
//
// Games frame the stream differently (lengths in a header or a trailer);
// they unpack it with this one routine. Recovered from Beach Volley's
// decruncher (volley.code $31B90-$31C6C; games/beach_volley_amiga/game.md
// "Crunched files"); Rock 'n Roll's files use the same stream.
namespace amiga {

// Unpacks `stream` (a multiple of 4 bytes) into `unpacked_length` bytes.
// Throws std::runtime_error when the stream is malformed, ends early, refers
// outside its output or fails `checksum`.
std::vector<std::uint8_t> uncrunch_backward(std::span<const std::uint8_t> stream, std::size_t unpacked_length,
                                            std::uint32_t checksum);

} // namespace amiga
