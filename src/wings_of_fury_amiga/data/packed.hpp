#pragma once
#include <cstdint>
#include <span>
#include <vector>

// The game's packed files ("Rpck").
//
// The file reader `wings` $10FF16 looks at the first 16 bytes of a file:
//   "Rpck", u32 unpacked size, packed bytes   unpacked into a buffer of that
//                                             size by $10FEE0
//   "Pckd"                                    refused (no file on the disk)
//   anything else                             read as it is
// The packed bytes are runs: a control byte n (signed); n < 0 copies the next
// -n bytes, n >= 0 repeats the next byte n + 1 times. (Like ByteRun1 with the
// meaning of the sign swapped, and without a no-op code.) The runs end with
// the file; some files decode one byte more than the stated size, which the
// game's buffer drops (it is allocated with that size).
namespace wings::data {

// True when the file starts with "Rpck".
bool is_run_packed(std::span<const std::uint8_t> file);

// The file's contents: unpacked when it starts with "Rpck", else as it is.
// Throws std::runtime_error when the runs do not fill the stated size.
std::vector<std::uint8_t> unpack_file(std::span<const std::uint8_t> file);

} // namespace wings::data
