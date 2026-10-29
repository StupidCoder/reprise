#include "media/aac_stream.hpp"

#include <algorithm>

namespace doc::media {

std::unique_ptr<AacStream> AacStream::open(std::shared_ptr<const std::vector<std::uint8_t>> file, int outputRate,
                                           std::string& error) {
    if (!file) {
        error = "no file";
        return nullptr;
    }
    std::unique_ptr<AacStream> s(new AacStream());
    s->file_ = std::move(file);
    s->outputRate_ = outputRate;
    auto track = readMp4AudioTrack(*s->file_, error);
    if (!track) return nullptr;
    s->track_ = std::move(*track);
    if (!s->decoder_.configure(s->track_.decoderConfig, error)) return nullptr;
    s->rewind();
    return s;
}

void AacStream::rewind() {
    // Priming and padding (iTunSMPB) are in core samples; the decoder's output may run at twice
    // the core's rate (SBR) and lags by its filterbank delay on top.
    const std::uint64_t ratio = std::uint64_t(decoder_.outputRate() / decoder_.coreRate());
    skip_ = std::uint64_t(track_.priming) * ratio + std::uint64_t(decoder_.extraDelay());
    limited_ = track_.validSamples != 0;
    remaining_ = track_.validSamples * ratio;
    nextFrame_ = 0;
    decoded_.clear();
    used_ = 0;
    phase_ = 0.0;
    primed_ = false;
}

bool AacStream::decodeNext() {
    while (nextFrame_ < track_.frames.size()) {
        const Mp4AudioTrack::Frame& f = track_.frames[nextFrame_++];
        decoder_.decodeFrame({file_->data() + f.offset, f.size}, decoded_);  // (a bad frame is silence)
        used_ = 0;
        if (!decoded_.empty()) return true;
    }
    // The decoder's delay still holds the end of the music the gapless data counts.
    if (nextFrame_ == track_.frames.size() && limited_ && remaining_ > 0) {
        ++nextFrame_;
        decoder_.flush(decoded_);
        used_ = 0;
        return !decoded_.empty();
    }
    return false;
}

int AacStream::read(float* stereo, int frames) {
    const int sourceRate = decoder_.outputRate();
    auto nextSource = [&](float* lr) {
        for (;;) {
            if (limited_ && remaining_ == 0) return false;
            if (used_ * 2 >= decoded_.size() && !decodeNext()) return false;
            if (skip_ == 0) break;
            const std::uint64_t drop = std::min<std::uint64_t>(skip_, decoded_.size() / 2 - used_);
            used_ += std::size_t(drop);
            skip_ -= drop;
        }
        if (limited_) --remaining_;
        lr[0] = decoded_[used_ * 2];
        lr[1] = decoded_[used_ * 2 + 1];
        ++used_;
        return true;
    };
    int written = 0;
    if (sourceRate == outputRate_) {
        for (; written < frames; ++written)
            if (!nextSource(stereo + written * 2)) break;
        return written;
    }
    // Linear interpolation to the output rate.
    const double step = double(sourceRate) / double(outputRate_);
    if (!primed_) {
        if (!nextSource(previous_) || !nextSource(current_)) return 0;
        primed_ = true;
    }
    for (; written < frames; ++written) {
        while (phase_ >= 1.0) {
            previous_[0] = current_[0];
            previous_[1] = current_[1];
            if (!nextSource(current_)) return written;
            phase_ -= 1.0;
        }
        const float t = float(phase_);
        stereo[written * 2] = previous_[0] + (current_[0] - previous_[0]) * t;
        stereo[written * 2 + 1] = previous_[1] + (current_[1] - previous_[1]) * t;
        phase_ += step;
    }
    return written;
}

}  // namespace doc::media
