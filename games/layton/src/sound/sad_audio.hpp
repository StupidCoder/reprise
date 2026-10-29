#pragma once
#include "system/resources.hpp"
#include <array>

namespace picarat {
// SAD: the game's streamed audio format (voices, jingles, film sound and some
// music). A header gives the channel count, format, sample rate and a filter
// cutoff; the audio follows in 16-byte blocks per channel, compressed with
// one of three formats (second and third adventures 0xb2: 60 samples per
// block, 0xb4: 30; first adventure 0x72: 4-bit IMA-style ADPCM at half rate,
// 64 samples per block). Each decoded sample passes through a small filter
// set by the cutoff.
//
// ARM9 resident stream decoder state at +208 + channel*18 (hex).
// Keep this explicit: the measured first voice inherits a nonzero filter tail
// from an earlier stream even though its predictor histories have been reset.
struct SadChannelState {
    std::int32_t history = 0, olderHistory = 0; // 0x72: history is the last value
    std::int16_t previousSample = 0, previousFiltered = 0;
    std::uint8_t index = 0; // 0x72: the step index
    bool operator==(const SadChannelState&) const = default;
};

struct SadFilter {
    std::int16_t current = 0, previous = 0;
};

// 020068f8: preserve the binary64 intermediate and binary32 coefficient steps.
SadFilter sadFilter(std::uint32_t cutoff, std::uint32_t sampleRate = 32728);

// 02007cac (b2) and 02007ab0 (b4). One original 16-byte channel block.
// The caller owns stream/channel state; neither function resets or mixes it.
// b2 produces 60 samples, b4 30. No DS CPU or emulator is involved.
std::vector<std::int16_t> decodeSadBlock(std::span<const std::uint8_t> block, std::uint8_t format,
                                         SadFilter filter, SadChannelState& state);

// First adventure, 020759c0: one 16-byte channel block of format 0x72, 32
// nibbles giving 64 samples (the midpoint with the previous value, then the
// value, both filtered). `steps`: the 89-entry step table of the ARM9
// program (020c92d8).
std::vector<std::int16_t> decodeSadImaBlock(std::span<const std::uint8_t> block, SadFilter filter,
                                            std::span<const std::int16_t> steps,
                                            SadChannelState& state);

struct SadHeader {
    std::uint8_t channels = 0, format = 0;
    std::uint32_t sampleRate = 32728, cutoff = 0;
    std::uint32_t dataBegin = 0, dataEnd = 0;
    bool repeats = false;
    std::uint32_t loopBegin = 0, loopEnd = 0;
    // 0x72: each channel's starting value and step index (header +0x80).
    std::array<std::int16_t, 2> startValue{};
    std::array<std::uint8_t, 2> startIndex{};

    std::size_t samplesPerBlock() const { return format == 0xb2 ? 60 : format == 0x72 ? 64 : 30; }
};

// Bounds-checked subset of 020064f4 (second adventure) and 02075000 (first):
// the SAD formats present in the ROMs. Preserve loop offsets as metadata;
// loop scheduling/state restore is separate.
SadHeader readSadHeader(std::span<const std::uint8_t> file);

struct SadPcm {
    SadHeader header;
    std::vector<std::int16_t> samples; // WAV-style interleaved channel samples
    std::array<SadChannelState, 2> finalState;
};

// Native asset-export service: one physical payload pass, optional initial
// states, no repeat, mixer, resampling or end-of-stream buffer padding.
// `imaSteps`: needed for format 0x72 (decodeSadImaBlock).
SadPcm decodeSadFile(std::span<const std::uint8_t> file,
                     std::array<SadChannelState, 2> initialState = {},
                     std::span<const std::int16_t> imaSteps = {});
Bytes sadWave(const SadPcm& pcm);
} // namespace picarat
