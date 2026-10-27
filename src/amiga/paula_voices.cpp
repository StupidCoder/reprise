#include "amiga/paula_voices.hpp"
#include <cmath>

namespace amiga {

void PaulaVoices::set_samples(int channel, std::span<const std::int8_t> samples) {
    auto& c = channels_[std::size_t(channel)];
    c.samples.assign(samples.begin(), samples.end());
    if (c.position >= double(c.samples.size())) c.position = 0;
}

void PaulaVoices::render(std::span<float> stereo, int rate) {
    // The A500's output filter: a first-order low-pass near 4.4 kHz.
    const float alpha = float(1.0 - std::exp(-2.0 * 3.141592653589793 * 4420.0 / rate));
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        std::array<float, 2> mix{};
        if (dma_) {
            for (int n = 0; n < 4; ++n) {
                auto& c = channels_[std::size_t(n)];
                if (c.samples.empty()) continue;
                const float sample = float(c.samples[std::size_t(c.position)]) / 128.0f;
                const int volume = c.volume > 64 ? 64 : (c.volume < 0 ? 0 : c.volume);
                mix[(n == 1 || n == 2) ? 1 : 0] += sample * float(volume) / 64.0f * 0.5f;
                // (A period of 0 counts 65536 colour clocks.)
                const int period = (c.period & 0xFFFF) == 0 ? 0x10000 : (c.period & 0xFFFF);
                c.position += pal_clock / period / rate;
                while (c.position >= double(c.samples.size())) c.position -= double(c.samples.size());
            }
        }
        for (int s = 0; s < 2; ++s) {
            filter_[std::size_t(s)] += alpha * (mix[std::size_t(s)] - filter_[std::size_t(s)]);
            stereo[frame + std::size_t(s)] += filter_[std::size_t(s)];
        }
    }
}

} // namespace amiga
