// =============================================================================================
// Offscreen.hpp - offscreen render targets and framebuffer read-back
// =============================================================================================
//
// The research modes and the shell's snapshot render without showing a window: they draw into
// an offscreen target and read the result back as an image.
// =============================================================================================
#pragma once
#include "gpu/Device.hpp"
#include "platform/GameImage.hpp"

namespace captain_toad::platform {

// Creates an offscreen colour + depth/stencil target of the given size and leaves it bound. The
// target lives until the process ends (research runs and snapshots create one or two).
inline captain_toad::gpu::Target* offscreenTarget(int w, int h) {
    captain_toad::gpu::Target* target = captain_toad::gpu::device().createTarget(w, h).release();
    captain_toad::gpu::device().bindTarget(target);
    return target;
}

// Reads the bound target into an opaque top-down image (its alpha is not transparency).
inline Image readFramebuffer(int w, int h) {
    return captain_toad::gpu::device().readPixels({0, 0, w, h});
}

}   // namespace captain_toad::platform
