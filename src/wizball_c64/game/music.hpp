#pragma once
#include "c64/sid.hpp"
#include "wizball_c64/data/assets.hpp"
#include <array>
#include <cstdint>

// Wizball's tunes ($45F1 start, $47F2 set-up, $4D4E/$4E86/$4FD5 the three
// voices' sequencers, $50F4/$5269/$53D7 their per-tick sweeps, $4CB7 the
// filter sweep). A tune ($5520 + n: three stream addresses and a tempo)
// gives each voice a stream of notes (a note and a duration, which indexes
// a table of tempo multiples) and commands $C0-$EA: calls, jumps and loops
// with a five-deep stack, transposes, instrument fields, filter routing,
// and one that runs a routine of the game (only two are used). An
// instrument (29 bytes) holds a frequency sweep or arpeggio, a pulse-width
// sweep, the pulse width, waveform, envelope, a gate time and a release
// time; a note copies it into the voice's working block, which the sweeps
// then run on. The streams address both blocks by field number, so they
// are kept as numbered fields here.
namespace wizball::game {

class Music {
public:
    explicit Music(const data::GameImage& image);

    void start(int tune);       // $45F1 / $47F2
    void reset(c64::Sid& sid);  // $47D0
    // One tick: voices `first`-2 (3 voices when first = 0; only the third
    // when first = 2), then the filter sweep ($B925 / $B93A).
    void tick(c64::Sid& sid, int first);
    bool third_voice_playing() const { return voices_[2].active; } // $5B ($5522)
    bool playing() const { return voices_[0].active || voices_[1].active || voices_[2].active; } // $5513
    std::uint8_t filter_mode_bits() const { return filter_mode_; } // $B94D
    void set_filter_mode_bits(std::uint8_t v) { filter_mode_ = v; }

    struct Voice {
        bool active{};
        std::uint8_t duration{};
        std::uint16_t pointer{};
        std::uint8_t transpose{};
        std::uint8_t depth{4};
        std::array<std::uint16_t, 6> return_to{};
        std::array<std::uint8_t, 6> count{};
        std::array<std::uint8_t, 29> instrument{}; // $87 / $4680 / $A4 on
        std::array<std::uint8_t, 35> work{};       // $469D / $46C0 / $C1 on
        std::uint16_t pulse{}, frequency{};        // $29, $2F (voice 1); $2B, $15; $2D, $17
    };
    const std::array<Voice, 3>& voices() const { return voices_; }
    void set_duration(int voice, std::uint8_t d) { voices_[std::size_t(voice)].duration = d; } // checks only

private:
    enum class Command {
        end, call, jump, transpose_call, transpose_jump, set_field, loop, next, instrument, envelope,
        set_work, routine, transpose, set_field2, set_work2, filter_on, filter_params, filter_off,
        arpeggio5, arpeggio7, modulation, tempo, invalid
    };
    Command command_of(int voice, std::uint8_t code) const;
    void sequence(c64::Sid& sid, int voice);
    void note(c64::Sid& sid, int voice, std::uint8_t byte, std::uint8_t length);
    void sweeps(c64::Sid& sid, int voice);
    void filter_reset(c64::Sid& sid); // $4D3B
    void filter_sweep(c64::Sid& sid); // $4CB7
    void write_cutoff(c64::Sid& sid);
    void filter_route(c64::Sid& sid, std::uint8_t routing, int owner);
    void build_durations();

    const data::GameImage& image_;
    std::array<Voice, 3> voices_{};
    std::array<std::uint8_t, 34> durations_{}; // $46E2 + n
    std::uint8_t tempo_{};                     // $4838
    std::array<std::uint8_t, 16> filter_params_{}; // $31-$40
    std::array<std::uint8_t, 16> filter_work_{};   // $41-$50: deltas, counts, delay, mode, start
    std::uint16_t cutoff_{};                       // $51/$52
    std::array<std::uint8_t, 4> filter_count_{};   // $53-$56
    std::uint8_t filter_owner_{0xFF};              // $57
    std::uint8_t filter_mode_{0x10};               // $B94D
};

} // namespace wizball::game
