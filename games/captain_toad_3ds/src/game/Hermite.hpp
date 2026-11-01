// Recovered Hermite curve segment evaluation (original code 0x258078, file offset 0x158078).
//
// Animation curves in the game's model files are piecewise cubic Hermite splines: each key stores a
// frame, a value and a slope. To evaluate a curve at a frame, the caller finds the two keys around
// the frame and calls this function with:
//   a, slopeA      value and slope at the earlier key
//   b, slopeB      value and slope at the later key
//   offset         frames since the earlier key
//   duration       frames between the two keys (must be positive)
// The arithmetic follows the original instruction by instruction (variable names are the VFP
// registers of the original: s0 = a, s1 = slopeA, s2 = b, s3 = slopeB, s4 = offset,
// s5 = duration). Build with -ffp-contract=off so every multiply and add rounds separately.
#pragma once

inline float recoveredHermite(float a, float slopeA, float b, float slopeB, float offset, float duration) {
    float s6 = offset / duration;   // t, the normalised position in the segment
    float s8 = a - b;
    float s7 = s6 * 2.0f - 3.0f;
    float s2 = s6 - 1.0f;
    float s3 = s6 * slopeB;
    float s5 = s6 * s6;
    s6 = s8 * s7;
    float s1 = s2 * slopeA;
    s2 = offset * s2;
    a = a + s6 * s5;
    s1 = s1 + s3;
    return a + s2 * s1;
}
