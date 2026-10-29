#pragma once
// Narration (overlay 10): a page of narrated story text, read aloud.
//
// A narration page is a set of sprites ("actors") that fade in over
// about 60 frames while the narrator's voice plays, stay until the voice has
// finished, and then fade out. A tap skips ahead (except in mode 3). The
// story scene calls presentPages() once per page.
#include <cstdint>

namespace picarat {
// What a narration page needs from the scene showing it. Whether the voice
// is still playing is asked of the sound system every frame, never timed.
struct NarrationHost {
    virtual ~NarrationHost() = default;
    virtual void update() = 0;                        // ov2 0206c7b4
    virtual void waitOneFrame() = 0;                  // ov2 0206ce18(...,1)
    virtual void present() = 0;                       // ov2 0206cfb8
    virtual void draw(int actorIndex, int alpha) = 0; // ov2 02075e50
    virtual bool skipRequested() = 0;                 // ov2 0206ca88
    virtual bool abortRequested() = 0;                // ov2 0206ae08
    virtual void startVoice(int category, int id, int subindex, float volume, float fade) = 0;
    virtual bool voicePlaying() = 0;                        // ov2 0207fb14
    virtual void stopVoice(float fade) = 0;                 // ov2 0207fbcc
    virtual void fadeMusic(int channel, float seconds) = 0; // ov2 0207fae4
};

struct NarrationPage {
    std::int32_t fadeLevel = 0;   // Original context +18, not the local fade-out alpha.
    std::uint8_t mode = 0;        // +1c: mode 3 suppresses skip input checks.
    bool fadeMusicOnExit = false; // +20

    // Shows one page with the actors firstActor..endActor-1 (overlay 10,
    // 020f0164): wait 60 frames, fade in while the voice starts, wait for the
    // voice to end, fade out (each alpha step shown `fadeRepeats` times).
    // Returns true when the player skipped or the page was aborted; the
    // caller cleans up in that case.
    bool presentPages(NarrationHost& host, int firstActor, int endActor, int voiceId,
                      int fadeRepeats);
};
} // namespace picarat
