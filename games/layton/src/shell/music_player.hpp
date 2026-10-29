#pragma once
// The music player: plays a game's music the way the game's own bonus music
// page does, from that page's track list and titles in the user's pack.
//
//  * Second adventure, bonus menu overlay 23: the track list is 19 music IDs
//    at 020f3b80 (pages loop to 19, 020f2200), the titles text/music_<n>.txt.
//    Choosing a track (020f2550) stops the music and the stream, then plays
//    ID 1 and 29..31 as a streamed piece (0207fb84, stream route 1) and every
//    other ID as sequenced music (0207faa4), at full volume, at once.
//  * Third adventure, loop lt3CLoopSecretSound (overlay 31): the track list
//    is 26 pairs {kind, ID} at 0211e54c (loops to 26, 0211e02c), the titles
//    rows 1..26 of the text table txt/?/40 part 1400 (the page's script,
//    evt/prg/lt3_scrt_sound). Choosing a track (0211e184) stops the current
//    one (kind 1 stream 020d0820, kind 0 music 020d05c4), opens the stream
//    (020d0618) or loads the music (020d04fc, 30 frames), and 30 frames later
//    starts it at full volume (020d07a8 / 020d05b8).
//
//  * First adventure, the bonus music room (scene 0x12, 0203090c): 19
//    pieces, the titles otext music<n>.txt of the language played. Piece 1
//    is the ending theme's stream (BGM_09.SAD); choosing another stops the
//    current one and loads and plays its sequence (table 020d4da4).
//
// The player runs on the window's thread: tick() advances one frame at 60 Hz.
// Music loops as in the game, so a track plays until it is stopped.
#include "shell/audio_output.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace picarat {
class Adventure1Sound;
class Adventure3Sound;

namespace platform {
class AudioStream;
}
} // namespace picarat

namespace picarat::shell {

struct MusicTrack {
    std::string title; // UTF-8
    int id = 0;        // the music or stream ID
    bool streamed = false;
};

// The track lists; empty when the pack does not hold them (an older pack).
std::vector<MusicTrack> adventure2Music(const std::filesystem::path& pack);
std::vector<MusicTrack> adventure3Music(const std::filesystem::path& pack);
std::vector<MusicTrack> adventure1Music(const std::filesystem::path& pack,
                                        const std::string& language);

class MusicPlayer {
public:
    enum class State { Stopped, Starting, Playing, Paused };

    // Opens the sound device with the game's sound data; throws when the
    // pack cannot be played.
    static std::unique_ptr<MusicPlayer> adventure2(const std::filesystem::path& pack);
    static std::unique_ptr<MusicPlayer> adventure3(const std::filesystem::path& pack);
    static std::unique_ptr<MusicPlayer> adventure1(const std::filesystem::path& pack,
                                                   const std::string& language);
    ~MusicPlayer();

    const std::vector<MusicTrack>& tracks() const { return tracks_; }

    void play(int index);
    void togglePause();
    void stop();
    // One frame (call at 60 Hz).
    void tick();

    int track() const { return track_; } // -1 before the first track

    State state() const { return state_; }

    double elapsed() const { return frames_ / 60.0; }

    // The last `count` output frames, mono, -1..1.
    std::vector<float> waveform(std::size_t count);

private:
    MusicPlayer() = default;
    void stopTrack();

    bool adventure3_ = false;
    std::vector<MusicTrack> tracks_;
    std::unique_ptr<AudioOutput> audio_;
    std::unique_ptr<picarat::Adventure3Sound> sound3_;
    int track_ = -1;
    State state_ = State::Stopped;
    int countdown_ = 0;        // frames until a third-adventure track starts
    std::uint64_t frames_ = 0; // frames played of the current track

    // The first adventure: its sound object on its own device, which renders
    // silence while paused, and the recent output for the waveform.
    bool adventure1_ = false;
    std::unique_ptr<picarat::Adventure1Sound> sound1_;
    std::unique_ptr<platform::AudioStream> device1_; // after sound1_: stops first
    std::atomic<bool> silent_{false};
    std::mutex recentMutex_;
    std::vector<float> recent_ = std::vector<float>(4096, 0.0f);
    std::size_t recentAt_ = 0;
    void play1(const MusicTrack& track);
};

} // namespace picarat::shell
