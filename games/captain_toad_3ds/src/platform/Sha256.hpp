#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace captain_toad::platform {

// SHA-256 of a byte buffer as 64 lowercase hex digits. The importer uses it to verify every ROM
// file it reads against the supported release (no platform crypto library is involved).
std::string sha256(std::span<const std::uint8_t> input);

}   // namespace captain_toad::platform
