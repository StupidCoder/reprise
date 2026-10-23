#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace platform {

// SHA-256 of a byte buffer as 64 lowercase hex digits. Used to identify
// imported game content exactly and to checksum quick saves.
std::string sha256(std::span<const std::uint8_t> input);

} // namespace platform
