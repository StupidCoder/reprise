#pragma once
// Frame timing of the logo and title sequence: waiting frames, waiting for a
// tap, and brightness fades, with the original's exact frame counts.
#include <cstdint>
#include <functional>

namespace picarat {
// What a frame needs from the host, in the order the game calls it each frame.
struct FrameHost {
    virtual ~FrameHost() = default;
    virtual void prepareFrame() = 0;     // ov2:0206d950
    virtual void submitFrame() = 0;      // ov2:0206cb00
    virtual void waitVBlank() = 0;       // ARM9:0201fd44
    virtual void afterVBlank() = 0;      // ov2:0206aebc
    virtual bool pollTouchRelease() = 0; // ov2:0206e2c0; 0206e1f8 == state 3
    virtual void brightness(bool sub, std::int32_t value) = 0;
};

// The wait and fade routines of overlay 2, used by the opening.
class OpeningTiming {
public:
    explicit OpeningTiming(FrameHost& host) : host_(host) {}

    void wait(std::int32_t count);
    void waitForTouchRelease(std::int32_t count);
    // Reconstructed parameter specialization used by logo commands: step=1,
    // interval=1. Initial levels must be supplied by the scene, not guessed.
    void fadeIn();
    void fadeOut();
    // ov2:02071884 / 02071f6c. Separate duration-based binary32 routines used
    // after name entry. Positive durations only; other inputs remain unrecovered.
    void fadeOverFrames(bool fadeIn, std::int32_t frames);
    std::function<void(std::uint32_t)> onFadeFloat;
    // The host's skip (system/game_types.hpp; not in the original): while it
    // returns true, waits end at once and fades jump to their end. Set by a
    // scene for its skippable stretches only.
    std::function<bool()> interrupted;
    std::int32_t mainBrightness = -16, subBrightness = -16;
    std::uint32_t fadeSteps = 0;

private:
    FrameHost& host_;
    void finishFrame();
    void shiftBrightness(std::int32_t step);
};
} // namespace picarat
