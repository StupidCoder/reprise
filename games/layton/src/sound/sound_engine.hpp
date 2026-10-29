#pragma once
// Native sound engine: schedules the recovered driver's sound-thread
// iterations against simulated hardware time and mixes the hardware model.
// One iteration per expiry of ARM7 alarm 1 (set up by the driver's 0x02008038
// as 0x1474 x 64 = 335,104 system clocks; the ARM9 thread 0x02008130 waits for
// it, and a pause stops it), one output sample per 1,024 clocks.
// The channel status the driver reads (SDK 0x02021c74) is the busy state the
// ARM7 recorded after the previous iteration's commands (ARM7 0x03801c8c).
// ARM7 alarm 0 (set up by command 0x12, started and stopped by the alarm bits
// of 0x0c/0x0d) paces SAD stream refills: each expiry wakes the ARM9 stream
// thread (0x020060e4), which decodes the half the channels just finished and
// writes it to the ring. Picarat runs that pass kRefillLatency clocks after
// the expiry rather than on it: the ARM9 thread can only write once it has
// woken and decoded, by which time the channels are inside the other half.
// Refilling on the expiry itself overwrote the last samples of the half the
// channels were still playing (they start two timer steps late, DsSound), a
// click every refill (960 frames for 0xb2 streams).
#include "sound/ds_sound.hpp"
#include "sound/sound_object.hpp"
#include <memory>
#include <span>
#include <vector>

namespace picarat {

class SoundEngine final : private dse::SoundCommands {
public:
    explicit SoundEngine(std::filesystem::path assets);
    explicit SoundEngine(SoundLayout layout);
    ~SoundEngine() override;

    GameSound& sound() { return *object_; }

    // Mixes interleaved stereo frames at the hardware rate (32,728.5 Hz),
    // running driver iterations at their scheduled clocks.
    void render(std::span<std::int16_t> stereo);

    std::uint64_t iterations() const { return iterations_; }

    // Advances hardware time without output (research probes).
    void advance(std::uint64_t clocks);

private:
    DsSound hardware_;
    std::unique_ptr<SoundObject> object_;
    std::uint64_t clocks_ = 0, iterations_ = 0;
    std::uint16_t status_ = 0;

    struct Alarm {
        std::uint32_t tick = 0, period = 0;
        bool running = false;
        std::uint64_t next = 0;
    };

    Alarm alarms_[2]; // 0: stream refills, 1: sound thread (64-clock units)
    // Stream thread passes due (alarm 0 expiry + latency), earliest first.
    // Hypothesis: 64 output samples (about 2 ms) for the wake and the decode;
    // anything from a few samples to most of a half avoids the overlap.
    static constexpr std::uint64_t kRefillLatency = 64 * DsSound::clocksPerSample;
    std::vector<std::uint64_t> refills_;
    std::vector<std::int16_t> scratch_;

    void startTimers(std::uint32_t channels, std::uint32_t, std::uint32_t alarms,
                     std::uint32_t) override {
        hardware_.startChannels(channels);
        for (unsigned i = 0; i < 2; ++i) {
            if (alarms >> i & 1) {
                alarms_[i].running = true;
                alarms_[i].next = clocks_ + std::uint64_t(alarms_[i].tick) * 64;
            }
        }
        if (alarms & 1) {
            refills_.clear(); // a new stream's alarm; earlier passes are void
        }
    }

    void stopTimers(std::uint32_t channels, std::uint32_t, std::uint32_t alarms,
                    std::uint32_t flags) override {
        hardware_.stopChannels(channels, flags & 1);
        for (unsigned i = 0; i < 2; ++i) {
            if (alarms >> i & 1) {
                alarms_[i].running = false;
            }
        }
        if (alarms & 1) {
            refills_.clear();
        }
    }

    void setupAlarm(unsigned alarm, std::uint32_t tick, std::uint32_t period,
                    std::uint8_t) override {
        if (alarm < 2) {
            alarms_[alarm].tick = tick;
            alarms_[alarm].period = period;
        }
    }

    void setupPcm(unsigned channel, unsigned format, const std::uint8_t* data, unsigned repeat,
                  unsigned loopStart, unsigned loopLength, unsigned volume, unsigned shift,
                  unsigned timer, unsigned pan) override;

    void setupPsg(unsigned channel, unsigned duty, unsigned volume, unsigned shift, unsigned timer,
                  unsigned pan) override {
        hardware_.setupPsg(channel, duty, volume, shift, timer, pan);
    }

    void setupNoise(unsigned channel, unsigned volume, unsigned shift, unsigned timer,
                    unsigned pan) override {
        hardware_.setupNoise(channel, volume, shift, timer, pan);
    }

    void setTimer(std::uint32_t channels, unsigned timer) override {
        hardware_.setTimer(channels, timer);
    }

    void setVolume(std::uint32_t channels, unsigned volume, unsigned shift) override {
        hardware_.setVolume(channels, volume, shift);
    }

    void setPan(std::uint32_t channels, unsigned pan) override { hardware_.setPan(channels, pan); }

    void setMasterVolume(unsigned volume) override { hardware_.setMasterVolume(volume); }

    std::uint16_t channelStatus() override { return status_; }
};

} // namespace picarat
