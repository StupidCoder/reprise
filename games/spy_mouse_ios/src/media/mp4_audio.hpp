// The audio track of an MP4 (.m4a) file: its decoder configuration (the AAC
// AudioSpecificConfig) and where each coded frame lies. Only what playing a file from start to
// end needs; nothing is decoded here.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace doc::media {

struct Mp4AudioTrack {
    std::vector<std::uint8_t> decoderConfig;  // the esds DecoderSpecificInfo (AudioSpecificConfig)
    struct Frame {
        std::uint64_t offset = 0;
        std::uint32_t size = 0;
    };
    std::vector<Frame> frames;  // in decoding order
    std::uint32_t timescale = 0;
    std::uint32_t channels = 0, sampleRate = 0;  // as the sample entry states them
    // iTunes gapless playback data (the iTunSMPB tag), in samples of the AAC core: the encoder's
    // priming before the first real sample and how many real samples follow. Zero when absent.
    std::uint32_t priming = 0;
    std::uint64_t validSamples = 0;
};

// The first audio ('mp4a') track of the file. Fails with a message for files it cannot use.
std::optional<Mp4AudioTrack> readMp4AudioTrack(std::span<const std::uint8_t> file, std::string& error);

}  // namespace doc::media
