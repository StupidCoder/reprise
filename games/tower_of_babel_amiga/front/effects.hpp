#pragma once
#include "amiga/paula_channels.hpp"
#include "tower_of_babel_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The game's sound effects: 46-byte records in the program (from $3504)
// played on the four channels with pitch and volume envelopes.
//
// A record holds three segments of 12 bytes (a waveform or -1, a pitch
// step and its length, a volume step and its length; +$00, +$0C, +$18),
// the priority (+$24: bit 0 the envelopes run, bit 1 a second record
// follows at +$2E), the pitch and volume segments' repeat counts (+$26,
// +$28) and the starting pitch and volume (+$2A, +$2C, both in 64ths). A
// sound takes the channel with the lowest priority when its own is higher
// ($15540). Each tick of the CIA timer (about 99 a second, $81FA) every
// sounding channel adds its steps; when a step's length runs out the next
// segment (from +$18 down to +$00, then again while the repeats last) gives
// the next step, and a new waveform plays from the end of the current one.
// The pitch picks a period from the program's table ($D2(a6), 432 entries).
namespace babel::front {

class Effects {
public:
    explicit Effects(const data::Assets&);

    // $15540: starts the record at this address now (and its second part).
    void start(std::uint32_t record);
    // $15494: every channel silent and free. $154C6 / $154F2: paused (the
    // envelopes wait, nothing sounds) and going on.
    void stop();
    void pause();
    void resume();

    // $15342, once a game frame: the sounds queued during the frame, then
    // the lifts' hum (while lifts or forcers move; a clunk when it stops)
    // and the tower's own background sound (setting $A4: 0 none).
    struct Frame {
        std::vector<std::uint32_t> queued; // (a silent tower queues only footsteps, $1531E)
        bool lifts_moving{};
        int background{};
        bool silent{}; // setting $A8: no effects at all
    };
    void frame(const Frame&);

    // The volume offset ($CE(a6)): quieter when seen from a camera ($AB22).
    void set_volume_offset(int offset) { volume_offset_ = offset; }

    // One tick of the timer ($81FA).
    void tick();
    // Adds the channels' sound for `stereo.size() / 2` frames at `rate`.
    void render(std::span<float> stereo, int rate) { paula_.render(stereo, rate); }

    // The channels as the original keeps them ($432(a6) + $22 n), for checks.
    struct Channel {
        std::uint32_t record{}; // +$04
        int priority{};         // +$08 (0 free)
        int pitch_repeats{}, volume_repeats{}; // +$0A, +$0C
        int pitch{}, volume{};  // +$0E, +$10 (64ths)
        int pitch_segment{}, volume_segment{}; // +$16, +$18 (24, 12, 0)
        int pitch_step{}, pitch_wait{}, volume_step{}, volume_wait{}; // +$1A-$20
    };
    std::array<Channel, 4>& channels() { return channels_; }

private:
    bool start_one(std::uint32_t record);
    void play_waveform(int channel, std::uint32_t waveform, bool now);
    int period(int pitch) const;
    int loudness(int volume) const;
    void silence(int channel);

    const data::Assets& assets_;
    std::vector<std::int8_t> memory_; // the main data as Paula reads it
    amiga::PaulaChannels paula_;
    std::array<Channel, 4> channels_{};
    bool paused_{};
    int volume_offset_{};
    int background_toggle_{}; // $CC(a6): the two background records in turn
};

} // namespace babel::front
