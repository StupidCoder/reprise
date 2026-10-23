#pragma once
#include <cstdint>

// Decimal-mode arithmetic: the game keeps its score, bonus, fuel and lives
// as packed BCD bytes and adds and subtracts them in the 6502's decimal mode.
namespace fort_apocalypse::game {

inline std::uint8_t bcd_add(std::uint8_t a, std::uint8_t b, bool& carry) {
    int lo = (a & 15) + (b & 15) + (carry ? 1 : 0);
    int hi = (a >> 4) + (b >> 4);
    if (lo > 9) {
        lo -= 10;
        ++hi;
    }
    carry = hi > 9;
    if (carry) {
        hi -= 10;
    }
    return std::uint8_t(hi << 4 | lo);
}

inline std::uint8_t bcd_sub(std::uint8_t a, std::uint8_t b, bool& borrow) {
    int lo = (a & 15) - (b & 15) - (borrow ? 1 : 0);
    int hi = (a >> 4) - (b >> 4);
    if (lo < 0) {
        lo += 10;
        --hi;
    }
    borrow = hi < 0;
    if (borrow) {
        hi += 10;
    }
    return std::uint8_t(hi << 4 | lo);
}

// The distance the original uses for "how near" tests: a signed difference,
// with negative values flipped by EOR #$FE (which gives 1 for -1, 0 for -2,
// 3 for -3, 2 for -4 ...).
inline std::uint8_t near_distance(std::uint8_t difference) {
    return difference & 0x80 ? std::uint8_t(difference ^ 0xFE) : difference;
}

} // namespace fort_apocalypse::game
