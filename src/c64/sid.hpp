#pragma once
#include <array>
#include <cstdint>
#include <span>

// The C64's sound chip (SID 6581) as a sound output service: games set its
// registers the way the original programs do, and the SID turns them into
// samples. Three voices (triangle, sawtooth, pulse and noise waveforms with
// ring modulation and sync, ADSR envelopes), a state-variable filter and the
// master volume, following the published register behaviour. It is an
// approximation of the analogue chip (filter curve, combined waveforms).
namespace c64 {

class Sid {
public:
    static constexpr double clock_hz = 985248.0; // PAL

    void write(int reg, std::uint8_t value);
    std::uint8_t read_register(int reg) const { return regs_[std::size_t(reg & 31)]; }
    void reset();

    // Renders mono samples at `sample_rate`, advancing the chip's clock.
    void render(std::span<float> out, int sample_rate);

private:
    struct Voice {
        std::uint32_t accumulator{};  // 24-bit phase
        std::uint32_t noise{0x7FFFF8}; // 23-bit noise shift register
        std::uint32_t last_bit19{};
        // Envelope.
        enum class Stage { attack, decay_sustain, release } stage{Stage::release};
        int level{};       // 0-255
        int rate_counter{};
        int exponential_counter{};
        bool gate{};
    };

    void clock(int cycles);
    int voice_output(int v) const; // signed 12-bit waveform * envelope

    std::array<std::uint8_t, 32> regs_{};
    std::array<Voice, 3> voices_{};
    double cycle_credit_{};
    // Filter state.
    double low_{}, band_{};
};

} // namespace c64
