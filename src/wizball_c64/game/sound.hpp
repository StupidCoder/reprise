#pragma once
#include "c64/sid.hpp"
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/game/music.hpp"
#include "wizball_c64/game/random_source.hpp"
#include <array>
#include <cstdint>
#include <vector>

// Wizball's sound effects ($4553 start, $B960 the per-frame voice routine).
// An effect is a 25-byte record ($BACA: one per effect number): which voice
// slot it uses, its priority, two gate times, waveform, envelope, a start
// frequency that sweeps up and down by a step for given times, and a pulse
// width that does the same. Starting an effect copies its record into the
// slot, which then also holds the running counters. Effect $18 (the wiz's
// hum) has six bytes re-rolled from the random numbers each time it starts,
// in the record itself. The tunes are not rebuilt yet.
namespace wizball::game {

class Sound {
public:
    Sound(const data::GameImage& image, RandomSource& random);

    // $4553: start effect `number` unless its voice slot is busy with one of
    // higher priority (a lower priority number wins; equal replaces).
    void start_effect(int number);
    // The raster interrupt's sound work once a frame ($B90C): the volume
    // fade, then the effects on all voices (mode 0), on the lower two with a
    // tune on the third (mode 1), or a tune on all three (mode 2).
    void frame();
    // The other music interrupts of a frame ($B8F3, $B8FC, $B903: with a
    // tempo of 1 or 2 the tunes tick two or four times a frame).
    void music_tick();
    // The three extra music interrupts by their handler ($B903 line 63 and
    // $B8F3 line 255 at speed 2, $B8FC line 229 at speed 1 or 2).
    void extra_interrupt(std::uint16_t handler);
    void silence(); // $BAAD
    // The game rewrites a byte of an effect's record (the clear moves effect
    // 5 to another voice slot: $6479, $660F).
    void patch_effect(int effect, int byte, std::uint8_t value) {
        records_[std::size_t(effect)][std::size_t(byte)] = value;
    }
    std::uint8_t volume() const { return fade_level_; } // $45CF
    void reset_music() { music_.reset(sid_); } // $47D0
    void start_tune(int tune, int mode, int speed = -1); // $45F1 with $B8F1 (and $B8F2)
    void set_mode(int mode) { music_mode_ = std::uint8_t(mode); }
    int mode() const { return music_mode_; }
    int speed() const { return music_speed_; }
    void set_speed(int speed) { music_speed_ = std::uint8_t(speed); }
    // $45BD, once a pass: when the third voice's tune has ended, the
    // effects get all voices back.
    void check_tune();
    // $7DBE: the death cuts a tune off (the filter mode bits stay).
    void stop_tune() {
        music_mode_ = 0;
        sid_.write(0x17, 0);
    }
    void fade(int direction) { fade_direction_ = std::uint8_t(direction); } // $45CE: $FF out, 1 in
    const Music& music() const { return music_; }
    Music& music() { return music_; }

    bool slot_busy(int slot) const { return slots_[std::size_t(slot)].gate1 != 0; } // $0402 + $20 * slot
    c64::Sid& sid() { return sid_; }

    struct Slot {
        std::uint8_t voice{}, priority{}, gate1{}, gate2{}, waveform{}, attack_decay{}, started{};
        std::uint16_t frequency{}, frequency_step{};
        std::uint8_t up_time{}, down_time{};
        std::uint16_t pulse{}, pulse_step{};
        std::uint8_t pulse_up{}, pulse_down{};
        std::uint8_t rising{}, count{}, pulse_rising{}, pulse_count{};
        std::uint8_t sustain_release{}, spare{};
    };
    const std::array<Slot, 3>& slots() const { return slots_; }
    // For checks against the original: a slot set from its 25 bytes.
    void set_slot(int slot, const std::uint8_t* bytes);

private:
    Slot read_record(int number) const;
    void tick(int slot, int sid_voice);

    const data::GameImage& image_;
    RandomSource& random_;
    std::array<Slot, 3> slots_{};
    std::array<std::vector<std::uint8_t>, 0x1A> records_{};
    std::uint8_t last_effect_{}, toggle_{}; // $BB02, $BB03
    std::uint8_t music_mode_{}; // $B8F1: 0 effects on all voices, 1 a tune on the third, 2 tunes only
    std::uint8_t music_speed_{}; // $B8F2
    std::uint8_t fade_direction_{}, fade_level_{0x0F}; // $45CE, $45CF
    Music music_;
    c64::Sid sid_;
};

} // namespace wizball::game
