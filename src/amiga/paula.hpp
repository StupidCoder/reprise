#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

// The Amiga's four sound channels as a program sees them through
// audio.device: each channel plays the writes queued on it one after the
// other. A write is a block of signed 8-bit samples played `cycles` times
// (0: until the channel is flushed) at a period of the PAL colour clock
// (3 546 895 Hz / period samples per second) and a volume of 0-64.
// Channels 0 and 3 are the left output, 1 and 2 the right one.
//
// This is a model of the hardware the original drove, rendered to the
// host's sample rate: each sample is held until the next one (as the DAC
// does), followed by the A500's fixed output low-pass filter.
namespace amiga {

class Paula {
public:
    static constexpr double pal_clock = 3546895.0;

    struct Write {
        const LoadedImage* image{}; // where the samples are
        std::uint32_t data{};       // address of the first sample
        // Or the samples themselves (a sample file), `data` an offset into them.
        std::span<const std::int8_t> samples{};
        std::uint32_t length{};     // bytes
        std::uint16_t period{}, volume{};
        int cycles{};               // 0: repeat until flushed
        bool set_period_volume{};   // ADIOF_PERVOL: take period and volume from this write
        int tag{};                  // returned to `done` when the write has played
    };

    // Called when a write has played all its cycles (audio.device replies
    // it), with the channel and the write's tag.
    using Done = std::function<void(int channel, int tag)>;

    void write(int channel, const Write&);                                    // CMD_WRITE
    void set_period_volume(int channel, std::uint16_t period, std::uint16_t volume); // ADCMD_PERVOL
    void flush(int channel);                                                  // CMD_FLUSH
    void reset();

    // Research: when set, every request is appended here.
    struct Request {
        int command{};  // 3 write, 8 flush, 12 period and volume
        int channel{};
        Write write{};  // period and volume also for command 12
    };
    std::vector<Request>* log{};

    // Adds `frames` stereo frames at `rate` to `stereo` (interleaved).
    void render(std::span<float> stereo, int rate, const Done& done);

private:
    struct Channel {
        std::deque<Write> queue; // front: playing
        double position{};       // bytes into the playing write
        int played{};            // cycles done
        std::uint16_t period{0x1AC}, volume{};
        bool started{};          // the front write has taken its period and volume
    };
    void start_front(Channel&);

    std::array<Channel, 4> channels_{};
    std::array<float, 2> filter_{};
};

} // namespace amiga
