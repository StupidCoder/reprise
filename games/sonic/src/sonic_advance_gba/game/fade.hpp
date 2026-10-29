#pragma once
#include "sonic_advance_gba/game/types.hpp"

// Fades of the whole picture to black or white (or from it), done with a
// full-screen window and the colour effect registers (08054370 sets the
// registers for the current level, 08054208 does that and steps the fade).
namespace sonic_advance_gba {

struct VideoState;

// The original's 0xC-byte fade record.
struct ScreenFade {
    s16 delay{};  // +00 frames (in steps of `speed`) before the level moves
    u16 window{}; // +02 0: window 0, else window 1
    u16 kind{};   // +04 fade:: bits
    s16 level{};  // +06 0-2000 (8.8): how far the fade has gone
    s16 speed{};  // +08 added to the level each frame
    u8 blend{};   // +0A which layers take part (blend:: values)
};

namespace fade {
inline constexpr u16 brightness_rises = 0x5; // BLDY follows the level (else 32 - level)
inline constexpr u16 to_white = 0xC;         // brighten (else darken)
} // namespace fade

// 08054370: sets the window and colour effect for the current level.
void apply_fade(VideoState& v, const ScreenFade& f);
// 08054208: the same (with more blend choices) and one step; true when the
// level has reached its end (2000).
bool step_fade(VideoState& v, ScreenFade& f);

} // namespace sonic_advance_gba
