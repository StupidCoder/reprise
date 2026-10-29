#include "gamegear/psg.hpp"
#include <cmath>
#include <numbers>

namespace gg {
namespace {
constexpr unsigned taps = 64, phases = 256;
// A fractional-delay low-pass filter resamples the chip's clock to 48 kHz.
// Filtering before decimation avoids the harsh aliased upper harmonics of
// simply evaluating a square wave once per output sample.
const auto filters = [] {
    std::array<std::array<float, taps>, phases> table{};
    const double cutoff = 20000.0 / (double(Psg::clock_rate) / 16);
    for (unsigned phase = 0; phase < phases; ++phase) {
        double sum = 0;
        for (unsigned j = 0; j < taps; ++j) {
            const double x = double(j) - 31.0 - double(phase) / phases;
            const double sinc = std::abs(x) < 1e-12 ? 2 * cutoff
                                                    : std::sin(2 * std::numbers::pi * cutoff * x) /
                                                          (std::numbers::pi * x);
            const double window = 0.42 - 0.5 * std::cos(2 * std::numbers::pi * j / (taps - 1)) +
                                  0.08 * std::cos(4 * std::numbers::pi * j / (taps - 1));
            table[phase][j] = float(sinc * window);
            sum += table[phase][j];
        }
        for (auto& coefficient : table[phase]) {
            coefficient /= sum;
        }
    }
    return table;
}();
const auto levels = [] {
    std::array<float, 16> values{};
    for (unsigned i = 0; i < 15; ++i) {
        values[i] = 0.18f * std::pow(10.0f, -float(i) / 10);
    }
    return values;
}();
} // namespace

void Psg::write(std::uint8_t value) {
    // SN76489 serial latch/data protocol, including data writes to volume and
    // noise registers. Game Gear has the Sega 16-bit noise shift register.
    if (value & 128) {
        latch_ = (value >> 4) & 7;
    }
    const unsigned channel = latch_ >> 1;
    if (latch_ & 1) {
        attenuation_[channel] = value & 15;
    } else if (channel < 3) {
        if (value & 128) {
            period_[channel] = (period_[channel] & 0x3f0) | (value & 15);
        } else {
            period_[channel] = (period_[channel] & 15) | ((value & 63) << 4);
        }
    } else {
        noise_ = value & 7;
        shift_ = 0x8000;
    }
}

void Psg::shift_noise() {
    const unsigned feedback = (noise_ & 4) ? ((shift_ ^ (shift_ >> 3)) & 1) : (shift_ & 1);
    shift_ = (shift_ >> 1) | (feedback << 15);
}

void Psg::clock() {
    bool tone2_rise = false;
    for (unsigned i = 0; i < 3; ++i) {
        if (counter_[i]) {
            --counter_[i];
        }
        if (!counter_[i]) {
            counter_[i] = period_[i];
            high_[i] = !high_[i];
            if (i == 2 && high_[i]) {
                tone2_rise = true;
            }
        }
    }
    if ((noise_ & 3) == 3) {
        if (tone2_rise) {
            shift_noise();
        }
    } else {
        if (noise_counter_) {
            --noise_counter_;
        }
        if (!noise_counter_) {
            noise_counter_ = 16u << (noise_ & 3);
            noise_high_ = !noise_high_;
            if (noise_high_) {
                shift_noise();
            }
        }
    }
    std::array<float, 2> mixed{};
    for (unsigned i = 0; i < 4; ++i) {
        const bool high = i == 3 ? (shift_ & 1) : (period_[i] <= 1 || high_[i]);
        const float amplitude = high ? levels[attenuation_[i]] : 0;
        if (stereo_ & (16u << i)) {
            mixed[0] += amplitude;
        }
        if (stereo_ & (1u << i)) {
            mixed[1] += amplitude;
        }
    }
    history_position_ = (history_position_ + 1) & 127;
    history_[history_position_] = mixed;
}

std::vector<float> Psg::render(unsigned clocks) {
    std::vector<float> samples;
    samples.reserve((clocks * std::uint64_t(sample_rate) / clock_rate + 1) * 2);
    clock_remainder_ += clocks;
    while (clock_remainder_ >= 16) {
        clock_remainder_ -= 16;
        clock();
        sample_phase_ += sample_rate * 16;
        if (sample_phase_ < clock_rate) {
            continue;
        }
        sample_phase_ -= clock_rate;
        const auto& filter = filters[sample_phase_ * std::uint64_t(phases) / (sample_rate * 16)];
        std::array<float, 2> sample{};
        for (unsigned j = 0; j < taps; ++j) {
            for (unsigned side = 0; side < 2; ++side) {
                sample[side] += history_[(history_position_ - j) & 127][side] * filter[j];
            }
        }
        for (unsigned side = 0; side < 2; ++side) {
            // A gentle DC blocker models the output coupling and keeps rests
            // centred. The analogue speaker/amplifier is not being emulated.
            float output = sample[side] - previous_input_[side] + 0.995f * previous_output_[side];
            previous_input_[side] = sample[side];
            previous_output_[side] = output;
            samples.push_back(output);
        }
    }
    return samples;
}
} // namespace gg
