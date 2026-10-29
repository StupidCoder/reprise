#pragma once
#include <cstdint>

// Motorola Fast Floating Point, as the Amiga's mathffp.library computes it.
// `wings` does its flight arithmetic with these (through wrappers at
// $111C9C…$111CEC); the results must match bit for bit, so this is a model of
// the library's rounding, fitted to the library itself (.analysis/
// wings_of_fury_amiga/formats.md "FFP"): 2,000 additions, multiplications and
// conversions and 1,144 divisions run in the reference machine, all equal for
// exponents $30..$50 (the game's values). Overflow and underflow are not
// modelled beyond saturating/flushing (never reached by the game).
//
// Format: mantissa in bits 31-8 (bit 31 set unless the number is zero), sign
// in bit 7, exponent in bits 6-0 (excess 64); value = mantissa / 2^24 x
// 2^(exponent - 64). Zero is all bits clear.
namespace wings::game::ffp {

using Ffp = std::uint32_t;

Ffp from_int(std::int32_t);   // SPFlt: rounds to nearest
std::int32_t to_int(Ffp);     // SPFix: truncates toward zero
Ffp add(Ffp, Ffp);            // SPAdd
Ffp multiply(Ffp, Ffp);       // SPMul: rounds to nearest
Ffp divide(Ffp a, Ffp b);     // SPDiv as called: a / b, rounds to nearest
Ffp negate(Ffp);              // SPNeg

} // namespace wings::game::ffp
