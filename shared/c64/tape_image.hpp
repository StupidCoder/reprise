#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// TAP files: a raw recording of the C64 cassette read line. Each entry is the
// time between two falling edges of the signal ("pulse"), in CPU cycles, or a
// stretch of silence. Every C64 tape loader, ROM or custom, measures these
// pulse lengths, so the decoders for each loader start from this list.
namespace c64 {

struct TapePulse {
    std::uint32_t cycles{}; // duration in C64 clock cycles
    bool pause{};           // silence (a gap between blocks), not a pulse
};

struct TapeImage {
    int version{};
    std::vector<TapePulse> pulses;
};

// True when the bytes start with a TAP signature.
bool is_tap(std::span<const std::uint8_t> file);

// Parses a TAP file, versions 0 and 1. Throws std::runtime_error with a
// message for the user when the file is not a usable TAP image.
//
// Layout: 12-byte signature "C64-TAPE-RAW", version byte, 3 reserved bytes,
// 32-bit little-endian data length, then one byte per pulse: n > 0 is a pulse
// of n * 8 cycles. A zero byte is an overflow (version 0: a pulse longer than
// 255 * 8 cycles; version 1: followed by its 24-bit little-endian length in
// cycles).
TapeImage read_tap(std::span<const std::uint8_t> file);

} // namespace c64
