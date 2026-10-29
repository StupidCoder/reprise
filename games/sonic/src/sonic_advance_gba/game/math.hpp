#pragma once
#include "sonic_advance_gba/data/assets.hpp"
#include "sonic_advance_gba/game/tables.hpp"
#include "sonic_advance_gba/game/types.hpp"
#include <cstdint>

// Arithmetic the game takes from the cartridge's tables and the BIOS.
namespace sonic_advance_gba {

// The sine table (0809B3F0): 1024 steps per turn, then a quarter turn more
// so that sine(i + 0x100) is the cosine. Values in 1.14 fixed point.
inline s16 sine(const Assets& a, unsigned index) {
    return a.s16(table::sine + 2 * index);
}

// BIOS call 6 (080997E4): signed division, rounding toward zero. The BIOS
// never returns from a division by zero.
inline s32 divide(s32 n, s32 d) {
    if (d == 0) {
        throw Unrecovered("division by zero (the original hangs)");
    }
    if (n == INT32_MIN && d == -1) {
        return n;
    }
    return n / d;
}

// 080997E8: the remainder of the same division.
inline s32 remainder(s32 n, s32 d) {
    if (d == 0) {
        throw Unrecovered("division by zero (the original hangs)");
    }
    if (d == -1) {
        return 0;
    }
    return n % d;
}

// BIOS call 8 (08099818): the square root, rounded down (GBATEK).
inline u16 square_root(u32 n) {
    u32 r = 0;
    for (u32 bit = 1u << 30; bit != 0; bit >>= 2) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
    }
    return u16(r);
}

// 08099880: the compiler's division routine (rounds toward zero; a
// division by zero gives the dividend).
inline s32 quotient(s32 n, s32 d) {
    if (d == 0) {
        return n;
    }
    if (n == INT32_MIN && d == -1) {
        return n;
    }
    return n / d;
}

// 0800338C: the angle (0-3FF) of the vector (dx, dy) by octants: the
// smaller over the larger component, 128 steps per octant, from the
// octant table 0809BDF0. -1 for the null vector.
inline s16 octant_angle(const Assets& a, s16 dy, s16 dx) {
    if (dy == 0 && dx == 0) {
        return -1;
    }
    unsigned octant = 0;
    if (dy < 1) {
        dy = s16(-dy);
        octant = 4;
    }
    if (dx < 1) {
        dx = s16(-dx);
        octant += 2;
    }
    s32 small = dx, big = dy;
    if (dy < dx) {
        ++octant;
        small = dy;
        big = dx;
    }
    s16 step = s16(quotient((small << 23) >> 16, big));
    const u8 start = a.u8(0x0809BDF0 + octant);
    if (start & 1) {
        step = s16(0x80 - step);
    }
    return s16((step + start * 0x80) & 0x3FF);
}

} // namespace sonic_advance_gba
