// An .m4a file played from start to end (and again, for loops): frames decoded as they are
// needed, resampled to the mixer's rate. Decoding a frame takes a fraction of its playing time,
// so music starts at once and costs little memory.
#pragma once

#include "media/aac_decoder.hpp"
#include "media/mp4_audio.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace doc::media {

class AacStream {
public:
    // `file`: the whole .m4a (shared: several plays of a track keep one copy). Null with
    // `error` when it cannot be played.
    static std::unique_ptr<AacStream> open(std::shared_ptr<const std::vector<std::uint8_t>> file, int outputRate,
                                           std::string& error);

    // Up to `frames` interleaved stereo frames at the output rate; fewer (0) at the end.
    int read(float* stereo, int frames);
    void rewind();
    int decoderRate() const { return decoder_.outputRate(); }

private:
    AacStream() = default;
    bool decodeNext();

    std::shared_ptr<const std::vector<std::uint8_t>> file_;
    Mp4AudioTrack track_;
    AacDecoder decoder_;
    int outputRate_ = 44100;
    std::size_t nextFrame_ = 0;
    std::vector<float> decoded_;  // the current frame's samples (interleaved)
    std::size_t used_ = 0;        // frames of decoded_ consumed
    double phase_ = 0.0;          // resampling position between the last two source frames
    float previous_[2] = {}, current_[2] = {};
    bool primed_ = false;
    // Gapless playback: decoded samples still to drop at the start, and how many to play (all
    // when the file does not say).
    std::uint64_t skip_ = 0, remaining_ = 0;
    bool limited_ = false;
};

}  // namespace doc::media
