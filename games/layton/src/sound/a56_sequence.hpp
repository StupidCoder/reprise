#pragma once
// The 3DS adventures' effect sequences (NintendoWare BCSEQ inside the sound
// archive), played in real time into the mixer.
//
// A sequence is bytecode for up to 16 tracks. Multi-byte arguments are
// big-endian, durations are MIDI-style variable-length numbers. The fifth
// adventure's 469 effects use only these commands (cseq.py survey):
//   0x00..0x7f key, velocity, duration   note; duration 0 holds the note
//                                         until its wave ends (or the
//                                         effect is stopped)
//   0x80 duration                         wait
//   0x81 number                           program (bank instrument)
//   0x88 track, u24 offset                open a track
//   0xb0 ticks                            timebase (ticks per quarter)
//   0xc0 pan (0 left, 64 centre, 127 right)
//   0xc1 volume
//   0xfe u16 mask                         tracks the sequence uses
//   0xff                                  end of track
// The player also understands the common neighbours (jumps, calls,
// loops, tempo, transpose, the other one-byte settings) so a sequence
// that uses them still plays; anything unknown ends the track.
//
// Compared with the console's audio (puzzle 1's recording): the effect
// waveforms match, and the centre level of 1/sqrt(2) per side (a
// square-root pan law). Hypotheses: velocity, volume and region volume
// scale the amplitude by (v / 127)^2 (every effect so far uses 127); the
// envelope rates (only a handful of regions use anything but the instant
// 127 settings).
#include "sound/a56_stream.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace picarat::adventure56 {

// One bank instrument's key region (every effect bank uses one region
// per instrument).
struct Instrument {
    std::shared_ptr<const Pcm> wave;
    int originalKey = 60;
    int volume = 127; // 0..127
    int pan = 64;     // 0..127
    float pitch = 1.0f;
    int attack = 127, decay = 127, sustain = 127, hold = 0, release = 127;
};

class SequencePlayer {
public:
    // `data`: the sequence's DATA block; `start`: the effect's entry;
    // `instruments`: the bank's programs (empty slots have no wave).
    SequencePlayer(std::shared_ptr<const std::vector<std::uint8_t>> data, std::size_t start,
                   std::vector<Instrument> instruments);

    // Mixes `frames` stereo frames at `rate` into `mix` scaled by `gain`;
    // false once every track and note has finished.
    bool render(float* mix, int frames, int rate, float gain);
    // Stops the tracks and lets every note enter its release.
    void release();

private:
    struct Track {
        bool active = false;
        std::size_t pc = 0;
        long wait = 0;
        int volume = 127, pan = 64, program = 0, transpose = 0;
        bool noteWait = true;
        std::vector<std::size_t> calls;

        struct Loop {
            std::size_t pc;
            int count;
        };

        std::vector<Loop> loops;
    };

    struct Note {
        const Instrument* instrument = nullptr;
        double position = 0, step = 0;
        float left = 0, right = 0;
        long length = 0; // ticks until note-off; < 0: until the wave ends
        enum class Phase { Attack, Decay, Sustain, Release, Done } phase = Phase::Attack;
        float level = 0; // envelope amplitude 0..1
    };

    void tick();
    void step(Track& t);
    void startNote(Track& t, int key, int velocity, long length);
    void releaseNote(Note& n);
    void mixNote(Note& n, float* mix, int frames, int rate, float gain);

    std::shared_ptr<const std::vector<std::uint8_t>> data_;
    std::vector<Instrument> instruments_;
    std::array<Track, 16> tracks_;
    std::vector<Note> notes_;
    int timebase_ = 48, tempo_ = 120;
    double tickClock_ = 0; // seconds until the next tick
    bool released_ = false;
};

} // namespace picarat::adventure56
