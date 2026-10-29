#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gg {
// The Game Gear's sound chip (SN76489-compatible PSG): three square-wave tone
// channels and one noise channel, each with 4-bit attenuation, plus the Game
// Gear's per-channel left/right stereo switch. Games drive it with the same
// byte writes the cartridge sends to port 0x7F; render() produces interleaved
// stereo float samples at `sample_rate`, band-limited by a windowed-sinc filter.
class Psg {
public:
    static constexpr unsigned clock_rate = 3579545, sample_rate = 48000;
    void write(std::uint8_t value);

    void stereo(std::uint8_t mask) { stereo_ = mask; }

    // Advances the chip by `master_clocks` CPU clocks (3.579545 MHz).
    std::vector<float> render(unsigned master_clocks);

    // Save states: passes every internal field, in a fixed order, to the
    // game's serializer (`a(fields...)`), which reads or writes them.
    template <class A>
    void serialize(A& a) {
        a(period_, counter_, high_, attenuation_, latch_, noise_, noise_counter_, stereo_,
          noise_high_, shift_, clock_remainder_, sample_phase_, history_position_, history_,
          previous_input_, previous_output_);
    }

private:
    std::array<unsigned, 3> period_{}, counter_{};
    std::array<bool, 3> high_{};
    std::array<unsigned, 4> attenuation_{15, 15, 15, 15};
    unsigned latch_{}, noise_{4}, noise_counter_{}, stereo_{255};
    bool noise_high_{};
    std::uint16_t shift_{0x8000};
    unsigned clock_remainder_{}, sample_phase_{}, history_position_{};
    std::array<std::array<float, 2>, 128> history_{};
    std::array<float, 2> previous_input_{}, previous_output_{};
    void clock();
    void shift_noise();
};
} // namespace gg
