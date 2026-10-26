#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// ByteRun1, the run-length code of IFF ILBM bodies, which many Amiga games
// also use for their own files. Each control byte n is followed by n + 1
// literal bytes (n = 0..127) or by one byte to repeat 1 - n times
// (n = -1..-127); n = -128 is skipped.
namespace amiga {

// Unpacks until the input ends or `limit` bytes are written. `consumed`
// receives the number of input bytes used. Throws std::runtime_error when a
// run is cut short by the end of the input.
std::vector<std::uint8_t> unpack_byte_run1(std::span<const std::uint8_t> packed, std::size_t limit,
                                           std::size_t* consumed = nullptr);

} // namespace amiga
