#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>

// The Amiga's four sound channels at the register level, for programs that
// write AUDxLC/LEN/PER/VOL and DMACON themselves and rely on how the
// hardware takes them: a channel whose DMA is switched on loads its location
// and length registers and plays that block, and at the block's end loads
// them again (so new values written while a block plays take effect after
// it). Period and volume take effect at once; a channel whose DMA is off is
// silent. Samples are signed 8-bit, in one memory the caller owns; locations
// are offsets into it. Channels 0 and 3 are the left output, 1 and 2 the
// right. Rendered like the other models: samples held, then the A500's
// output low-pass filter.
namespace amiga {

class PaulaChannels {
public:
    static constexpr double pal_clock = 3546895.0;
    void set_memory(std::span<const std::int8_t> memory) { memory_ = memory; }
    void set_location(int channel, std::uint32_t offset) { channels_[std::size_t(channel)].location = offset; } // AUDxLC
    void set_length(int channel, std::uint16_t words) { channels_[std::size_t(channel)].length = words; }   // AUDxLEN
    void set_period(int channel, std::uint16_t period) { channels_[std::size_t(channel)].period = period; } // AUDxPER
    void set_volume(int channel, std::uint16_t volume) { channels_[std::size_t(channel)].volume = volume; } // AUDxVOL
    // DMACON: bit 15 set/clear, bits 0-3 the channels (bit 9, the master
    // enable, is taken as always on).
    void write_dmacon(std::uint16_t value);
    bool dma(int channel) const { return channels_[std::size_t(channel)].on; }
    // Adds `frames` stereo frames at `rate` to `stereo` (interleaved).
    void render(std::span<float> stereo, int rate);

    // The audio interrupt: called when a channel takes its location and
    // length registers for a new block (DMA switched on, or a block ended),
    // so a program can set the registers for the block after it.
    std::function<void(int channel)> block_started;

private:
    struct Channel {
        std::uint32_t location{};
        std::uint16_t length{1}, period{0x1ac}, volume{};
        bool on{};
        std::uint32_t block{}, block_bytes{}; // the block playing
        double position{};                    // bytes into it

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) {
            ar(location, length, period, volume, on, block, block_bytes, position);
        }
    };
    void load(int channel);
    std::span<const std::int8_t> memory_;
    std::array<Channel, 4> channels_{};
    std::array<float, 2> filter_{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(channels_, filter_); }
};

} // namespace amiga
