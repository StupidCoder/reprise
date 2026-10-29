#pragma once
// The first adventure's streamed audio: voice lines, the movies' sound and
// one piece of music (stream/*.SAD, format 0x72). The ARM9 stream library
// (02074654..02075bb0) reads the file a piece at a time, decodes it into a
// ring of two halves per channel that two sound channels loop over as
// 16-bit samples, and refills one half on every driver alarm (1,024 samples
// at 32,728 Hz). This object is the library's decoder side; the sound
// object (Adventure1Sound) sends the channel and alarm commands and calls
// refill() when the alarm fires.
#include "sound/sad_audio.hpp"
#include <array>
#include <span>
#include <vector>

namespace picarat {

class Adventure1Stream {
public:
    // 02004644 / 02074d18: the ring's size, its half per channel and the
    // part refilled at a time.
    static constexpr std::size_t kRing = 0x2000, kChannelRing = kRing / 2,
                                 kRefill = kChannelRing / 2;

    // `steps`: the ARM9 program's 89-entry step table (020c92d8).
    explicit Adventure1Stream(std::vector<std::int16_t> steps);

    // 02075000: read the header and start at the data. The decoder state
    // is reset from the header except the filter's last output, which the
    // previous stream left (the game allocates the decoder again where the
    // last one was; measured: every voice line after the first starts with
    // the previous line's last filtered value).
    void open(Bytes file);
    // 02074dfc: the ring is cleared before the channels start.
    void clearRing();
    // 020753f4: one alarm's refill. False when the stream has ended: after
    // the last data, two more halves are cleared, and the third alarm stops
    // the channels.
    bool refill();

    unsigned channels() const { return header_.channels; }

    // The part of the ring channel `i` of the pair plays (mono: both play
    // the first channel's part).
    std::span<const std::uint8_t> channelRing(unsigned i) const;

    const SadHeader& header() const { return header_; }

private:
    std::vector<std::int16_t> steps_;
    Bytes file_;
    SadHeader header_;
    SadFilter filter_;
    std::array<SadChannelState, 2> state_{};
    std::array<std::uint8_t, kRing> ring_{};
    std::vector<std::uint8_t> read_;
    std::uint32_t position_ = 0, readSize_ = 0;
    unsigned half_ = 0, countdown_ = 0;
    bool ending_ = false;
};

} // namespace picarat
