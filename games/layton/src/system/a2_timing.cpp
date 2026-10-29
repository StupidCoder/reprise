// Frame waits and fades. See a2_timing.hpp.
#include "system/a2_timing.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace picarat {
void OpeningTiming::finishFrame() {
    host_.submitFrame();
    host_.waitVBlank();
    host_.afterVBlank();
}

// ov2:0206ce18 (+4598). Zero and negative counts still wait once; frame
// preparation is deliberately omitted on the first iteration.
void OpeningTiming::wait(std::int32_t count) {
    auto limit = std::max<std::int32_t>(1, count);
    for (std::int32_t i = 0; i < limit; ++i) {
        if (i != 0 && interrupted && interrupted()) {
            return; // the host's skip
        }
        if (i != 0) {
            host_.prepareFrame();
        }
        finishFrame();
    }
}

// ov2:0206ce64 (+45e4). Poll before preparing every frame. Unlike wait(),
// nonpositive counts do nothing. This is a release event, not any held key.
void OpeningTiming::waitForTouchRelease(std::int32_t count) {
    for (std::int32_t i = 0; i < count; ++i) {
        if (host_.pollTouchRelease() || (interrupted && interrupted())) {
            return;
        }
        host_.prepareFrame();
        finishFrame();
    }
}

void OpeningTiming::shiftBrightness(std::int32_t step) {
    // Wider intermediate avoids signed C++ overflow for malformed host state.
    auto shifted = [step](std::int32_t value) {
        return std::int32_t(
            std::clamp(std::int64_t(value) + step, std::int64_t(-16), std::int64_t(0)));
    };
    mainBrightness = shifted(mainBrightness);
    host_.brightness(false, mainBrightness);
    subBrightness = shifted(subBrightness);
    host_.brightness(true, subBrightness);
}

// ov2:02071790 (+8f10), including its 17-iteration safety bound and early exit.
void OpeningTiming::fadeIn() {
    for (int i = -16; i <= 0; ++i) {
        wait(1);
        shiftBrightness(interrupted && interrupted() ? 16 : 1); // a skip ends the fade
        ++fadeSteps;
        if (mainBrightness == 0 && subBrightness == 0) {
            break;
        }
    }
}

// ov2:02071e84 (+9604). The inclusive counter comparison causes 17 waits,
// even though both levels have already reached -16 on iteration 16.
void OpeningTiming::fadeOut() {
    for (int i = 0; i >= -16; --i) {
        wait(1);
        ++fadeSteps;
        if (interrupted && interrupted()) {
            shiftBrightness(-16); // a skip ends the fade
            return;
        }
        shiftBrightness(-1);
    }
}

// Original soft-float calls perform a division followed by a separately rounded
// addition/subtraction on EACH frame. Replacing this with i*16/frames changes
// the brightness sequence. The handler always starts at -16 (in) or 0 (out),
// rather than interpolating from the current brightness fields.
void OpeningTiming::fadeOverFrames(bool fadeIn, std::int32_t frames) {
    static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4);
    if (frames <= 0) {
        throw std::runtime_error("nonpositive fade duration is unrecovered");
    }
    const float step = 16.0f / float(frames);
    float level = fadeIn ? -16.0f : 0.0f;
    for (;;) {
        wait(1);
        level = fadeIn ? level + step : level - step;
        if (onFadeFloat) {
            onFadeFloat(std::bit_cast<std::uint32_t>(level));
        }
        ++fadeSteps;
        const bool finished = fadeIn ? level >= 0.0f : level <= -16.0f;
        const auto brightness = finished ? (fadeIn ? 0 : -16) : std::int32_t(level);
        mainBrightness = brightness;
        host_.brightness(false, mainBrightness);
        subBrightness = brightness;
        host_.brightness(true, subBrightness);
        if (finished) {
            return;
        }
    }
}
} // namespace picarat
