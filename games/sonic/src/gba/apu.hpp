#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Game Boy Advance sound hardware, shared by every GBA game in BlueStreak:
// the four programmable sound generator channels inherited from the Game Boy
// (two square waves, the first with a frequency sweep; a 32-step wave; noise)
// and the two "direct sound" channels that play 8-bit samples the game mixes
// itself. Games drive it with the register writes the cartridge makes
// (offsets 60h-9Fh of the I/O area) and hand over each frame's direct sound
// samples; render() produces interleaved stereo float samples at
// output_rate.
//
// Semantics follow GBATEK (Martin Korth), "GBA Sound Controller". The
// analog stage is simplified: generator outputs are taken as symmetric
// around zero, the output is AC-coupled (a 20 Hz high-pass), and the
// result is not quantised to the bias resolution.
namespace gba {

class Apu {
public:
    static constexpr int output_rate = 48000;
    static constexpr std::uint32_t clock_rate = 16777216;

    // The registers 04000060-040000A7 by their offset from 04000000.
    void write8(std::uint32_t offset, std::uint8_t value);
    std::uint8_t read8(std::uint32_t offset) const;

    // One frame of direct sound samples for FIFO A and B, played at
    // `rate` samples per second from now on.
    void queue_direct(std::span<const std::int8_t> a, std::span<const std::int8_t> b, double rate);

    // Advances the sound by `cycles` CPU clocks.
    std::vector<float> render(std::uint32_t cycles);

private:
    struct Envelope {
        int volume{};    // 0-15
        int step_time{}; // in 64 Hz ticks, 0: no change
        bool rise{};
        int counter{};

        void start(std::uint8_t reg) {
            volume = reg >> 4;
            rise = reg & 8;
            step_time = reg & 7;
            counter = step_time;
        }

        void tick() {
            if (step_time == 0 || --counter > 0) {
                return;
            }
            counter = step_time;
            if (rise && volume < 15) {
                ++volume;
            } else if (!rise && volume > 0) {
                --volume;
            }
        }
    };

    struct Square {
        bool on{};
        int length{}; // ticks left (256 Hz)
        bool length_on{};
        Envelope envelope;
        double phase{};  // 0-1 within one period
        int frequency{}; // the 11-bit register value
        // Sweep (channel 1).
        int sweep_counter{};
        bool sweep_on{};
        int shadow{};
    };

    struct Wave {
        bool on{};
        int length{};
        bool length_on{};
        int frequency{};
        double position{}; // in samples (32 or 64)
    };

    struct Noise {
        bool on{};
        int length{};
        bool length_on{};
        Envelope envelope;
        std::uint16_t lfsr = 0x7FFF;
        double clock{};
    };

    void trigger_square(int n);
    void trigger_wave();
    void trigger_noise();
    void sequencer_tick();
    std::array<int, 2> psg_sample();

    double square_frequency(const Square& s) const { return 131072.0 / (2048 - s.frequency); }

    // 60-A7; SOUNDBIAS (88) starts at 0200h.
    std::array<std::uint8_t, 0x48> regs_ = [] {
        std::array<std::uint8_t, 0x48> r{};
        r[0x89 - 0x60] = 0x02;
        return r;
    }();
    std::array<std::uint8_t, 32> wave_ram_{}; // two banks of 16 bytes
    std::array<Square, 2> square_{};
    Wave wave_;
    Noise noise_;
    double sequencer_{};         // 512 Hz steps owed
    double owed_{};              // output samples owed
    std::array<double, 2> dc_{}; // the constant part followed by the output's high-pass
    int sequencer_step_{};

    struct Direct {
        std::vector<std::int8_t> samples;
        double position{};
        double rate = 1;
    };

    std::array<Direct, 2> direct_{};
};

} // namespace gba
