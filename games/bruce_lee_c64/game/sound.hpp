#pragma once
#include "bruce_lee_c64/data/assets.hpp"
#include "c64/sid.hpp"
#include <array>
#include <cstdint>

// The game's sound: the effect player used during play ($24F6 and the
// three per-voice interpreters at $EB03, $EF73, $F073) and the title's
// music player ($E300, "Micropearl Music Player"). Both are rewritten
// here; their streams and tables are read from the high block ($E000-).
namespace bruce_lee::game {

// Effects: up to three at once, one per voice. A request takes a free
// voice, or the first whose effect is not more important than the new one
// ($255F, by the effect's low nibble).
class EffectPlayer {
public:
    explicit EffectPlayer(const data::Assets&);

    void request(c64::Sid&, std::uint8_t effect); // $24F6 (effects $80-$8F)
    void silence(c64::Sid&);                      // $254B
    void frame(c64::Sid&);                        // from the frame interrupt ($15C5)
    std::uint16_t stream(int voice) const { return channels_[std::size_t(voice)].stream; }

private:
    struct Channel {
        std::uint8_t state{};    // 0 idle, bit 7: just requested (the effect code), else 1 playing
        std::uint8_t position{}; // in the stream
        std::uint8_t wait{};     // frames to the next command
        std::uint16_t stream{};
        std::uint8_t priority{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(state, position, wait, stream, priority); }
    };
    void run(c64::Sid&, int voice);
    std::uint8_t at(std::uint16_t address) const { return assets_.sound_byte(address); }

    const data::Assets& assets_;
    std::array<Channel, 3> channels_{};
    std::array<std::uint8_t, 16> priorities_{}; // $255F

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(channels_, priorities_); }
};

// The title music ($E300-$E7BE): three voices read note and command streams
// per tune ($E8C0); a tick every `tempo` frames counts each voice's note
// down, releases it at 0 and starts the next when the count reaches the
// note's (negative) gap. Commands ($80-$FE) set durations, waveforms,
// envelopes, pulse width, filter and volume, tempo; $FF ends the tune.
// The tune list ($E870) says what follows.
class MusicPlayer {
public:
    explicit MusicPlayer(const data::Assets&);

    void stop(c64::Sid&);                                     // $E300
    void start_list(c64::Sid&, std::uint8_t end_action, std::array<std::uint8_t, 2> list); // $06CC + $E003
    void frame(c64::Sid&);                                    // $E006
    bool playing() const { return playing_; }

private:
    struct Voice {
        std::uint16_t stream{};   // $E3DF/$E3E3/$E3E7
        std::uint8_t legato{};    // $E101: no gate change between notes
        std::uint8_t count{};     // $E104: ticks left of the note, then down to its (negative) gap
        std::uint8_t gap{};       // $E107
        std::uint8_t length{};    // $E10A: the next note's count
        std::uint8_t next_gap{};  // $E10D
        std::uint8_t attack_decay{}, sustain_release{}, waveform{}; // $E110, $E113, $E116
        std::uint8_t style{};     // $E119: how much of a note sounds (0 most, 1 half, 2 all but a tick)
        std::array<std::uint8_t, 6> envelopes{}; // $E11C + 6v: three attack/decay, sustain/release pairs

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(stream, legato, count, gap, length, next_gap, attack_decay, sustain_release, waveform, style,
               envelopes);
        }
    };
    std::uint8_t at(std::uint16_t address) const { return assets_.sound_byte(address); }
    void silence(c64::Sid&);                  // $E3DA
    void start_tune(c64::Sid&);               // $E352
    void end_of_tune(c64::Sid&, std::uint8_t action); // $E438
    void play_list(c64::Sid&, std::uint8_t action);   // $E45E
    std::uint8_t fetch(int voice);            // $E3EA (with the look-ahead byte)
    void next_note(c64::Sid&, int voice);     // $E578
    void commands(c64::Sid&, int voice);      // $E5BB
    void apply_filter(c64::Sid&);             // $E489

    const data::Assets& assets_;
    std::array<Voice, 3> voices_{};
    std::uint8_t look_ahead_{0xFF};           // $E130
    std::uint8_t tune_{};                     // $E309
    std::uint8_t tempo_{}, tempo_count_{};    // $E30D, $E12F
    std::uint8_t transpose_{};                // $E100
    bool playing_{};                          // $E30C
    bool ended_{};                            // $E30E = $FF
    std::uint8_t end_action_{};               // $E314
    std::uint8_t filter_mode_{}, volume_{};   // $E30F, $E311 ($D418)
    std::uint8_t resonance_{};                // $E2E8 ($D417)
    std::uint8_t filter_voice_{};             // $E310: voice + 1 whose next note applies them
    std::uint8_t long_notes_{};               // $E2E7: the second half of the length table
    std::array<std::uint8_t, 2> list_{};      // $E870
    std::uint8_t list_position_{}, list_start_{}; // $E86C, $E86D

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(voices_, look_ahead_, tune_, tempo_, tempo_count_, transpose_, playing_, ended_, end_action_,
           filter_mode_, volume_, resonance_, filter_voice_, long_notes_, list_, list_position_, list_start_);
    }
};

} // namespace bruce_lee::game
