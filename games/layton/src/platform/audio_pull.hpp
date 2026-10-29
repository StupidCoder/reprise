#pragma once
// Sound pulled by an embedding host instead of played by a device of its own.
//
// The standalone app opens a sound device per game (AudioStream in
// host_sdl.cpp), whose thread asks the game's sound for samples in real
// time. Embedded in another application (Reprise: src/embed), the game may
// not open a device: there the AudioStream of a game thread joins the
// AudioPull that thread was started with (AudioPull::current), and the host
// renders it in step with the game's frames - a fixed number of samples per
// frame, so the sound's progress (which the games ask about: is the music
// settled, is a voice still playing) follows the frames and never the
// wall clock.
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <vector>

namespace picarat::platform {

class AudioPull {
public:
    // Fills `frames` stereo frames of 16-bit samples (as AudioStream::Fill).
    using Fill = std::function<void(std::int16_t* stereo, int frames)>;

    // The pull a thread's AudioStreams join (set by the thread that runs a
    // game; none on the standalone app's game threads).
    static thread_local AudioPull* current;

    // A stream joins (`rate` its own sample rate) or leaves; `key` identifies it.
    void add(const void* key, int rate, Fill fill);
    void remove(const void* key);

    // Mixes `frames` stereo frames at `rate` from every stream, each
    // resampled from its own rate (linear interpolation), into `out`
    // (2 * frames floats, -1..1). Streams render only what this needs.
    void render(std::span<float> out, int rate);

private:
    struct Source {
        const void* key;
        int rate;
        Fill fill;
        double position = 0;             // in source frames, from buffer[0]
        std::vector<std::int16_t> buffer; // source frames not used up yet (stereo)
    };
    std::mutex mutex_;
    std::vector<Source> sources_;
};

} // namespace picarat::platform
