// Decodes sound-bank waves to interleaved 16-bit PCM.
//   ATRAC9: via LibAtrac9 (third_party/atrac9, MIT), superframe by superframe.
//   HE-VAG: Sony's 4-tap ADPCM. 16-byte frames of 28 samples: byte 0 = predictor low nibble << 4 |
//           shift, byte 1 = predictor high bits (0xf0) | flags, 14 bytes of 4-bit codes (low nibble
//           first). Coefficients: third_party/hevag_coefs.h.
#pragma once

#include "data/SoundBank.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace beacon::audio {

struct Pcm {
    std::uint32_t channels = 0, sampleRate = 0;
    std::vector<std::int16_t> samples;   // interleaved
    std::size_t frames() const { return channels ? samples.size() / channels : 0; }
};

// `hevagInterleave`: bytes per channel block in multichannel HE-VAG data.
bool decodeWave(const data::Wave& wave, Pcm& out, std::string* error = nullptr, std::size_t hevagInterleave = 16);

}   // namespace audio
