#pragma once
#include "platform/image.hpp"

// A worn VHS tape played on an old CRT, over a game's finished picture. The
// picture comes back twice the size (room for scanlines and the tape's
// sub-pixel wobble), with:
// - colour carried as on tape: luma sharpened with a little ringing, chroma
//   at low bandwidth (smeared and late), so colour edges bleed;
// - the tape's wobble: a per-line jitter, a wavy tracking band at the
//   bottom, the head-switching noise in its last lines, the odd slip of the
//   whole picture;
// - dropouts (short white streaks), grain, a slow rolling bar, scanlines and
//   a soft vignette;
// - at the higher levels, tears (a band yanked sideways with static), the
//   colour fading out for moments, and the vertical hold letting go.
// Level 0 is off (the picture as it is); 1-4 are light, medium, heavy and
// broken. The look follows the frame number, so the same frame always looks
// the same.
namespace ui {

constexpr int vhs_levels = 5;

platform::Image vhs(const platform::Image& picture, long frame, int level);

} // namespace ui
