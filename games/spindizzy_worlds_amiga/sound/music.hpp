#pragma once
#include "amiga/paula_channels.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The game's music player (the file "music", loaded at $432A8): entry 0
// starts a tune, entry +$94 plays one tick from the CIA-B interrupt at
// 49.92 Hz on the title, the menus and the maps. Tunes, sequences,
// patterns, instruments, envelopes, periods and samples are the file's
// own tables. Evidence: game.md, "Sound".
namespace spindizzy::sound {

class MusicPlayer {
public:
    explicit MusicPlayer(std::vector<std::uint8_t> file);

    // Entry 0: tune 0-3 ($11250 passes d0 & 3).
    void start(int tune, amiga::PaulaChannels&);
    // The player's state from a copy of its blocks ($4390E, $112 bytes).
    void load_state(std::span<const std::uint8_t> region);
    // $112B0: the music stops (its interrupt hook cleared).
    void stop(amiga::PaulaChannels&);
    // Entry +$94 ($4333C).
    void tick(amiga::PaulaChannels&);
    bool playing() const { return playing_; }
    int tune() const { return playing_ ? tune_ : -1; }

    std::span<const std::int8_t> memory() const { return memory_; }

    // For checks against the original's state ($4390E, $439A0).
    struct Probe { int period, volume, note, row, pattern, countdown, position, instrument, env_position; };
    Probe probe(int channel) const {
        const auto& v = voices_[std::size_t(channel & 3)];
        return {v.period, v.volume, v.note, v.row, v.pattern, v.countdown, v.position, v.instrument, v.env_position};
    }
    int tempo_countdown() const { return tempo_countdown_; }
    int master_volume() const { return volume_; }
    // The player's state (the file's tables and samples are not part of it).
    template <class A> void persist(A& ar) {
        ar(voices_, playing_, fade_period, tune_, volume_, fade_countdown_, tempo_countdown_, tempo_index_,
           instrument_change_, tempo_);
    }

private:
    struct Voice {          // per channel: the state block at $439A0 (+ n) and $4390E (+ 16n …)
        bool off{};          // +$02
        int sequence{};      // +$08: offset of its sequence from $442DA
        int position{};      // +$18: in the sequence
        int row{};           // +$1C: in the pattern
        int repeats{};       // +$20: the pattern's
        int pattern{};       // +$24
        bool in_block{};     // +$2C
        int block_count{};   // +$30
        int block_start{};   // +$34
        int countdown{};     // +$3C: ticks left of the row
        int sample{};        // +$40
        int note{};          // +$44
        bool held{};         // +$48 ($FF: the instrument's second phase is over)
        int instrument{0xFF};// +$4C
        int slide{};         // +$50
        bool vibrato_down{}; // +$54
        int delay{};         // +$58
        int vibrato_init{};  // +$5C
        bool vibrato_back{}; // +$60
        int vibrato_count{}; // +$64
        int vibrato_depth{}; // +$68
        int note_byte{};     // +$6C (bit 7: tied)
        int flags{};         // +$70: the instrument's byte 2
        int phase{};         // +$74: the instrument's byte 7, counted down
        int tick{};          // +$78: ticks into the note
        int first_position{};// +$7C
        // Paula's side ($4390E): sample, loop, envelope, period.
        std::uint32_t start{}, repeat{};
        int length{}, repeat_length{};
        int volume{};        // +$C
        int envelope{};      // +$40 + 2n
        int env_position{}, env_countdown{}, env_repeats{};
        int vibrato_step{};  // +$5C + 2n
        int period{};        // +$64 + 2n
        template <class A> void persist(A& ar) {
            ar(off, sequence, position, row, repeats, pattern, in_block, block_count, block_start, countdown, sample, note, held,
               instrument, slide, vibrato_down, delay, vibrato_init, vibrato_back, vibrato_count, vibrato_depth, note_byte,
               flags, phase, tick, first_position, start, repeat, length, repeat_length, volume, envelope, env_position,
               env_countdown, env_repeats, vibrato_step, period);
        }
    };

    std::uint8_t byte(std::uint32_t address) const { return file_[address - 0x432A8]; }
    std::uint16_t word(std::uint32_t address) const { return std::uint16_t(byte(address) << 8 | byte(address + 1)); }
    std::uint32_t longword(std::uint32_t address) const { return std::uint32_t(word(address)) << 16 | word(address + 2); }
    void next_sequence(Voice&);        // $43876
    void new_row(int n, Voice&, amiga::PaulaChannels&); // $43394
    void effects(int n, Voice&, bool skip_vibrato); // $43548 …
    void envelope(Voice&);             // $43764
    int period_of(int note) const;

    std::vector<std::uint8_t> file_;
    std::vector<std::int8_t> memory_;
    std::array<Voice, 4> voices_{};
    bool playing_{};
    int fade_period{-1};     // +$00 ($FF: no fade)
    int tune_{-1};
    int volume_{0x40};       // +$06
    int fade_countdown_{};   // +$28
    int tempo_countdown_{};  // +$29
    int tempo_index_{};      // +$2A
    int instrument_change_{};// +$2B
    std::array<int, 8> tempo_{}; // +$10 … +$17
};

} // namespace spindizzy::sound
