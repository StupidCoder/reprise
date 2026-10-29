#include "sound/sad_stream.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace picarat {
void SadStream::resetPredictor(std::array<SadChannelState, 2>& replacement) {
    for (unsigned c = 0; c < 2; ++c) {
        // 020055b4..055e0 clears 16 bytes at +208 + channel*18, leaving
        // previousFiltered at +10 intact. The loop restore has the same width.
        channels[c].history = replacement[c].history;
        channels[c].olderHistory = replacement[c].olderHistory;
        channels[c].previousSample = replacement[c].previousSample;
    }
}

void SadStream::open(Bytes file, std::uint32_t requestBytes) {
    auto header = readSadHeader(file);
    std::array<SadChannelState, 2> zeros{};
    resetPredictor(zeros);
    file_ = std::move(file);
    header_ = header;
    for (unsigned c = 0; c < header_.channels; ++c) {
        const auto offset = 0x80 + c * 16;
        loopState_[c] = {std::bit_cast<std::int32_t>(read32(file_, offset)),
                         std::bit_cast<std::int32_t>(read32(file_, offset + 4)),
                         std::bit_cast<std::int16_t>(read16(file_, offset + 8)), 0};
    }
    request = requestBytes;
    scratch.assign(std::max<std::uint32_t>(request, 256), 0);
    // Header read uses the same shared backing buffer as subsequent refills.
    std::copy_n(file_.begin(), 256, scratch.begin());
    cursor = header_.dataBegin;
    bytesRead = header_.dataBegin;
    emptyRefillSeen = false;
    playing = true;
    loopCount = 0;
}

void SadStream::readIntoScratch(std::size_t count) {
    if (count > scratch.size() || cursor > file_.size() || count > file_.size() - cursor) {
        throw std::runtime_error("SAD stream read outside payload");
    }
    std::copy_n(file_.begin() + cursor, count, scratch.begin());
}

std::vector<std::int16_t> SadStream::refill() {
    if (file_.empty()) {
        throw std::runtime_error("SAD stream is not open");
    }
    const auto channelCount = header_.channels;
    const auto frames = request / (16u * channelCount) * header_.samplesPerBlock();
    std::vector<std::int16_t> pcm(frames * channelCount, 0);
    const auto filter = sadFilter(header_.cutoff);
    const auto end = header_.repeats ? header_.loopEnd : header_.dataEnd;
    std::size_t written = 0;
    auto decode = [&](std::size_t compressedBytes) {
        const auto blocks = compressedBytes / (16 * channelCount);
        for (unsigned c = 0; c < channelCount; ++c) {
            for (std::size_t b = 0; b < blocks; ++b) {
                auto samples =
                    decodeSadBlock(std::span(scratch).subspan((b * channelCount + c) * 16, 16),
                                   header_.format, filter, channels[c]);
                for (std::size_t i = 0; i < samples.size(); ++i) {
                    pcm[(written + b * samples.size() + i) * channelCount + c] = samples[i];
                }
            }
        }
        written += blocks * header_.samplesPerBlock();
    };
    if (header_.repeats) {
        std::uint32_t remaining = request;
        while (remaining) {
            const auto count = std::min(remaining, end - cursor);
            if (!count) {
                throw std::runtime_error("empty SAD loop segment");
            }
            readIntoScratch(count);
            decode(count);
            cursor += count;
            bytesRead += count;
            remaining -= count;
            if (cursor >= end) {
                cursor = header_.loopBegin;
                // 02006cd0 copies only the 16-byte saved predictor state.
                for (unsigned c = 0; c < channelCount; ++c) {
                    channels[c].history = loopState_[c].history;
                    channels[c].olderHistory = loopState_[c].olderHistory;
                    channels[c].previousSample = loopState_[c].previousSample;
                }
                ++loopCount;
            }
        }
    } else if (cursor == end) {
        // 02006e24..e98: two empty refill callbacks before clearing busy.
        if (emptyRefillSeen) {
            playing = false;
        } else {
            emptyRefillSeen = true;
        }
    } else {
        const auto count = std::min<std::uint32_t>(request, end - cursor);
        readIntoScratch(count);
        // Deliberately decode the entire block, including the stale source
        // tail, BEFORE zeroing output beyond EOF. State keeps that tail result.
        decode(request);
        cursor += count;
        bytesRead += count;
        const auto validSamples = count / 16 * header_.samplesPerBlock();
        std::fill(pcm.begin() + validSamples, pcm.end(), 0);
    }
    return pcm;
}
} // namespace picarat
