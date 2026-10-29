#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace sonic_advance_gba {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

// A part of the original that the reconstruction has not recovered yet was
// reached. The game stops there instead of guessing.
struct Unrecovered : std::runtime_error {
    explicit Unrecovered(const std::string& what)
        : std::runtime_error("Not reconstructed yet: " + what) {}
};

} // namespace sonic_advance_gba
