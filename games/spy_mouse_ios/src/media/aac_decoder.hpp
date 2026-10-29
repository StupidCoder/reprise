// An AAC decoder (ISO/IEC 14496-3) for the game's music: the Low Complexity profile with
// spectral band replication (HE-AAC: SBR, sbr_decoder.hpp), mono or stereo, from raw frames
// (MP4 samples, mp4_audio.hpp). Floating point, portable C++; no platform decoder needed.
//
// Supported: single and channel-pair elements (with M/S and intensity stereo, perceptual noise
// substitution, pulse data and temporal noise shaping), long, start, stop and eight-short
// windows with sine and Kaiser-Bessel-derived shapes, fill elements with SBR data, data stream
// and LFE elements (skipped). Not supported (not used by the game's files): main and LTP
// prediction, coupling channel elements, gain control, parametric stereo (ignored when sent).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace doc::media {

class SbrDecoder;
class BitReader;

class AacDecoder {
public:
    AacDecoder();
    ~AacDecoder();
    AacDecoder(const AacDecoder&) = delete;
    AacDecoder& operator=(const AacDecoder&) = delete;

    // The AudioSpecificConfig (MP4 esds). False with `error` for configurations it cannot decode.
    bool configure(std::span<const std::uint8_t> config, std::string& error);
    // The rate and length of the decoded frames: twice the core's with SBR.
    int outputRate() const { return sbr_ ? coreRate_ * 2 : coreRate_; }
    int frameLength() const { return sbr_ ? 2048 : 1024; }
    int channels() const { return channels_; }
    int coreRate() const { return coreRate_; }
    // Output samples between a core sample entering and leaving the decoder beyond the core's own
    // delay (which the files' priming counts): SBR's QMF analysis and synthesis. Measured: with
    // it, the trimmed output lines up sample for sample with Apple's AudioToolbox decoding.
    int extraDelay() const { return sbr_ ? 962 : 0; }
    // One frame (a raw_data_block) into `stereo`: frameLength() interleaved left/right samples
    // (mono is duplicated), -1..1. False for a frame it cannot decode (`stereo` then silent).
    bool decodeFrame(std::span<const std::uint8_t> frame, std::vector<float>& stereo);
    // After the last frame: the samples still inside the decoder (its delay), as one frame.
    bool flush(std::vector<float>& stereo);

    struct Channel;  // per output channel: spectrum, overlap, window shape

private:
    bool decodeElements(BitReader& bits);
    bool singleChannel(BitReader& bits, int channel);
    bool channelPair(BitReader& bits, int first);
    void fill(BitReader& bits, int lastElementChannels, int lastChannel);
    void synthesize(int channel);
    void output(std::vector<float>& stereo);

    int coreRate_ = 0, rateIndex_ = 0;
    int channels_ = 0;
    std::vector<std::unique_ptr<Channel>> channel_;
    std::unique_ptr<SbrDecoder> sbr_;
    bool sbrSeen_ = false;
    std::uint32_t noiseState_ = 0x1F2E3D4C;  // perceptual noise substitution
    std::array<std::vector<float>, 2> time_;  // the core's output of the frame, per channel
};

}  // namespace doc::media
