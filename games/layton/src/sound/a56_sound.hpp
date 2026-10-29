#pragma once
// The 3DS adventures' sound player: the stream players (music, events,
// jingles, voices, movie sound, ambience) and the effects, mixed to stereo
// for the host. The scripts' sound services (11xxx) drive it.
//
// Players, as the game's sound manager numbers them (table 0x411ed0: the
// stream name prefixes): 0 BGM, 1 EVT, 2 JNG, 3 EVS, 4 MOV, 5 SUR. A
// stream is played by name ("BGM_004") from snd/stream/<name>.bcstm;
// starting one on a player replaces what it played. Fades are in game
// frames (60 per second). Volumes come from the sound archive's sound
// table when known (0..127, 127 full). Effects are the archive's
// sequences, run in real time on the audio thread (a56_sequence.hpp).
#include "sound/a56_sequence.hpp"
#include "sound/a56_stream.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace picarat::adventure56 {

class SoundArchive; // a56_sound_archive.hpp (effects)

class SoundPlayer {
public:
    static constexpr int kRate = 32728; // the 3DS sound rate; streams play at their own
    static constexpr int kStreamPlayers = 6;

    explicit SoundPlayer(std::filesystem::path soundFolder);
    ~SoundPlayer();

    // Streams.
    bool playStream(int player, const std::string& name, int fadeInFrames = 0);
    void stopStream(int player, int fadeOutFrames = 0);
    bool streamPlaying(int player) const;
    // Still fading out after a stop.
    bool streamStopping(int player) const;
    // Pause: fade to silence over the frames and hold the place; resume
    // fades back in from there.
    // A stream player's own volume (the SDK player's, 0x2019b0; 1 at
    // first), applied to whatever it plays.
    void setStreamVolume(int player, float volume);
    void pauseStream(int player, int fadeFrames);
    void resumeStream(int player, int fadeFrames);

    // Effects (archive sequences by name, "SI_291_577").
    bool playEffect(const std::string& name);
    void stopEffect(const std::string& name, int fadeOutFrames = 0);
    bool effectPlaying(const std::string& name) const;
    void stopEffects(int fadeOutFrames = 0);
    bool hasSound(const std::string& name) const;

    // Effects off (tools compare the streams alone).
    void muteEffects(bool muted) { effectsMuted_ = muted; }

    // The audio thread: `frames` stereo frames at kRate.
    void render(std::int16_t* stereo, int frames);

private:
    struct Voice {
        std::shared_ptr<const Pcm> pcm;
        std::string name;
        double position = 0; // frames into the PCM
        double step = 1;     // PCM frames per output frame
        float volume = 1;
        float fade = 1, fadeStep = 0; // per output frame; stops at 0 when fading out
        bool stopping = false;
        bool pausing = false, paused = false;
        bool done = false;
    };

    struct Effect {
        std::string name;
        std::unique_ptr<SequencePlayer> player;
        float volume = 1;
        float fade = 1, fadeStep = 0;
        bool done = false;
    };

    std::shared_ptr<const Pcm> load(const std::string& name);
    void start(Voice& v, std::shared_ptr<const Pcm> pcm, const std::string& name, int fadeInFrames);
    static void fadeOut(Voice& v, int frames);
    static void fadeOut(Effect& e, int frames);
    float archiveVolume(const std::string& name) const;

    std::filesystem::path folder_;
    mutable std::mutex mutex_;
    Voice streams_[kStreamPlayers];
    float playerVolume_[kStreamPlayers] = {1, 1, 1, 1, 1, 1};
    std::vector<Effect> effects_;
    std::vector<float> effectMix_; // the audio thread's scratch
    std::map<std::string, std::shared_ptr<const Pcm>> cache_;
    std::vector<std::string> cacheOrder_;
    std::unique_ptr<SoundArchive> archive_;
    bool effectsMuted_ = false;
};

} // namespace picarat::adventure56
