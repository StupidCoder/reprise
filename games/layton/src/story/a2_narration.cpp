// Narration pages (overlay 10). See a2_narration.hpp.
#include "story/a2_narration.hpp"
#include <stdexcept>

namespace picarat {
bool NarrationPage::presentPages(NarrationHost& host, int firstActor, int endActor, int voiceId,
                                 int fadeRepeats) {
    // Native object-index validation replaces guest pointer indexing. Reversed
    // in-range spans intentionally retain the original empty drawing loop.
    if (firstActor < 0 || firstActor > 6 || endActor < 0 || endActor > 6 || mode > 3) {
        throw std::runtime_error("narration actor range/mode outside recovered context");
    }
    for (int frame = 0; frame < 60; ++frame) {
        host.update();
        host.waitOneFrame();
        if (mode != 3 && host.skipRequested()) {
            return true;
        }
    }
    fadeLevel = 0;
    host.startVoice(2, voiceId, firstActor, 1.0f, 0.0f);
    int frame = 0;
    while (fadeLevel < 31) {
        // 020f0210..228 tests a signed remainder. This local counter is reset
        // above and reaches only 0..60, so its evenness has no overflow case.
        if ((frame % 2) == 0) {
            ++fadeLevel;
        }
        ++frame;
        host.update();
        for (int actor = firstActor; actor < endActor; ++actor) {
            if (fadeLevel > 1) {
                host.draw(actor, fadeLevel);
            }
        }
        host.present();
        if (mode != 3 && host.skipRequested()) {
            return true;
        }
        if (host.abortRequested()) {
            return true;
        }
    }
    bool playing = host.voicePlaying();
    while (playing) {
        host.update();
        host.waitOneFrame();
        if (mode != 3 && host.skipRequested()) {
            return true;
        }
        playing = host.voicePlaying();
    }
    host.stopVoice(0.0f);
    if (fadeMusicOnExit) {
        host.fadeMusic(0, 5.0f);
        fadeRepeats = 6;
    }
    for (int alpha = 31; alpha > 0; --alpha) {
        for (int repeat = 0; repeat < fadeRepeats; ++repeat) {
            host.update();
            for (int actor = firstActor; actor < endActor; ++actor) {
                host.draw(actor, alpha);
            }
            host.present();
            if (mode != 3 && host.skipRequested()) {
                return true;
            }
        }
    }
    // The original never stores the local fade-out alpha back into fadeLevel.
    // Abort is checked ONLY during fade-in; do not add it to the other loops.
    host.update();
    host.present();
    return false;
}
} // namespace picarat
