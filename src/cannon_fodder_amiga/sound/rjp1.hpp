#pragma once
#include "amiga/paula_channels.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The game's music and sound player for "RJP1" song/instrument pairs
// (.analysis/cannon_fodder_amiga/sound.md), rebuilt from the program's own
// routines ($A3E14 … $A4854).
//
// A song (`.sng`, "RJP1SMOD" and seven sections) gives each of the four
// channels a sequence: a list of patterns, with jumps. A pattern is byte
// code: notes (an index into the program's period table), and commands for
// the end of the pattern, release, note length, rows, instrument, volume and
// a portamento. An instrument plays a sample from the `.ins` file (an attack
// part, then a loop), shaped by an attack/decay/release envelope, a volume
// table and a period table (vibrato). Sound effects are single patterns
// played on a channel over the music, which pauses there until the effect
// ends. The player runs once a frame and writes the channels' registers.
namespace fodder::sound {

class Rjp1 {
public:
    // The song and instrument files and the program (for its period table at
    // $A4856). Starts like $A3E14: all channel volumes 64, mode $D.
    Rjp1(std::span<const std::uint8_t> sng, std::span<const std::uint8_t> ins, std::span<const std::uint8_t> program_file);

    void start_song(int song);                     // $A3FCC
    void start_sequence(int channel, int sequence); // $A400A
    void start_effect(int channel, int pattern);   // $A4076
    void set_channel_volume(int channel, int volume); // $A3F60 (channel < 0: all)
    void set_mode(int mode);                       // $A3F96: speed (bits 0-1), music (3), effects (2)
    void stop(amiga::PaulaChannels& paula);        // $A3F26
    // $A40C2: one frame; returns the busy bits (0-3 music, 4-7 effects).
    int frame(amiga::PaulaChannels& paula);

    std::span<const std::int8_t> samples() const { return samples_; }
    bool playing() const { return on_; }
    int channel_volume(int channel) const { return channel_volume_[std::size_t(channel & 3)]; }
    void set_channel_volume_raw(int channel, int volume) { channel_volume_[std::size_t(channel & 3)] = volume; }

    // One voice: the music part of a channel or its effect part (the
    // original's channel record, $B0 bytes, holds both).
    struct Voice {
        int instrument{};      // $5A: number (0 none)
        int sample{};          // $04: offset of the instrument's sample in the .ins data
        int period_table{-1};  // $08
        int volume_table{-1};  // $0C
        int envelope{};        // $10: offset into the envelope section
        int attack_length{};   // $14 (words)
        int loop_start{}, loop_length{}; // $16, $18 (words)
        int period_loop{}, period_end{}, period_position{}; // $1A, $1C, $20
        int volume_loop{}, volume_end{}, volume_position{}; // $24, $26, $2A
        int volume_scale{};    // $30
        int volume{};          // $32 (word)
        int base_period{}, period{}; // $34, $36
        bool key_on{}, loop_next{};  // $38, $39
        int envelope_stage{};  // $3A: 4 attack, 2 decay, -1 release, 0 still
        int envelope_delta{}, envelope_count{}, envelope_length{}, envelope_target{}, envelope_value{}; // $3B-$3F
        int slide_count{};     // $40
        std::int32_t slide_step{}, slide{}; // $42, $46
        int pattern{-1};       // $52: offset into the pattern section, -1 none
        int note_length{6}, note_count{}, rows{}, row_count{}; // $56-$59
    };
    struct Channel {
        Voice music, effect;
        int sequence{-1};      // $4E: offset into the sequence section
        bool active{};         // $5B
    };
    // Verification access (tools/cannon_fodder_assets sndreplay).
    std::array<Channel, 4>& channels() { return channels_; }
    int& busy() { return busy_; }
    int& mode() { return mode_; }
    bool& half_step() { return half_step_; }
    bool& music_on() { return music_on_; }
    bool& effects_on() { return effects_on_; }
    bool& on() { return on_; }
    struct Sections { int instruments, envelopes, songs, sequence_table, pattern_table, sequences, patterns; };
    const Sections& sections() const { return sections_; }

private:
    struct Instrument {
        int sample, period_table, volume_table, envelope, volume_scale, attack_offset, attack_length, loop_start,
            loop_length, period_loop, period_length, volume_loop, volume_length;
    };
    void update_channel(int n, amiga::PaulaChannels& paula, int steps);
    void step_music(int n, amiga::PaulaChannels& paula, int steps);
    void step_effect(int n, amiga::PaulaChannels& paula);
    // Pattern byte code shared by both parts; false when the part's pattern
    // (or the music's whole sequence) ended.
    void read_pattern(int n, Voice& v, bool music, amiga::PaulaChannels& paula);
    void select_instrument(Voice& v, int number);
    void note_on(int n, Voice& v, int period, amiga::PaulaChannels& paula);
    void release(Voice& v);
    void envelope(Voice& v);
    void apply_volume_table(Voice& v);
    void write_volume(int n, Voice& v, amiga::PaulaChannels& paula);
    void write_period(int n, Voice& v, amiga::PaulaChannels& paula);
    void key_and_loop(int n, Voice& v, amiga::PaulaChannels& paula);
    int sng(int offset) const { return offset >= 0 && std::size_t(offset) < sng_.size() ? sng_[std::size_t(offset)] : 0; }
    std::int32_t sng_long(int offset) const;
    int pattern_offset(int number) const;
    int sequence_offset(int number) const;

    std::vector<std::uint8_t> sng_;
    std::vector<std::int8_t> samples_; // the .ins data after "RJP1" (the player writes into it)
    Sections sections_{};
    std::vector<Instrument> instruments_;
    std::array<int, 64> periods_{};
    std::array<Channel, 4> channels_{};
    std::array<int, 4> channel_volume_{64, 64, 64, 64}; // $28-$2B
    int mode_{0xd};        // $22
    bool half_step_{};     // $25
    bool music_on_{true}, effects_on_{true}; // $26, $27
    bool on_{true};        // $A3C5E
    int busy_{};           // $2C
};

} // namespace fodder::sound
