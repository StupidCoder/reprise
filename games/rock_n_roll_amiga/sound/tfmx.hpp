#pragma once
#include "amiga/paula_channels.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The game's music and sound effect player, rebuilt from the copy in the
// program ($37F00-$39128, a relocated hunk with a jump table at $37F00).
// Its song data ("TFMX " files, loaded at $5740E) hold:
//   +$100/+$140/+$180  per song (32): first and last track step, speed
//   +$1C0              eight words: per track, notes are not played when set
//   +$400/+$600        offsets of the patterns and macros (longs)
//   +$5FC              offset of the sound effect table (8 bytes each)
//   +$800              track steps, 16 bytes: a word per track (pattern
//                      number << 8 | transpose; negative: the track keeps
//                      what it plays), or $EFFE and a command
// A pattern is a list of longs: notes (byte 0 below $F0: note, macro,
// volume/channel, detune) and commands $F0-$FF. A note starts a macro on a
// channel; a macro is a list of longs that set the channel's sample,
// period and volume and drive its effects. Samples are addressed as offsets
// from $3912C, the base the game passes in (the program's memory from
// there on, with the sample files loaded into it).
// game.md, "Music and sound effects", has the details and the quirks kept.
namespace rocknroll::sound {

class Tfmx {
public:
    static constexpr std::uint32_t sample_base = 0x3912C; // what the game passes to +$34 ($70A7A)

    // The note periods ($390A6: 64 entries; notes past them read on into
    // the bytes after the table, so 256 are kept).
    void set_periods(std::span<const std::uint16_t> periods);
    // +$34 ($38D98): the song data; the per-song start steps and speeds are
    // taken as the positions to resume from.
    void init(std::vector<std::uint8_t> song_data);
    // +$2C ($38D1A): the song starts at the next frame.
    void start_song(int song) { request_ = song; }
    // +$28 ($38D3A): everything stops; the song's step is kept.
    void stop(amiga::PaulaChannels&);
    // +$40 ($38AC4): one channel stops.
    void stop_channel(int channel, amiga::PaulaChannels&);
    // +$60 ($38CBA): a sound effect from the table, by its priority.
    void effect(int number);
    // +$24 ($37F50): one frame.
    void frame(amiga::PaulaChannels&, std::span<std::int8_t> memory);

    bool playing() const { return playing_; }

    // Research: the state in the original's terms (absolute addresses,
    // given where the song data lies), named as the comparison tool reads
    // the original's memory.
    std::vector<std::pair<std::string, std::int64_t>> snapshot(std::uint32_t song_data_address) const;

private:
    static constexpr std::uint32_t unset = 0xFFFFFFFF; // (research: a pointer the original still has at 0)
    struct Track {
        bool muted{};                  // $8: from +$1C0 each step
        std::uint32_t pattern{unset};  // $28: offset of the pattern in the song data
        std::uint8_t status{0xFF};     // $48: pattern number, $FF stopped, $FE stop a channel, $80+ keep
        std::uint8_t transpose{};      // $49 (for $FE: the channel)
        std::uint8_t loop{0xFF};       // $4A
        std::uint16_t position{}, wait{}; // $68, $6A
        std::uint32_t saved_pattern{}; // $88 (gosub)
        std::uint16_t saved_position{};// $A8
    };
    struct Channel {
        int state{};                   // $2: 0 off, 1 note started, -1 running
        std::uint8_t note{}, macro_number{}, volume_level{}; // $11, $13, $21
        std::int8_t detune{};          // $23
        std::uint32_t macro{unset};    // $30
        std::uint16_t macro_position{}, macro_wait{}; // $40, $42
        std::uint8_t macro_loop{0xFF}; // $62
        std::uint8_t volume{};         // $60
        std::uint8_t envelope_speed{}, envelope_count{}, envelope_target{}, envelope_step{}; // $70-$73
        std::int8_t vibrato_delta{};   // $80
        std::uint8_t porta_speed{}, porta_count{}; // $82, $83
        std::int16_t vibrato_offset{}; // $90
        std::uint8_t vibrato_width{}, vibrato_count{}; // $92, $93
        std::uint16_t period{};        // $A0
        std::uint32_t sample{unset};   // $B0: offset from the sample base
        std::uint16_t porta_step{}, porta_period{}; // $C0, $C2
        std::uint16_t length{};        // $D0
        bool key_down{};               // $D2
        std::uint32_t saved_macro{};   // $E0 (gosub)
        std::uint16_t saved_position{};// $F0
    };

    std::uint8_t u8(std::uint32_t at) const { return at < data_.size() ? data_[at] : 0; }
    std::uint16_t u16(std::uint32_t at) const { return std::uint16_t(u8(at) << 8 | u8(at + 1)); }
    std::uint32_t u32(std::uint32_t at) const { return std::uint32_t(u16(at)) << 16 | u16(at + 2); }

    void start(amiga::PaulaChannels&, std::span<std::int8_t> memory); // $38AE6
    void sequencer(amiga::PaulaChannels&);              // $37FAE
    bool track_tick(int track, amiga::PaulaChannels&); // $38076: true when the steps moved on
    void load_step();                    // $38292
    void start_fade(std::uint8_t target, std::uint8_t speed); // $381FC / $3843E
    void note(std::uint32_t command);    // $389A8
    void channel_tick(int channel, amiga::PaulaChannels&); // $384E6
    void macro_tick(int channel, amiga::PaulaChannels&);   // $384FE
    void vibrato(int channel, amiga::PaulaChannels&);      // $38862
    void portamento(int channel, amiga::PaulaChannels&);   // $388A0
    void volume(int channel, amiga::PaulaChannels&);       // $388EE
    std::uint16_t note_period(int note) const;             // table $390A6

    std::vector<std::uint8_t> data_;
    std::array<std::uint16_t, 256> periods_{};
    bool initialised_{}, playing_{};     // $8, $2E
    int request_{-1};                    // $3C: bit 5 keep the channels, bit 6 resume
    int song_{};                         // $C
    std::uint16_t frame_count_{};        // $E
    bool half_speed_{};                  // $3E: sequencer and channels on alternate frames
    std::uint16_t speed_count_{};        // $2C
    bool track8_reserved_{};             // $40
    // Track steps ($38F4A): first, last, current, speed.
    std::uint16_t first_step_{}, last_step_{}, step_{}, speed_{};
    std::array<Track, 8> tracks_{};
    std::array<Channel, 4> channels_{};
    // Master volume and its fade ($18-$1C), and the fade's busy flag ($39092).
    std::uint8_t master_{0x40}, fade_target_{0x40}, fade_count_{}, fade_speed_{};
    std::int8_t fade_direction_{};
    bool fade_busy_{};
    bool fade_stopped_{};                // $39094: set when a step stopped the song
    // Sound effects: priority, time left, last number ($42, $1E, $43).
    std::int8_t effect_priority_{};
    std::int16_t effect_time_{};
    std::uint8_t last_effect_{};
    // Where each song resumes ($39012, $39052).
    std::array<std::uint16_t, 32> resume_step_{}, resume_speed_{};
};

} // namespace rocknroll::sound
