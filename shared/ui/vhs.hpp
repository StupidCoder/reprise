#pragma once
#include "platform/host.hpp"
#include "platform/image.hpp"
#include <cstdint>

// A worn VHS tape played on an old CRT, over a game's finished picture. The
// picture is sampled into an aspect-adaptive virtual video signal, with
// 480 output lines (240 picture lines and scanline gaps). This is a
// stylised CRT/VHS model, independent of game and window resolution, with:
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
constexpr int vhs_signal_width = 360, vhs_signal_height = 240;
constexpr int vhs_video_width = vhs_signal_width * 2, vhs_video_height = vhs_signal_height * 2;

// 720 samples across a 4:3 display, more for wide views; vertical timing stays fixed.
int vhs_raster_width(int width, int height, double pixel_aspect = 1.0);
platform::Image vhs(const platform::Image& picture, long frame, int level, double pixel_aspect = 1.0);

// The tape's state for one whole frame of the fixed virtual signal: what does not change
// from line to line. vhs() works from it, and so does the host's GPU version of the look
// (ui/shaders/vhs.frag), so both show the same tape.
struct VhsFrame {
    float wear{};          // master amount (0: off)
    float dropouts{};      // dropout streaks per frame
    int chroma_radius{};   // chroma smear, in virtual signal samples either side
    float chroma_delay{};  // how late the chroma arrives, in virtual signal samples
    std::uint32_t frame{}; // the frame number, for the per-line noise
    float time{};          // the same as a float, for the moving waves
    float slip{};          // the whole picture's sideways slip
    float roll{};          // the vertical hold letting go (virtual signal lines)
    float saturation{}, brightness{};
    float tear_centre{-1}, tear_half{}, tear_shift{}; // a tear (centre < 0: none)
    int head_lines{};      // head-switching noise at the bottom
    float track_top{};     // where the tracking band starts
    float bar_at{};        // the slow rolling bar's line
};

VhsFrame vhs_frame(long frame, int level);

// The look as the host's screen effect over a frame of `width` x `height` (a GPU-drawn game's
// logical size): the shader with this frame's tape, and vhs() for where no shader runs. The
// Display aspect sets horizontal signal density; all faults remain in video space.
// Doubling native resolution without changing aspect leaves the video grid unchanged.
platform::ScreenEffect vhs_effect(long frame, int level, int width, int height, double pixel_aspect = 1.0);

} // namespace ui
