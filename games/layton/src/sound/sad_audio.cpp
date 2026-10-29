#include "sound/sad_audio.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace picarat {
namespace {
// ARM additions/multiplies wrap at 32 bits. Express that before division;
// signed overflow or left shifting negative integers would be undefined C++.
std::int32_t wrap32(std::int64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}

std::int16_t wrap16(std::int32_t value) {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}

std::int32_t armAsr(std::int32_t value, unsigned shift) {
    shift &= 255; // ARM register shift uses only the low byte.
    if (shift == 0) {
        return value;
    }
    if (shift >= 32) {
        return value < 0 ? -1 : 0;
    }
    auto bits = std::bit_cast<std::uint32_t>(value) >> shift;
    if (value < 0) {
        bits |= ~std::uint32_t{0} << (32 - shift);
    }
    return std::bit_cast<std::int32_t>(bits);
}

// The ROM biases negative values before ASR, implementing division toward zero.
std::int16_t quantize(std::int16_t value) {
    return (value / 64) * 64;
}

std::int16_t filtered(std::int16_t value, std::int16_t previous, SadFilter f) {
    auto sum = wrap32(std::int64_t(f.current) * value + std::int64_t(f.previous) * previous);
    return wrap16(sum / 256);
}

void append16(Bytes& out, std::uint16_t n) {
    out.push_back(n & 255);
    out.push_back(n >> 8);
}

void append32(Bytes& out, std::uint32_t n) {
    append16(out, n & 65535);
    append16(out, n >> 16);
}
} // namespace

SadFilter sadFilter(std::uint32_t cutoff, std::uint32_t sampleRate) {
    if (sampleRate != 32728 || cutoff > 6000) {
        throw std::runtime_error("SAD filter parameters outside the reviewed ROM range");
    }
    // Constant bits 401921fb54442d18 at 02006998; software double multiply /
    // divide followed by round-to-nearest binary32 at 02039714. No fast-math.
    constexpr double twoPi = std::bit_cast<double>(std::uint64_t{0x401921fb54442d18});
    const double numerator = twoPi * static_cast<double>(static_cast<float>(cutoff));
    const float amount =
        static_cast<float>(numerator / static_cast<double>(static_cast<float>(sampleRate)));
    const float complement = 1.0f - amount;
    return {static_cast<std::int16_t>(256.0f * amount),
            static_cast<std::int16_t>(256.0f * complement)};
}

std::vector<std::int16_t> decodeSadBlock(std::span<const std::uint8_t> block, std::uint8_t format,
                                         SadFilter filter, SadChannelState& state) {
    if (block.size() != 16 || (format != 0xb2 && format != 0xb4)) {
        throw std::runtime_error("unsupported SAD block size/format");
    }
    // Resident coefficient table 0203e300 stores the bitwise inverse bytes.
    constexpr int coefficients[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    const unsigned header = block[15] ^ 0x80;
    const unsigned predictor = (header >> 4) > 4 ? 0 : header >> 4;
    const auto shift = static_cast<unsigned>(12 - static_cast<int>(header & 15));
    std::vector<std::int16_t> pcm;
    pcm.reserve(format == 0xb2 ? 60 : 30);
    // The original loads four words; the last supplies only six nibbles, so
    // byte 15 is the header, never a thirty-first or thirty-second sample.
    for (unsigned i = 0; i < 30; ++i) {
        const auto value = (block[i / 2] ^ 0x80) >> (4 * (i & 1));
        const auto delta = armAsr(wrap16(static_cast<std::int32_t>((value & 15) << 12)), shift);
        const auto sum = wrap32(std::int64_t(state.history) * coefficients[predictor][0] +
                                std::int64_t(state.olderHistory) * coefficients[predictor][1] + 32);
        const auto history = wrap32(sum / 64 + std::int64_t(delta) * 64);
        const auto rounded = wrap32(std::int64_t(history) + 32) / 64;
        const auto sample = static_cast<std::int16_t>(std::clamp(rounded, -32768, 32767));
        state.olderHistory = state.history;
        state.history = history; // History is NOT the clamped/quantized sample.
        if (format == 0xb2) {
            const auto midpoint =
                static_cast<std::int16_t>((int(sample) + state.previousSample) / 2);
            const auto first = filtered(midpoint, state.previousFiltered, filter);
            const auto second = filtered(sample, first, filter);
            pcm.push_back(quantize(first));
            pcm.push_back(quantize(second));
            state.previousSample = sample;
            state.previousFiltered = second;
        } else {
            pcm.push_back(quantize(sample));
            // b4 never writes the b2 interpolation/filter state fields.
        }
    }
    return pcm;
}

std::vector<std::int16_t> decodeSadImaBlock(std::span<const std::uint8_t> block, SadFilter filter,
                                            std::span<const std::int16_t> steps,
                                            SadChannelState& state) {
    if (block.size() != 16 || steps.size() < 89) {
        throw std::runtime_error("unsupported SAD block size/step table");
    }
    // 02075690 builds the index table: -1 for magnitudes 0..3, then 2, 4,
    // 6, 8, clamped to 0..88.
    constexpr int adjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    std::vector<std::int16_t> pcm;
    pcm.reserve(64);
    int value = std::int16_t(state.history);
    int index = std::min<int>(state.index, 88);
    std::int16_t previous = state.previousFiltered;
    for (unsigned i = 0; i < 32; ++i) { // four words, low nibble first
        const unsigned nibble = (block[i / 2] >> (4 * (i & 1))) & 15;
        const int step = steps[std::size_t(index)];
        int diff = step >> 3;
        if (nibble & 1) {
            diff += step >> 2;
        }
        if (nibble & 2) {
            diff += step >> 1;
        }
        if (nibble & 4) {
            diff += step;
        }
        const int last = value;
        value = nibble & 8 ? std::max(last - diff, -0x8000) : std::min(last + diff, 0x7fff);
        index = std::clamp(index + adjust[nibble & 7], 0, 88);
        // The midpoint truncates toward zero, as the ROM's halving does.
        const auto midpoint = static_cast<std::int16_t>((last + value) / 2);
        const auto first = filtered(midpoint, previous, filter);
        previous = filtered(static_cast<std::int16_t>(value), first, filter);
        pcm.push_back(quantize(first));
        pcm.push_back(quantize(previous));
    }
    state.history = value;
    state.index = std::uint8_t(index);
    state.previousFiltered = previous;
    return pcm;
}

SadHeader readSadHeader(std::span<const std::uint8_t> file) {
    if (file.size() < 256 || read32(file, 0) != 0x6c646173 || read32(file, 8) != file.size() ||
        (read16(file, 12) != 0x415 && read16(file, 12) != 0x411)) {
        throw std::runtime_error("invalid/unreviewed SAD header");
    }
    SadHeader h;
    h.channels = file[0x32];
    h.format = file[0x33];
    h.cutoff = read32(file, 0x68);
    const bool ima = h.format == 0x72;
    if (h.channels < 1 || h.channels > 2 || (h.format != 0xb2 && h.format != 0xb4 && !ima) ||
        read16(file, 0x3a) != 16 || (!ima && file[0x65] != 1) ||
        (!ima && read16(file, 12) != 0x415)) {
        throw std::runtime_error("unsupported SAD codec configuration");
    }
    if (ima) {
        for (unsigned ch = 0; ch < 2; ++ch) { // 02075000: header +0x80, a word each
            h.startValue[ch] = std::int16_t(read16(file, 0x80 + ch * 4));
            h.startIndex[ch] = file[0x82 + ch * 4];
        }
    }
    if (h.format == 0xb2 && h.cutoff == 0) {
        h.cutoff = 3000; // 0200667c
    }
    h.dataBegin = read32(file, 0x48);
    h.dataEnd = read32(file, 0x40);
    h.repeats = file[0x31] != 0;
    h.loopBegin = read32(file, 0x54);
    h.loopEnd = read32(file, 0x58);
    const unsigned group = 16 * h.channels;
    if (h.dataBegin < 256 || h.dataEnd > file.size() || h.dataBegin > h.dataEnd ||
        (h.dataEnd - h.dataBegin) % group != 0 || h.cutoff > 6000) {
        throw std::runtime_error("invalid SAD payload bounds");
    }
    if (h.repeats &&
        (h.loopBegin < h.dataBegin || h.loopEnd > h.dataEnd || h.loopBegin >= h.loopEnd ||
         (h.loopBegin - h.dataBegin) % group || (h.loopEnd - h.dataBegin) % group)) {
        throw std::runtime_error("invalid SAD loop bounds");
    }
    return h;
}

SadPcm decodeSadFile(std::span<const std::uint8_t> file,
                     std::array<SadChannelState, 2> initialState,
                     std::span<const std::int16_t> imaSteps) {
    SadPcm result{readSadHeader(file), {}, initialState};
    const auto& h = result.header;
    const auto filter = sadFilter(h.cutoff, h.sampleRate);
    if (h.format == 0x72) {
        for (unsigned ch = 0; ch < 2; ++ch) {
            result.finalState[ch].history = h.startValue[ch];
            result.finalState[ch].index = h.startIndex[ch];
        }
    }
    result.samples.reserve((h.dataEnd - h.dataBegin) / 16 * h.samplesPerBlock());
    for (std::size_t offset = h.dataBegin; offset < h.dataEnd; offset += 16 * h.channels) {
        std::array<std::vector<std::int16_t>, 2> channels;
        for (unsigned ch = 0; ch < h.channels; ++ch) {
            const auto block = file.subspan(offset + 16 * ch, 16);
            channels[ch] = h.format == 0x72
                               ? decodeSadImaBlock(block, filter, imaSteps, result.finalState[ch])
                               : decodeSadBlock(block, h.format, filter, result.finalState[ch]);
        }
        for (std::size_t i = 0; i < h.samplesPerBlock(); ++i) {
            for (unsigned ch = 0; ch < h.channels; ++ch) {
                result.samples.push_back(channels[ch][i]);
            }
        }
    }
    return result;
}

Bytes sadWave(const SadPcm& pcm) {
    if (pcm.samples.size() > (std::numeric_limits<std::uint32_t>::max() - 36u) / 2u) {
        throw std::runtime_error("SAD PCM exceeds WAV size field");
    }
    const auto length = static_cast<std::uint32_t>(pcm.samples.size() * 2);
    Bytes out{'R', 'I', 'F', 'F'};
    append32(out, length + 36);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    append32(out, 16);
    append16(out, 1);
    append16(out, pcm.header.channels);
    append32(out, pcm.header.sampleRate);
    append32(out, pcm.header.sampleRate * pcm.header.channels * 2);
    append16(out, pcm.header.channels * 2);
    append16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    append32(out, length);
    for (auto sample : pcm.samples) {
        append16(out, static_cast<std::uint16_t>(sample));
    }
    return out;
}
} // namespace picarat
