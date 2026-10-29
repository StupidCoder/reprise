#pragma once
#include "gamegear/psg.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gg {

// A sound test entry: a name and where the game plays it.
struct MusicTrack {
    std::string title;
    std::string description;
};

// The shell's sound test for a Game Gear game: plays one track at a time
// through a fresh sound driver and PSG, frame by frame like the game's
// interrupt, with the transport state and a short waveform history for the
// screen. A game provides the track list and how to start and run a track.
class MusicPlayer {
public:
    enum class Mode { Stopped, Playing, Paused, Finished };

    explicit MusicPlayer(std::vector<MusicTrack> tracks) : tracks_(std::move(tracks)) {}

    virtual ~MusicPlayer() = default;

    const std::vector<MusicTrack>& tracks() const { return tracks_; }

    void play(unsigned track);
    void toggle();
    void stop();
    void skip(int direction);
    // One video frame of the current track; its audio as stereo samples.
    std::vector<float> tick();

    // One Game Gear video frame: 262 scanlines of 228 CPU clocks.
    static constexpr unsigned frame_clocks = 262 * 228;

    static constexpr double step_seconds() { return double(frame_clocks) / Psg::clock_rate; }

    unsigned track() const { return track_; }

    Mode mode() const { return mode_; }

    bool playing() const { return mode_ == Mode::Playing; }

    double elapsed() const { return double(ticks_) * step_seconds(); }

    std::uint64_t ticks() const { return ticks_; }

    // Min/max mono PCM samples over a rolling window, for a blocky oscilloscope.
    std::array<std::array<float, 2>, 100> waveform() const;

protected:
    // Starts `track` from a silent, freshly reset driver.
    virtual void start(unsigned track) = 0;
    // Runs one video frame into `psg` and returns its samples; `active` is
    // false once the track has nothing more to play.
    virtual std::vector<float> frame(Psg& psg, bool& active) = 0;
    // Drops the driver (stopped).
    virtual void release() = 0;

private:
    std::vector<MusicTrack> tracks_;
    Psg psg_;
    unsigned track_{}, tail_{};
    Mode mode_{Mode::Stopped};
    std::uint64_t ticks_{};
    std::array<float, 2000> history_{};
    std::size_t position_{};

    void clear();
};

} // namespace gg
