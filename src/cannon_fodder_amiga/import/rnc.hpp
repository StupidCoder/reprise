#pragma once
#include <cstdint>
#include <span>
#include <vector>

// Rob Northen's "ProPack" compression, method 1 ("RNC\1"), which Cannon
// Fodder uses for most of its files. This is a general-purpose packer of the
// time; the format is rebuilt from the packed files themselves and checked
// with both of their CRCs (.analysis/cannon_fodder_amiga/formats.md).
//
// Header (18 bytes, big-endian): "RNC", method, unpacked size, packed size,
// CRC-16 of the unpacked data, CRC-16 of the packed data, leeway, chunk count.
// The packed data is a little-endian bit stream: per chunk three Huffman
// tables (literal run lengths, match distances, match lengths) and a count of
// runs; literal bytes are stored as whole bytes between the bit fields.
namespace fodder::import {

bool is_rnc(std::span<const std::uint8_t> bytes);

// Unpacks an "RNC\1" file. Throws std::runtime_error when the data is damaged
// (bad code, overrun or CRC mismatch).
std::vector<std::uint8_t> unpack_rnc(std::span<const std::uint8_t> packed);

// CRC-16 as the RNC header stores it (reflected polynomial $A001, start 0).
std::uint16_t rnc_crc(std::span<const std::uint8_t> bytes);

} // namespace fodder::import
