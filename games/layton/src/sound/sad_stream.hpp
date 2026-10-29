#pragma once
#include "sound/sad_audio.hpp"

namespace picarat {
// Plays one SAD file as a stream: the file is read a fixed number of bytes
// at a time (the driver's request size: 512 for 0xb2, 1024 for 0xb4 with the
// usual ring) and decoded into samples, looping if the file asks for it.
//
// Recovered refill/reset subset of resident ARM9 02005478 / 02006bdc.
// Native transport supplies refill requests; this object neither emulates the
// ARM7 nor estimates playback completion from an arbitrary frame counter.
class SadStream {
public:
    void open(Bytes file, std::uint32_t request = 512);
    std::vector<std::int16_t> refill(); // interleaved output, one source request

    const SadHeader& header() const { return header_; }

    std::array<SadChannelState, 2> channels{};
    std::vector<std::uint8_t> scratch; // Shared read buffer retains unread tail bytes.
    std::uint32_t request = 512;       // source bytes per refill (stream +0x1e4)
    std::uint32_t cursor = 0, bytesRead = 0;
    bool emptyRefillSeen = false, playing = false;
    unsigned loopCount = 0;

private:
    Bytes file_;
    SadHeader header_;
    std::array<SadChannelState, 2> loopState_{};
    void resetPredictor(std::array<SadChannelState, 2>& replacement);
    void readIntoScratch(std::size_t count);
};
} // namespace picarat
