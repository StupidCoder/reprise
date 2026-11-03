#include "audio/WaveDecoder.hpp"

#include "third_party/atrac9/libatrac9.h"
#include "third_party/hevag_coefs.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace beacon::audio {

namespace {

bool decodeAtrac9(const data::Wave& w, Pcm& out, std::string* error) {
    std::unique_ptr<void, void (*)(void*)> handle(Atrac9GetHandle(), Atrac9ReleaseHandle);
    std::uint8_t config[4] = {w.atrac9Config[0], w.atrac9Config[1], w.atrac9Config[2], w.atrac9Config[3]};
    if (!handle || Atrac9InitDecoder(handle.get(), config) != 0) {
        if (error) {
            *error = "ATRAC9 decoder rejected the configuration";
        }
        return false;
    }
    Atrac9CodecInfo info{};
    Atrac9GetCodecInfo(handle.get(), &info);
    // Atrac9Decode decodes one frame. Frames are packed back to back inside a superframe (advance by
    // the bytes used); each superframe is padded to its fixed size.
    const std::size_t superframe = std::size_t(info.superframeSize);
    std::vector<short> block(std::size_t(info.frameSamples) * info.channels);
    out.samples.reserve(std::size_t(w.samples) * w.channels + block.size());
    for (std::size_t base = 0; base + superframe <= w.data.size() && out.frames() < w.samples; base += superframe) {
        std::size_t at = base;
        for (int f = 0; f < info.framesInSuperframe; ++f) {
            int used = 0;
            if (Atrac9Decode(handle.get(), w.data.data() + at, block.data(), &used) != 0) {
                if (error) {
                    *error =
                        "ATRAC9 decode error at byte " + std::to_string(at) + " of " + std::to_string(w.data.size());
                }
                return false;
            }
            out.samples.insert(out.samples.end(), block.begin(), block.end());
            at += std::size_t(used);
        }
    }
    out.samples.resize(std::min(out.samples.size(), std::size_t(w.samples) * w.channels));
    return true;
}

// One channel of HE-VAG frames, `stride` bytes apart; returns decoded samples.
void decodeHeVagChannel(const std::uint8_t* data, std::size_t frames, std::size_t stride, std::size_t channel,
                        std::size_t channels, std::vector<std::int16_t>& out, std::size_t maxSamples) {
    float h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    std::size_t produced = 0;
    for (std::size_t f = 0; f < frames && produced < maxSamples; ++f) {
        const std::uint8_t* frame = data + f * stride;
        const int predictor = std::min(((frame[1] & 0xf0) | (frame[0] >> 4)), 127);
        const int shift = frame[0] & 0x0f;
        const int flags = frame[1] & 0x0f;
        const float* c = third_party::kHevagCoefficients[predictor];
        for (int i = 0; i < 28 && produced < maxSamples; ++i) {
            float sample = 0;
            if (flags < 7) {   // flag 7 frames decode to silence
                int nibble = (frame[2 + i / 2] >> ((i & 1) * 4)) & 0x0f;
                int code = nibble >= 8 ? nibble - 16 : nibble;
                sample = float((code << 12) >> shift) + h1 * c[0] + h2 * c[1] + h3 * c[2] + h4 * c[3];
            }
            h4 = h3;
            h3 = h2;
            h2 = h1;
            h1 = sample;
            out[produced * channels + channel] = std::int16_t(std::clamp(sample, -32768.0f, 32767.0f));
            ++produced;
        }
    }
}

bool decodeHeVag(const data::Wave& w, Pcm& out, std::size_t interleave) {
    const std::size_t channels = w.channels;
    out.samples.assign(std::size_t(w.samples) * channels, 0);
    if (channels == 1) {
        decodeHeVagChannel(w.data.data(), w.data.size() / 16, 16, 0, 1, out.samples, w.samples);
        return true;
    }
    // Blocks of `interleave` bytes per channel, channels alternating.
    const std::size_t framesPerBlock = interleave / 16;
    const std::size_t blocks = w.data.size() / (interleave * channels);
    std::vector<std::uint8_t> plane(blocks * interleave);
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t b = 0; b < blocks; ++b) {
            std::copy_n(w.data.data() + (b * channels + c) * interleave, interleave, plane.data() + b * interleave);
        }
        decodeHeVagChannel(plane.data(), blocks * framesPerBlock, 16, c, channels, out.samples, w.samples);
    }
    return true;
}

}   // namespace

bool decodeWave(const data::Wave& wave, Pcm& out, std::string* error, std::size_t hevagInterleave) {
    out.channels = wave.channels;
    out.sampleRate = wave.sampleRate;
    out.samples.clear();
    if (wave.codec == data::WaveCodec::Atrac9) {
        return decodeAtrac9(wave, out, error);
    }
    return decodeHeVag(wave, out, hevagInterleave);
}

}   // namespace audio
