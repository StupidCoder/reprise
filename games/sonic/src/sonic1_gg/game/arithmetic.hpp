#pragma once

// Fixed-width arithmetic of the original Z80 code.
//
// Positions are stored like the cartridge stores them: 24-bit unsigned values
// in 1/256 pixel units (16 bits of whole pixels, 8 bits of fraction). Velocities
// use the same 1/256 pixel units but are signed. Many original routines work on
// single bytes or 16-bit words and let them wrap, and several behaviours depend
// on that wraparound, so these helpers spell the truncation out explicitly.
namespace sonic1_gg {

// Whole-pixel part of a 24-bit position (0..65535).
inline int pixel(int fixed) {
    return (fixed & 0xffffff) / 256;
}

// Replaces the whole-pixel part of a position, keeping its 1/256 fraction.
// Only the low 16 bits of `whole` are used, like a 16-bit register store.
inline void set_pixel(int& fixed, int whole) {
    fixed = (whole * 256 + (fixed & 255)) & 0xffffff;
}

// Wraps to the 24-bit range of a position.
inline int wrap24(int value) {
    return value & 0xffffff;
}

// Low 16 bits, as an unsigned Z80 register pair.
inline int word(int value) {
    return value & 0xffff;
}

// Interprets 0..255 as a signed byte.
inline int signed8(int value) {
    return value < 128 ? value : value - 256;
}

// Interprets the low 16 bits as a signed word.
inline int signed16(int value) {
    value &= 0xffff;
    return (value & 0x8000) ? value - 0x10000 : value;
}

// Interprets the low 24 bits as a signed 24-bit value (velocity arithmetic).
inline int signed24(int value) {
    value &= 0xffffff;
    return (value & 0x800000) ? value - 0x1000000 : value;
}

// Moves `value` one step toward `target` (the camera's gap and arena easing).
inline int approach(int value, int target) {
    return value + (value < target) - (value > target);
}

} // namespace sonic1_gg
