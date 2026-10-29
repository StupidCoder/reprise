#include "sonic_advance_gba/game/fade.hpp"
#include "sonic_advance_gba/game/video.hpp"

namespace sonic_advance_gba {

namespace {

// The common part: the window covering the whole picture, everything shown
// inside it with effects, everything but effects outside; then the effect
// (brighten or darken) and its strength.
void window_and_brightness(VideoState& v, const ScreenFade& f) {
    auto& s = v.settings;
    if (f.window == 0) {
        s.window0 = true;
        s.windows[0].right = 0xF0;
        s.windows[0].left = 0;
        s.windows[0].bottom = 0xA0;
        s.windows[0].top = 0;
        s.inside[0] = 0x3F;
    } else {
        s.window1 = true;
        s.windows[1].right = 0xF0;
        s.windows[1].left = 0;
        s.windows[1].bottom = 0xA0;
        s.windows[1].top = 0;
        s.inside[1] = 0x3F;
    }
    s.outside = u8(s.outside | 0x1F);
    // The mode bits are ORed into BLDCNT (brighten 2, darken 3).
    s.blend.mode = u8(s.blend.mode | ((f.kind & fade::to_white) ? 2 : 3));
    // BLDY (a halfword): the level's whole part (a signed byte), or 32
    // minus it; above 1F it becomes 20; then halved.
    const s8 whole = s8(u16(f.level) >> 8);
    u16 y = (f.kind & fade::brightness_rises) ? u16(whole) : u16(0x20 - whole);
    if (y > 0x1F) {
        y = 0x20;
    }
    y = u16(y >> 1);
    s.blend.evy = u8(y & 0x1F);
}

} // namespace

void apply_fade(VideoState& v, const ScreenFade& f) {
    auto& b = v.settings.blend;
    if (f.blend & 1) {
        b.set_control(0x3F3F);
        b.eva = 0x10;
        b.evb = 0;
    } else if (f.blend == 0) {
        b.set_control(0x0F);
        b.eva = 0;
        b.evb = 0;
    }
    window_and_brightness(v, f);
}

bool step_fade(VideoState& v, ScreenFade& f) {
    auto& b = v.settings.blend;
    if (f.blend & 1) {
        b.set_control(0x3F3F);
        b.eva = 0x10;
        b.evb = 0;
    } else if (f.blend & 0x10) {
        b.set_control(0x203F);
        b.eva = 0;
        b.evb = 0;
    } else if (f.blend == 0) {
        b.set_control(0x0F);
        b.eva = 0;
        b.evb = 0;
    } else if (f.blend & 8) {
        b.set_control(0x3F3F);
    }
    window_and_brightness(v, f);
    if (f.delay == 0) {
        f.level = s16(f.level + f.speed);
        // Done once the level (as an unsigned halfword) is 2000-7FFF.
        const u16 l = u16(f.level);
        if (l >= 0x2000 && l < 0x8000) {
            f.level = 0x2000;
            return true;
        }
        return false;
    }
    f.delay = f.delay < f.speed ? 0 : s16(f.delay - f.speed);
    return false;
}

} // namespace sonic_advance_gba
