#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace picarat::import {

// Incremental SHA-256, for inputs read in pieces (the 3DS cartridge images
// are hashed while they stream from disk).
class Sha256 {
public:
    void update(std::span<const std::uint8_t> data);
    std::string finish(); // 64 lowercase hex digits; the object is spent

private:
    void block(const std::uint8_t* data);
    std::array<std::uint32_t, 8> h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> pending_{};
    std::size_t pendingSize_ = 0;
    std::uint64_t length_ = 0;
};

// SHA-256 of a byte buffer as 64 lowercase hex digits. Used to identify
// supported ROM revisions exactly and to hash the native asset pack.
std::string sha256(std::span<const std::uint8_t> input);

} // namespace picarat::import
