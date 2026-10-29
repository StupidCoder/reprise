#pragma once
#include <cstdint>

// Cartridge addresses of the data tables the game reads (see assets.hpp).
namespace sonic_advance_gba::table {

inline constexpr std::uint32_t backgrounds = 0x084A6C40; // 03001B20: background assets
inline constexpr std::uint32_t animations = 0x080D868C;  // 03002034: sprite animation tables
inline constexpr std::uint32_t sine =
    0x0809B3F0; // 0x500 halfwords: sine (index + 0x100: cosine), 1.14

} // namespace sonic_advance_gba::table
