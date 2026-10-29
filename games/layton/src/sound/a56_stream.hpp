#pragma once
// Sound streams of the 3DS adventures (NintendoWare BCSTM files under
// snd/stream: music BGM_*, ambience SUR_*, jingles JNG_*, voices EVS_* and
// EVT_*, movie sound MOV_*), decoded to 16-bit PCM.
//
// BCSTM: "CSTM", the block table (INFO, SEEK, DATA). INFO holds the
// stream's codec (0 PCM8, 1 PCM16, 2 DSP-ADPCM), loop flag, channel
// count, sample rate, loop start and frame count, the block layout (count,
// size, samples per block, the last block's size, samples and padded size)
// and per channel the ADPCM coefficients. DATA interleaves the channels
// block by block (each channel's block, then the next channel's).
// DSP-ADPCM: 8-byte frames of 14 samples; a header byte (coefficient pair
// index << 4 | scale), then 4-bit deltas: sample = (c1 * h1 + c2 * h2 +
// (delta << scale << 11) + 1024) >> 11.
#include <cstdint>
#include <span>
#include <vector>

namespace picarat::adventure56 {

struct Pcm {
    int rate = 32728;
    int channels = 1;
    std::vector<std::int16_t> samples; // interleaved
    bool loop = false;
    std::size_t loopStart = 0; // frames

    std::size_t frames() const { return channels ? samples.size() / std::size_t(channels) : 0; }
};

Pcm decodeStream(std::span<const std::uint8_t> bcstm);

// A stream's INFO alone (rate, loop, loop start, frames), without decoding.
struct StreamInfo {
    int rate = 32728;
    bool loop = false;
    std::size_t loopStart = 0, frames = 0;
};

StreamInfo readStreamInfo(std::span<const std::uint8_t> bcstm);

// One wave of a wave archive (BCWAV: "CWAV", INFO and DATA blocks; INFO:
// codec, loop flag, rate, loop start, end frame, then per channel the
// sample reference (into DATA) and the ADPCM coefficients and history).
// Every effect wave of the fifth adventure is mono DSP-ADPCM.
Pcm decodeWave(std::span<const std::uint8_t> bcwav);

// One DSP-ADPCM channel: `frames` samples from `data` with the 16
// coefficients and the start history (also used for the archive's waves).
void decodeDspAdpcm(std::span<const std::uint8_t> data, std::size_t frames,
                    const std::int16_t coef[16], std::int16_t& h1, std::int16_t& h2,
                    std::int16_t* out, int stride);

} // namespace picarat::adventure56
