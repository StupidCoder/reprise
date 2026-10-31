#pragma once
#include "amiga/paula_channels.hpp"
#include "fa_18_interceptor_amiga/data/assets.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The game's own sound driver (hunks 74-86, game.md): forty sound slots
// and the four channels. A slot is a block of samples with a period, a
// volume and a repeat count; a channel plays the slot put on it. The audio
// interrupt ($1453BC) starts each block: the slot's samples, period and
// volume, one repeat used up, then the slot after it (none: the channel
// stops). Fifty times a second ($145438, the vertical blank) every playing
// slot steps its effect script, writes its period and volume to the
// channel and slides them by its steps.
//
// Effect scripts are data in the program ($145F50-$1460A7): pairs of a
// slot field and a value, a wait (ticks to the next entries; 0 ends the
// sound), and two loops. The routines that start an effect write their
// parameters into the script's entries first, as the original does.
namespace interceptor::game {

class SoundDriver {
public:
    // The slots the flight loader fills ($1099CA, $109660): the samples of
    // text/ as they are, copies sharing a sample block, and the two
    // generated blocks (a square wave, the noise).
    explicit SoundDriver(const data::Assets& assets);

    struct Slot {
        bool present{};
        std::uint32_t data{};        // first sample, in the driver's sample memory
        std::uint32_t length{};      // bytes
        std::int32_t period{};       // 16.16; the high word is the Paula period
        std::int32_t volume{};       // 16.16; the high word is 0-63
        std::int32_t repeat{1};      // blocks still to play (-1: for ever)
        std::int32_t repeat_reload{};
        int next{-1};                // the slot that plays after the repeats
        std::int32_t period_step{}, volume_step{};   // added every tick
        std::int32_t period_ticks{}, volume_ticks{}; // steps end after these ticks (0: never)
        std::array<std::int32_t, 2> loops{};         // the script's two loop counters
        std::int32_t wait{};         // ticks to the script's next entries (0: none)
        int script{-1};              // the slot's script (first entry), if it has one
        int position{};              // next entry, counted from the script's start
        template <class A> void persist(A& ar) { ar(present, data, length, period, volume, repeat, repeat_reload, next, period_step, volume_step, period_ticks,
                volume_ticks, loops, wait, script, position); }
    };

    // The effect scripts' first entries.
    enum Script { beep_script = 0, noise_script, gear_script, thump_script, gun_script, script_count };

    static constexpr int slot_count = 40;

    Slot& slot(int n) { return slots_[std::size_t(n)]; }
    const Slot& slot(int n) const { return slots_[std::size_t(n)]; }
    bool has(int n) const { return n >= 0 && n < slot_count && slots_[std::size_t(n)].present; }
    // The slot a channel plays (-1: none).
    int playing(int channel) const { return cells_[std::size_t(channel)]; }

    // $109C58: the channel stops.
    void stop(int channel);
    // $109C7C: slot `n` on the channel at `volume` (0-63), from its start.
    void play(int n, int channel, int volume);
    // Starts slot `n`'s script from its first entry on the next tick.
    void start_script(int n, Script script);
    // The value of a script's entry (the starting routines write their
    // parameters here).
    std::int32_t& script_value(Script script, int entry);

    // $145E7C / $145ECA: `bits` bits of the driver's shift register (the
    // noise block came from it; the game draws its random periods from it).
    std::int32_t random_bits(int bits);

    // The title tune (slots 13-34) loaded.
    bool has_tune() const { return tune_; }
    // $109CE6 / $1015F6: the tune is on (channels 0 and 1 start it at
    // volume 63) or off; while it is on the master volume glides to its
    // target ($13A958) by $4000 a vertical blank.
    void set_tune_playing(bool on) { tune_playing_ = on; }
    bool tune_playing() const { return tune_playing_; }
    void set_master_target(int volume) { master_target_ = std::clamp(volume, 0, 0x3F) << 16; }
    void set_master_volume(int volume); // $14520E at once

    // Adds the four channels to `stereo` (interleaved) at `rate`, the
    // vertical-blank ticks falling between the samples where they come.
    void render(std::span<float> stereo, int rate);
    // One vertical-blank tick ($145438).
    void vertical_blank();

    // Research: every call of `play` and `stop` (and the effects' own
    // records), as the original's routines would have been called.
    std::vector<std::vector<std::int32_t>>* log{};

private:
    struct Entry {
        enum Kind { set_period, set_volume, set_period_step, set_volume_step, set_loop0, set_loop1, wait,
                    loop0, loop1 } kind{};
        std::int32_t value{};
        template <class A> void persist(A& ar) { ar(kind, value); }
    };

    void audio_interrupt(int channel);
    void interrupt(int channel); // forced through INTREQ, or the hardware's own
    void step_script(int channel, Slot& slot);
    void write_registers(int channel, const Slot& slot);
    int copy(int from, int to); // $14592C

    std::vector<std::int8_t> memory_;
    std::array<Slot, slot_count> slots_{};
    std::array<int, 4> cells_{-1, -1, -1, -1};
    std::vector<Entry> entries_;
    std::array<int, script_count> script_start_{};
    std::uint32_t shift_register_{};
    void fade_master();

    int master_volume_ = 0x3F; // $14520E's high word, as the registers use it
    std::int32_t master_ = 0x3F0000, master_target_ = 0x3F0000; // 16.16
    bool tune_ = false, tune_playing_ = false;
    amiga::PaulaChannels paula_;
    double to_blank_ = 0;      // samples left to the next tick
    bool in_interrupt_ = false;
    std::array<bool, 4> pending_{};
public:
    template <class A> void persist(A& ar) { ar(slots_, cells_, entries_, script_start_, shift_register_, master_volume_, master_, master_target_, tune_,
           tune_playing_, paula_, to_blank_, in_interrupt_, pending_); }
};

} // namespace interceptor::game
