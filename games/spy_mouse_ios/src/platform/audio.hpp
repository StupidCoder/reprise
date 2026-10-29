// Sound output: a small mixer on an SDL audio stream (stereo float at 44.1 kHz). Effects are
// the game's WAV files, decoded once; music is a source the mixer pulls while it plays (the
// game's AAC files, decoded by media/aac_stream.hpp on every platform).
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace doc::platform {

// Decoded sound: interleaved stereo float samples at the mixer's rate.
struct Pcm {
    std::vector<float> samples;
    std::size_t frames() const { return samples.size() / 2; }
};

// A music track decoded as it plays. Called from the mixer (the audio thread).
class MusicSource {
public:
    virtual ~MusicSource() = default;
    // Up to `frames` interleaved stereo frames at Audio::kRate; fewer at the end of the track.
    virtual int read(float* stereo, int frames) = 0;
    virtual void rewind() = 0;
};

class Audio {
public:
    static constexpr int kRate = 44100;
    ~Audio();
    // Opens the default output; false (and silent) when there is none.
    bool open();
    // No output of its own: the embedding application asks for the mix (render) as it needs it.
    void openPull() { pull_ = true; }
    // Pull mode: the next `frames` stereo frames at kRate, added into `out` (cleared first).
    void render(float* out, int frames);

    // A WAV file's bytes to mixer samples.
    static std::shared_ptr<Pcm> decodeWav(const std::vector<std::uint8_t>& bytes);

    // Plays a sound; returns its voice (0 if nothing plays).
    int play(std::shared_ptr<const Pcm> pcm, bool loop, float volume = 1.0f);
    void stop(int voice);
    bool playing(int voice) const;
    void stopAll();
    void stopEffects();  // all voices, the music plays on

    // One music track at a time.
    void playMusic(std::shared_ptr<MusicSource> music, bool loop);
    void stopMusic();

private:
    struct Voice {
        int id = 0;
        std::shared_ptr<const Pcm> pcm;
        std::size_t at = 0;
        bool loop = false;
        float volume = 1.0f;
    };
    static void feed(void* self, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);

    SDL_AudioStream* stream_ = nullptr;
    bool pull_ = false;
    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    std::shared_ptr<MusicSource> music_;
    bool musicLoop_ = false;
    std::vector<float> musicBuffer_;
    int nextId_ = 1;
    std::vector<float> buffer_;
};

}  // namespace doc::platform
