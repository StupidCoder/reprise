#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// The Amiga's four sound channels as a program sees them when it writes the
// custom chips' audio registers itself (AUDxLC/LEN/PER/VOL, DMACON): each
// channel loops its block of signed 8-bit samples at its period (PAL colour
// clock 3 546 895 Hz / period samples per second) and volume (0-64); a new
// block, period or volume takes effect at once (the hardware takes a new
// pointer and length at the end of the playing block, which these programs
// do not depend on). Channels 0 and 3 are the left output, 1 and 2 the
// right. Rendered like amiga::Paula: samples held, then the A500's output
// low-pass filter.
namespace amiga {

class PaulaVoices {
public:
    static constexpr double pal_clock = 3546895.0;

    // AUDxLC/AUDxLEN: the channel's block (copied: programs rewrite theirs).
    void set_samples(int channel, std::span<const std::int8_t> samples);
    void set_period(int channel, int period) { channels_[std::size_t(channel)].period = period; }
    void set_volume(int channel, int volume) { channels_[std::size_t(channel)].volume = volume; }
    // DMACON: all four channels on or off.
    void set_dma(bool on) { dma_ = on; }

    // Adds `frames` stereo frames at `rate` to `stereo` (interleaved).
    void render(std::span<float> stereo, int rate);

private:
    struct Channel {
        std::vector<std::int8_t> samples;
        double position{};
        int period{0x1AC}, volume{};
    };
    std::array<Channel, 4> channels_{};
    std::array<float, 2> filter_{};
    bool dma_{true};
};

} // namespace amiga
