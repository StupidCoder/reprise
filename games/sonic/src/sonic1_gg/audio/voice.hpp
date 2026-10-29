#pragma once
#include "gamegear/psg.hpp"
#include "sonic1_gg/data/level_data.hpp"

namespace sonic1_gg {
struct StateAccess;

// The sampled "SEGA" voice on the logo screen.
//
// The cartridge plays it with a dedicated routine (23C00) that runs with
// interrupts off and writes the PSG volume registers directly: each packed byte
// holds two 4-bit sample levels, and the routine interpolates three steps
// between them, so every byte yields eight volume changes. The playback rate is
// set by the routine's instruction timing, so this class turns the sample data
// into chip writes stamped with their CPU clock time and plays them through the
// PSG at those exact times.

// One timed chip write: CPU clock since the start, port (0x7F = PSG, 0x06 =
// stereo control) and value.
struct VoiceWrite {
    unsigned clock;
    unsigned port;
    std::uint8_t value;
};

class LogoVoice {
    friend struct StateAccess;

public:
    explicit LogoVoice(std::span<const std::uint8_t> packed);
    std::vector<VoiceWrite> writes;
    unsigned duration{}; // CPU clocks until the routine returns

    void start() {
        position_ = clock_ = 0;
        playing_ = true;
    }

    bool playing() const { return playing_; }

    // Plays the writes due within the next `clocks` CPU clocks and returns the
    // resulting stereo samples.
    std::vector<float> render(gg::Psg&, unsigned clocks);

private:
    std::size_t position_{};
    unsigned clock_{};
    bool playing_{};
};
} // namespace sonic1_gg
