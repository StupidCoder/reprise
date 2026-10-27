#include "amiga/paula_channels.hpp"
#include <cmath>

namespace amiga {

void PaulaChannels::load(int channel) {
    auto& c = channels_[std::size_t(channel)];
    c.block = c.location;
    // (A length of 0 plays 65536 words.)
    c.block_bytes = (c.length == 0 ? 0x10000u : c.length) * 2u;
    c.position = 0;
    if (block_started) block_started(channel);
}

void PaulaChannels::write_dmacon(std::uint16_t value) {
    const bool set = value & 0x8000;
    for (int n = 0; n < 4; ++n) {
        if (!(value & (1u << n))) continue;
        auto& c = channels_[std::size_t(n)];
        const bool start = set && !c.on;
        c.on = set;
        if (start) load(n);
    }
}

void PaulaChannels::render(std::span<float> stereo, int rate) {
    const float alpha = float(1.0 - std::exp(-2.0 * 3.141592653589793 * 4420.0 / rate));
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        std::array<float, 2> mix{};
        for (int n = 0; n < 4; ++n) {
            auto& c = channels_[std::size_t(n)];
            if (!c.on) continue;
            const std::size_t at = c.block + std::size_t(c.position);
            const float sample = at < memory_.size() ? float(memory_[at]) / 128.0f : 0.0f;
            const int volume = c.volume > 64 ? 64 : c.volume;
            mix[(n == 1 || n == 2) ? 1 : 0] += sample * float(volume) / 64.0f * 0.5f;
            const int period = c.period == 0 ? 0x10000 : c.period;
            c.position += pal_clock / period / rate;
            while (c.position >= double(c.block_bytes)) {
                const double over = c.position - double(c.block_bytes);
                load(n);
                c.position = over;
                if (!c.on) break; // (the interrupt switched the channel off)
            }
        }
        for (int s = 0; s < 2; ++s) {
            filter_[std::size_t(s)] += alpha * (mix[std::size_t(s)] - filter_[std::size_t(s)]);
            stereo[frame + std::size_t(s)] += filter_[std::size_t(s)];
        }
    }
}

} // namespace amiga
