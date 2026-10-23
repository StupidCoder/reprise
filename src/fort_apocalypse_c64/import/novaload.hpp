#pragma once
#include "c64/tape_image.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The tape's fast loader (it names itself "NOVALOAD" on screen). Its pulse
// handler is hidden in the spare bytes of the tape's KERNAL header block and
// runs once per pulse; see novaload.cpp for the recovered rules.
//
// Stream: a pilot of 0 bits ended by one 1 bit, the sync byte $AA, the key
// byte $55, then page records, each a page number, 256 bytes for that page
// and a checksum (page number plus the 256 bytes, modulo 256). Pages may come
// in any order. After the record for page $F0, a page number of $00 ends the
// load.
namespace fort_apocalypse::import {

struct PageRecord {
    std::uint8_t page{};
    std::array<std::uint8_t, 256> bytes{};
};

struct NovaloadStream {
    std::vector<PageRecord> records; // in tape order, all with good checksums
    bool complete{};                 // the end marker was read
    std::size_t end_pulse{};         // index just past the last pulse read
};

// Reads one stream starting at pulse `from`. Throws std::runtime_error when
// the stream is missing, a checksum fails or the tape ends before the end
// marker (the original loader stops in all three cases).
NovaloadStream read_novaload(const c64::TapeImage& tape, std::size_t from);

} // namespace fort_apocalypse::import
