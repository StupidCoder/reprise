// =============================================================================================
// DspMixer.hpp - the console's audio DSP mixing stage
// =============================================================================================
//
// A model of the hardware's mixing behaviour (no game logic), written to reproduce the reference
// recordings of the original sample for sample:
//   * the output is 32728 Hz stereo, produced in frames of 160 samples;
//   * a voice reads its PCM at a playback rate, with a fixed-point read position (24 fractional
//     bits) and linear interpolation between neighbouring samples; the difference between the
//     two samples saturates to 16 bits. (The real DSP filters with more taps; the recordings were
//     made with plain linear interpolation, so that is what reproduces them.)
//   * each voice sends to three intermediate mixes with four gains each (left, right, back left,
//     back right). A changed gain ramps linearly from the old value across the next frame;
//   * every mix is scaled by its volume, its back channels are folded into the front, and the
//     result is clamped to 16 bits before the mixes are summed (and clamped again).
// Voices play decoded PCM owned by the caller (streams and waves).
// =============================================================================================
#pragma once
#include "audio/AudioFormats.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace captain_toad::audio {

constexpr int kFrameSamples = 160;
constexpr double kOutputRate = 32728.498046875;   // 268111856 / 8192
constexpr double kRateBase = 32728.0;             // the middleware divides wave rates by 32728 (DSP log)

using Stereo = std::array<int16_t, 2>;
using Quad = std::array<float, 4>;   // left, right, back left, back right

// The gains of one voice into one intermediate mix. After a change the gains move from `from`
// to `to` across the next frame.
struct MixSend {
    Quad to{};
    Quad from{};
    bool ramping = false;

    // A new target, reached by the end of the next frame.
    void set(const Quad& gains) {
        from = to;
        to = gains;
        ramping = true;
    }
    // A new value that applies at once.
    void snap(const Quad& gains) {
        to = gains;
        from = gains;
        ramping = false;
    }
    // The ramp has run its frame.
    void settle() {
        from = to;
        ramping = false;
    }
    // Gain k at frame progress t in [0, 1].
    float at(int k, float t) const { return ramping ? from[k] + (to[k] - from[k]) * t : to[k]; }
};

// One DSP voice.
struct DspVoice {
    const Pcm* pcm = nullptr;
    bool enabled = false;
    bool loop = false;
    uint32_t loopStart = 0;   // samples
    uint32_t end = 0;         // samples
    float rate = 1.f;         // playback rate (1 = one input sample per output sample)
    int channel = -1;         // >= 0: a mono voice playing this PCM channel
    uint32_t cursor = 0;      // next input sample not yet read
    std::array<MixSend, 3> sends;

    // Read state: the two samples read last (oldest first) and the read position, 24-bit fixed
    // point, measured from the older of them.
    std::array<Stereo, 2> history{};
    uint64_t phase = 0;

    void start(const Pcm* p, uint32_t position = 0) {
        pcm = p;
        enabled = true;
        loop = p->loop;
        loopStart = p->loopStart;
        end = uint32_t(p->frames());
        cursor = position;
        channel = -1;
        history = {};
        phase = 0;
        for (auto& s : sends) {
            s.ramping = false;
        }
    }

    void setGains(const std::array<Quad, 3>& gains) {
        for (int m = 0; m < 3; ++m) {
            sends[m].set(gains[m]);
        }
    }

    void snapGains(const std::array<Quad, 3>& gains) {
        for (int m = 0; m < 3; ++m) {
            sends[m].snap(gains[m]);
        }
    }

    // Input sample i as a stereo pair (mono PCM is duplicated).
    Stereo sample(uint32_t i) const {
        if (channel >= 0) {
            const int16_t v = pcm->ch[size_t(channel)][i];
            return {v, v};
        }
        const int16_t l = pcm->ch[0][i];
        const int16_t r = pcm->ch.size() > 1 ? pcm->ch[1][i] : l;
        return {l, r};
    }
};

class DspMixer {
public:
    std::vector<DspVoice> sources;
    std::array<float, 3> mixVolume{1.f, 1.f, 1.f};

    explicit DspMixer(size_t count = 24) : sources(count) {}

    // Produces one DSP frame (160 stereo samples, interleaved) into `out`.
    void frame(int16_t* out) {
        Mixes mixes{};
        for (DspVoice& v : sources) {
            Frame samples{};
            if (v.enabled && v.pcm) {
                read(v, samples);
            }
            send(v, samples, mixes);
        }
        for (int i = 0; i < kFrameSamples; ++i) {
            int32_t left = 0;
            int32_t right = 0;
            for (int m = 0; m < 3; ++m) {
                const auto& q = mixes[m][i];
                const float volume = mixVolume[m];
                left = clamp16(left + clamp16(int32_t(volume * q[0] + volume * q[2])));
                right = clamp16(right + clamp16(int32_t(volume * q[1] + volume * q[3])));
            }
            out[2 * i] = int16_t(left);
            out[2 * i + 1] = int16_t(right);
        }
    }

private:
    using Frame = std::array<Stereo, kFrameSamples>;
    using Mixes = std::array<std::array<std::array<int32_t, 4>, kFrameSamples>, 3>;

    static constexpr int kFractionBits = 24;
    static constexpr uint64_t kOne = uint64_t(1) << kFractionBits;

    // Linear interpolation from a towards b at `fraction` / 2^24, with the difference saturated.
    static Stereo interpolate(const Stereo& a, const Stereo& b, uint64_t fraction) {
        Stereo out;
        for (int c = 0; c < 2; ++c) {
            const int64_t difference = std::min<int64_t>(32767, std::max<int64_t>(-32768, int64_t(b[c]) - a[c]));
            out[c] = int16_t(a[c] + int64_t(fraction) * difference / int64_t(kOne));
        }
        return out;
    }

    // Reads one frame of the voice. The read position moves over a window made of the two
    // samples read last followed by the unread input; an output sample needs the window sample
    // at the position and the one after it, with one more sample still ahead. When the input
    // runs out, a looping voice continues at its loop start; any other voice plays the partly
    // filled frame and stops at the next one.
    static void read(DspVoice& v, Frame& out) {
        int written = 0;
        while (written < kFrameSamples) {
            if (v.cursor >= v.end) {
                if (!v.loop) {
                    if (written == 0) {
                        v.enabled = false;
                    }
                    return;
                }
                v.cursor = v.loopStart;
            }
            const uint64_t step = uint64_t(v.rate * float(kOne));
            const size_t windowLength = size_t(v.end - v.cursor) + 2;
            auto window = [&](size_t k) {
                return k < 2 ? v.history[k] : v.sample(v.cursor + uint32_t(k - 2));
            };
            uint64_t phase = v.phase;
            size_t used = 0;   // window index of the last sample pair read
            while (written < kFrameSamples) {
                const size_t index = size_t(phase >> kFractionBits);
                if (index + 2 >= windowLength) {
                    used = windowLength - 2;
                    break;
                }
                used = index;
                out[written++] = interpolate(window(index), window(index + 1), phase & (kOne - 1));
                phase += step;
            }
            // Slide the window so the pair read last becomes the history.
            v.history = {window(used), window(used + 1)};
            v.phase = phase - (uint64_t(used) << kFractionBits);
            v.cursor += uint32_t(used);
        }
    }

    // Adds a voice's frame to the three mixes (back channels take the same input as the front).
    static void send(DspVoice& v, const Frame& samples, Mixes& mixes) {
        constexpr float kStep = 1.f / float(kFrameSamples - 1);
        for (int m = 0; m < 3; ++m) {
            MixSend& s = v.sends[m];
            if (v.enabled) {
                for (int i = 0; i < kFrameSamples; ++i) {
                    const float t = float(i) * kStep;
                    for (int k = 0; k < 4; ++k) {
                        mixes[m][i][k] += int32_t(s.at(k, t) * samples[i][k & 1]);
                    }
                }
            }
            s.settle();
        }
    }
};

}   // namespace captain_toad::audio
