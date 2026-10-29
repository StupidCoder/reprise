#pragma once
#include "sonic2_gg/game/types.hpp"

// Two-digit BCD arithmetic as the Z80 does it (ADC/SBC followed by DAA),
// for valid BCD operands.
namespace sonic2_gg {

// ADC A,(HL); DAA
inline u8 add_bcd(u8 a, u8 b, bool& carry) {
    unsigned lo = (a & 0x0F) + (b & 0x0F) + (carry ? 1u : 0u);
    unsigned r = unsigned(a) + b + (carry ? 1u : 0u);
    bool half = lo > 0x0F;
    unsigned correction = 0;
    carry = false;
    if (half || (r & 0x0F) > 9) {
        correction |= 0x06;
    }
    if (r > 0x99) {
        correction |= 0x60;
        carry = true;
    }
    return u8(r + correction);
}

// SBC A,(HL); DAA
inline u8 sub_bcd(u8 a, u8 b, bool& borrow) {
    const unsigned in = borrow ? 1u : 0u;
    unsigned r = unsigned(a) - b - in;
    if ((a & 0x0F) < (b & 0x0F) + in) {
        r -= 0x06;
    }
    borrow = unsigned(a) < unsigned(b) + in;
    if (borrow) {
        r -= 0x60;
    }
    return u8(r);
}

} // namespace sonic2_gg
