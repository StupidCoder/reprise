#pragma once
// Native DS sound-hardware service (platform side of the sequenced-audio
// boundary). The ROM's ARM9 driver issues SDK sound commands; the ROM's ARM7
// SDK sound driver (dispatcher ARM7 0x03801f1c) writes them to the sixteen
// hardware channels. This class implements:
//   * the ARM7 command semantics actually reached by this game
//     (0x0c start, 0x0d stop, 0x0e PCM setup, 0x0f PSG setup, 0x10 noise
//     setup, 0x13 timer, 0x14 volume, 0x15 pan, 0x17 master volume), with the
//     ARM7 pan-override and surround adjustment observed inactive (-1 / 0);
//   * a hardware mixer written from general DS hardware semantics (GBATEK
//     level): PCM8/PCM16/IMA-ADPCM/PSG/noise channels, 0..127 volume with
//     divider, 0..127 pan, master volume, one-shot and looping playback.
// It is a native service, not recovered game logic and not an emulator of the
// ARM7. Time is counted in system clocks (33,513,982 Hz): one output sample
// every 1024 clocks (32,728.5 Hz) and channel timers at clock/2.
// Known simplifications: no FIFO prefetch/start delay, no capture units, no
// 10-bit DAC quantisation or bias; output is 16-bit before the DAC stage.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace picarat {

class DsSound {
public:
    static constexpr std::uint32_t systemClock = 33513982;
    static constexpr std::uint32_t clocksPerSample = 1024;
    // Output stage: true (hardware) keeps the 10 bits that reach the DAC
    // (bias 0x200, clip 0..0x3ff); false returns the 16-bit pre-DAC value.
    bool dac10 = true;

    enum Format : std::uint8_t { Pcm8 = 0, Pcm16 = 1, Adpcm = 2, Psg = 3 };

    void reset();
    // Command 0x0c / 0x0d: bit masks of channels; capture and alarm masks are
    // accepted for completeness (capture is not modelled, alarms are owned by
    // the caller's tick scheduler).
    void startChannels(std::uint32_t channels);
    void stopChannels(std::uint32_t channels, bool hold);
    // Command 0x0e. `data` points at the sample bytes; `limit` bounds the
    // memory region that contains them (reads beyond it return silence).
    void setupPcm(unsigned channel, unsigned format, const std::uint8_t* data,
                  const std::uint8_t* limit, unsigned repeat, unsigned loopStart,
                  unsigned loopLength, unsigned volume, unsigned shift, unsigned timer,
                  unsigned pan);
    void setupPsg(unsigned channel, unsigned duty, unsigned volume, unsigned shift, unsigned timer,
                  unsigned pan);
    void setupNoise(unsigned channel, unsigned volume, unsigned shift, unsigned timer,
                    unsigned pan);
    void setTimer(std::uint32_t channels, unsigned timer);
    void setVolume(std::uint32_t channels, unsigned volume, unsigned shift);
    void setPan(std::uint32_t channels, unsigned pan);

    void setMasterVolume(unsigned volume) { masterVolume_ = volume & 0x7f; }

    // Research: every register write the commands stand for (address, size,
    // value), in the order the ARM7 driver's routines write them; data
    // addresses are host pointers.
    std::function<void(std::uint32_t, unsigned, std::uint32_t)> registerTrace;

    // SOUNDxCNT busy bits (bit 31 of every channel), as the ARM7 driver's
    // status routine (ARM7 0x03801c8c) reads them.
    std::uint16_t busy() const;
    // Mixes `frames` stereo samples (interleaved) and advances hardware time.
    void render(std::span<std::int16_t> stereo);
    void advance(unsigned frames); // advances time without producing output

private:
    struct Channel {
        bool busy = false;
        std::uint8_t format = 0, repeat = 0, volume = 0, shift = 0, pan = 0x40, duty = 0;
        const std::uint8_t* data = nullptr;
        const std::uint8_t* limit = nullptr;
        std::uint32_t loopStart = 0, loopLength = 0; // words
        std::uint16_t reload = 0;                    // TMR register (written as -timer)
        // Playback state.
        std::uint32_t counter = 0, position = 0; // timer ticks, byte (or nibble) position
        std::int32_t sample = 0;
        std::int32_t adpcmValue = 0, adpcmIndex = 0, adpcmLoopValue = 0, adpcmLoopIndex = 0;
        std::uint16_t noise = 0x7fff;
        std::uint8_t psgStep = 0;
        // Timer overflows without a fetch after start: the first sample comes
        // with overflow 1 (PSG/noise), 3 (PCM) or 11 (ADPCM) (GBATEK "starts
        // after 1/3/11 samples"; replay-matched against the emulator).
        std::uint8_t delay = 0;
        void start();
        void step();
        std::uint8_t byte(std::uint32_t offset) const;
        std::uint32_t control() const; // SOUNDxCNT
    };

    void trace(unsigned channel, unsigned offset, unsigned size, std::uint32_t value) const;

    std::array<Channel, 16> channels_{};
    unsigned masterVolume_ = 0x7f;
    void mix(std::int32_t& left, std::int32_t& right, bool produce);
};

} // namespace picarat
