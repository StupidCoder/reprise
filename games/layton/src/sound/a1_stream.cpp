// The first adventure's streamed audio. See a1_stream.hpp.
#include "sound/a1_stream.hpp"
#include <algorithm>
#include <cstring>

namespace picarat {

Adventure1Stream::Adventure1Stream(std::vector<std::int16_t> steps) : steps_(std::move(steps)) {}

void Adventure1Stream::open(Bytes file) {
    file_ = std::move(file);
    header_ = readSadHeader(file_);
    filter_ = sadFilter(header_.cutoff, header_.sampleRate); // 020755e4
    for (unsigned ch = 0; ch < 2; ++ch) {
        state_[ch].history = header_.startValue[ch];
        state_[ch].index = header_.startIndex[ch];
    }
    position_ = header_.dataBegin;
    // Format 0x72: a refill reads an eighth of the 16-bit output it makes.
    readSize_ = std::uint32_t(kRefill * header_.channels / 8);
    read_.assign(readSize_, 0);
    half_ = 0;
    countdown_ = 0;
    ending_ = false;
}

void Adventure1Stream::clearRing() {
    ring_.fill(0);
    half_ = 0;
}

bool Adventure1Stream::refill() {
    const unsigned half = half_;
    half_ ^= 1;
    std::uint32_t count = readSize_;
    const std::uint32_t remaining = header_.dataEnd > position_ ? header_.dataEnd - position_ : 0;
    if (remaining < readSize_) {
        if (!ending_) {
            countdown_ = 2;
            ending_ = true;
        }
        if (remaining == 0) { // 02075584: silence
            for (unsigned ch = 0; ch < header_.channels; ++ch) {
                std::memset(ring_.data() + kRefill * half + kChannelRing * ch, 0, kRefill);
            }
            if (countdown_ == 0) {
                return false; // 02074f64
            }
            --countdown_;
            return true;
        }
        std::fill(read_.begin(), read_.end(), std::uint8_t(0));
        count = remaining;
    }
    std::copy_n(file_.begin() + position_, count, read_.begin());
    position_ += count;
    // Channel blocks interleave 16 bytes at a time.
    for (unsigned ch = 0; ch < header_.channels; ++ch) {
        std::uint8_t* out = ring_.data() + kRefill * half + kChannelRing * ch;
        for (std::size_t at = 16 * ch; at + 16 <= read_.size(); at += 16u * header_.channels) {
            const auto pcm =
                decodeSadImaBlock(std::span(read_).subspan(at, 16), filter_, steps_, state_[ch]);
            for (const std::int16_t sample : pcm) {
                const auto bits = std::uint16_t(sample);
                *out++ = std::uint8_t(bits);
                *out++ = std::uint8_t(bits >> 8);
            }
        }
    }
    return true;
}

std::span<const std::uint8_t> Adventure1Stream::channelRing(unsigned i) const {
    const std::size_t offset = header_.channels == 1 ? 0 : kChannelRing * (i & 1);
    return std::span(ring_).subspan(offset, kChannelRing);
}

} // namespace picarat
