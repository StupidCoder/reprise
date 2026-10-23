#include "c64/sid.hpp"
#include <algorithm>
#include <cmath>

namespace c64 {
namespace {
// Clock cycles per envelope step for the 16 attack/decay/release settings
// (from the chip's documented rate table).
constexpr int rate_periods[16] = {9,   32,  63,  95,   149,  220,  267,  313,
                                  392, 977, 1954, 3126, 3907, 11720, 19532, 31251};
} // namespace

void Sid::reset() {
    regs_ = {};
    voices_ = {};
    low_ = band_ = 0;
}

void Sid::write(int reg, std::uint8_t value) {
    reg &= 31;
    if (reg > 0x18) {
        return;
    }
    if (reg % 7 == 4 && reg < 21) {
        auto& v = voices_[std::size_t(reg / 7)];
        const bool gate = value & 1;
        if (gate && !v.gate) {
            v.stage = Voice::Stage::attack;
            v.rate_counter = 0;
        } else if (!gate && v.gate) {
            v.stage = Voice::Stage::release;
            v.rate_counter = 0;
        }
        v.gate = gate;
        if (value & 8) { // test bit: hold the oscillator at zero, reset noise
            v.accumulator = 0;
            v.noise = 0x7FFFF8;
        }
    }
    regs_[std::size_t(reg)] = value;
}

void Sid::clock(int cycles) {
    for (int v = 0; v < 3; ++v) {
        auto& voice = voices_[std::size_t(v)];
        const auto* r = &regs_[std::size_t(v * 7)];
        const std::uint32_t frequency = std::uint32_t(r[0] | r[1] << 8);
        if (!(r[4] & 8)) {
            // The noise register shifts each time bit 19 of the phase rises.
            const std::uint64_t before = voice.accumulator;
            const std::uint64_t after = before + std::uint64_t(frequency) * std::uint64_t(cycles);
            const std::uint64_t risings = ((after + 0x80000) >> 20) - ((before + 0x80000) >> 20);
            for (std::uint64_t i = 0; i < risings; ++i) {
                const std::uint32_t bit = ((voice.noise >> 22) ^ (voice.noise >> 17)) & 1;
                voice.noise = ((voice.noise << 1) | bit) & 0x7FFFFF;
            }
            voice.accumulator = std::uint32_t(after & 0xFFFFFF);
        }
        // Envelope.
        const int attack = r[5] >> 4, decay = r[5] & 15, sustain = (r[6] >> 4) * 17,
                  release = r[6] & 15;
        int period = voice.stage == Voice::Stage::attack           ? rate_periods[attack]
                     : voice.stage == Voice::Stage::decay_sustain ? rate_periods[decay]
                                                                   : rate_periods[release];
        voice.rate_counter += cycles;
        while (voice.rate_counter >= period) {
            voice.rate_counter -= period;
            if (voice.stage == Voice::Stage::attack) {
                if (++voice.level >= 255) {
                    voice.level = 255;
                    voice.stage = Voice::Stage::decay_sustain;
                    period = rate_periods[decay];
                }
                continue;
            }
            // Decay and release fall exponentially (slower at low levels).
            const int divider = voice.level > 93 ? 1 : voice.level > 54 ? 2 : voice.level > 26 ? 4
                              : voice.level > 14 ? 8 : voice.level > 6 ? 16 : 30;
            if (++voice.exponential_counter < divider) {
                continue;
            }
            voice.exponential_counter = 0;
            if (voice.stage == Voice::Stage::decay_sustain) {
                if (voice.level > sustain) {
                    --voice.level;
                }
            } else if (voice.level > 0) {
                --voice.level;
            }
        }
    }
}

int Sid::voice_output(int v) const {
    const auto& voice = voices_[std::size_t(v)];
    const auto* r = &regs_[std::size_t(v * 7)];
    const int control = r[4];
    const std::uint32_t acc = voice.accumulator;
    int wave = 0xFFF; // combined by AND, as the chip roughly does
    bool any = false;
    if (control & 0x10) { // triangle (ring-modulated by the previous voice)
        std::uint32_t msb = acc & 0x800000;
        if (control & 4) {
            msb ^= voices_[std::size_t((v + 2) % 3)].accumulator & 0x800000;
        }
        const int tri = int(((msb ? ~acc : acc) >> 11) & 0xFFF);
        wave &= tri;
        any = true;
    }
    if (control & 0x20) { // sawtooth
        wave &= int(acc >> 12);
        any = true;
    }
    if (control & 0x40) { // pulse
        const int width = (r[2] | (r[3] & 15) << 8);
        wave &= int(acc >> 12) >= width ? 0xFFF : 0;
        any = true;
    }
    if (control & 0x80) { // noise
        const auto n = voice.noise;
        const int out = int(((n >> 20) & 1) << 11 | ((n >> 18) & 1) << 10 | ((n >> 14) & 1) << 9 |
                            ((n >> 11) & 1) << 8 | ((n >> 9) & 1) << 7 | ((n >> 5) & 1) << 6 |
                            ((n >> 2) & 1) << 5 | (n & 1) << 4);
        wave &= out;
        any = true;
    }
    if (!any) {
        wave = 0x800;
    }
    return (wave - 0x800) * voice.level / 255;
}

void Sid::render(std::span<float> out, int sample_rate) {
    const double cycles_per_sample = clock_hz / sample_rate;
    for (auto& sample : out) {
        cycle_credit_ += cycles_per_sample;
        const int cycles = int(cycle_credit_);
        cycle_credit_ -= cycles;
        clock(cycles);

        const int routing = regs_[0x17] & 15, mode = regs_[0x18];
        double filtered_in = 0, direct = 0;
        for (int v = 0; v < 3; ++v) {
            const double o = voice_output(v) / 2048.0;
            if (v == 2 && (mode & 0x80) && !(routing & 4)) {
                continue; // voice 3 switched off
            }
            ((routing >> v) & 1 ? filtered_in : direct) += o;
        }
        // State-variable filter with the cutoff mapped to roughly 30 Hz-12 kHz.
        const int cutoff = (regs_[0x15] & 7) | regs_[0x16] << 3;
        const double hz = 30.0 + cutoff * 5.8;
        const double f = std::min(0.9, 2.0 * std::sin(3.14159265 * hz / sample_rate));
        const double q = 1.0 / (0.707 + (regs_[0x17] >> 4) / 15.0 * 1.3);
        const double high = filtered_in - low_ - q * band_;
        band_ += f * high;
        low_ += f * band_;
        double filtered = 0;
        if (mode & 0x10) filtered += low_;
        if (mode & 0x20) filtered += band_;
        if (mode & 0x40) filtered += high;
        const double volume = (mode & 15) / 15.0;
        sample = float(std::clamp((direct + filtered) * volume * 0.25, -1.0, 1.0));
    }
}

} // namespace c64
