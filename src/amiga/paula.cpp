#include "amiga/paula.hpp"
#include <cmath>

namespace amiga {

void Paula::write(int channel, const Write& w) {
    if (log != nullptr) {
        log->push_back({3, channel, w});
    }
    auto& c = channels_[std::size_t(channel & 3)];
    c.queue.push_back(w);
    if (c.queue.size() == 1) {
        start_front(c);
    }
}

void Paula::set_period_volume(int channel, std::uint16_t period, std::uint16_t volume) {
    if (log != nullptr) {
        Write w;
        w.period = period;
        w.volume = volume;
        log->push_back({12, channel, w});
    }
    auto& c = channels_[std::size_t(channel & 3)];
    c.period = period;
    c.volume = volume;
}

void Paula::flush(int channel) {
    if (log != nullptr) {
        log->push_back({8, channel, {}});
    }
    auto& c = channels_[std::size_t(channel & 3)];
    c.queue.clear();
    c.position = 0;
    c.played = 0;
}

void Paula::reset() {
    channels_ = {};
    filter_ = {};
}

void Paula::start_front(Channel& c) {
    c.position = 0;
    c.played = 0;
    if (!c.queue.empty() && c.queue.front().set_period_volume) {
        c.period = c.queue.front().period;
        c.volume = c.queue.front().volume;
    }
}

void Paula::render(std::span<float> stereo, int rate, const Done& done) {
    // The A500's output filter: a first-order low-pass near 4.4 kHz.
    const float alpha = float(1.0 - std::exp(-2.0 * 3.141592653589793 * 4420.0 / rate));
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        std::array<float, 2> mix{};
        for (int n = 0; n < 4; ++n) {
            auto& c = channels_[std::size_t(n)];
            if (c.queue.empty()) {
                continue;
            }
            const Write& w = c.queue.front();
            if (w.length == 0 || (w.image == nullptr && w.samples.empty())) {
                c.queue.pop_front();
                done(n, w.tag);
                start_front(c);
                continue;
            }
            const auto at = w.data + std::uint32_t(c.position);
            float sample = 0.0f;
            if (w.image != nullptr) {
                sample = w.image->contains(at) ? float(w.image->s8(at)) / 128.0f : 0.0f;
            } else if (at < w.samples.size()) {
                sample = float(w.samples[at]) / 128.0f;
            }
            const int volume = c.volume > 64 ? 64 : c.volume;
            mix[(n == 1 || n == 2) ? 1 : 0] += sample * float(volume) / 64.0f * 0.5f;
            const std::uint16_t period = c.period == 0 ? 1 : c.period;
            c.position += pal_clock / period / rate;
            while (!c.queue.empty() && c.position >= double(c.queue.front().length)) {
                c.position -= double(c.queue.front().length);
                ++c.played;
                const Write& f = c.queue.front();
                if (f.cycles != 0 && c.played >= f.cycles) {
                    const int tag = f.tag;
                    const double rest = c.position;
                    c.queue.pop_front();
                    start_front(c);
                    c.position = c.queue.empty() ? 0 : rest;
                    done(n, tag);
                }
            }
        }
        for (int s = 0; s < 2; ++s) {
            filter_[std::size_t(s)] += alpha * (mix[std::size_t(s)] - filter_[std::size_t(s)]);
            stereo[frame + std::size_t(s)] += filter_[std::size_t(s)];
        }
    }
}

} // namespace amiga
