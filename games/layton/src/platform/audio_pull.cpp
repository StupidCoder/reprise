// Sound pulled in step with the game. See audio_pull.hpp.
#include "platform/audio_pull.hpp"
#include <algorithm>
#include <cmath>

namespace picarat::platform {

thread_local AudioPull* AudioPull::current = nullptr;

void AudioPull::add(const void* key, int rate, Fill fill) {
    std::lock_guard lock(mutex_);
    sources_.push_back({key, std::max(1, rate), std::move(fill), 0.0, {}});
}

void AudioPull::remove(const void* key) {
    std::lock_guard lock(mutex_);
    std::erase_if(sources_, [key](const Source& s) { return s.key == key; });
}

void AudioPull::render(std::span<float> out, int rate) {
    std::fill(out.begin(), out.end(), 0.0f);
    const std::size_t frames = out.size() / 2;
    std::lock_guard lock(mutex_);
    for (auto& s : sources_) {
        const double step = double(s.rate) / double(std::max(1, rate));
        // The source frames this needs: up to the last output frame's
        // position and the one after it (interpolation).
        const double last = s.position + step * double(frames == 0 ? 0 : frames - 1);
        const std::size_t needed = std::size_t(std::floor(last)) + 2;
        const std::size_t have = s.buffer.size() / 2;
        if (needed > have) {
            s.buffer.resize(needed * 2);
            s.fill(s.buffer.data() + have * 2, int(needed - have));
        }
        for (std::size_t i = 0; i < frames; ++i) {
            const double at = s.position + step * double(i);
            const std::size_t k = std::size_t(at);
            const float t = float(at - double(k));
            for (std::size_t c = 0; c < 2; ++c) {
                const float a = s.buffer[k * 2 + c] / 32768.0f;
                const float b = s.buffer[(k + 1) * 2 + c] / 32768.0f;
                out[i * 2 + c] += a + (b - a) * t;
            }
        }
        // Keep the frames from the next output position on.
        s.position += step * double(frames);
        const std::size_t used = std::min(std::size_t(s.position), s.buffer.size() / 2);
        s.buffer.erase(s.buffer.begin(), s.buffer.begin() + std::ptrdiff_t(used * 2));
        s.position -= double(used);
    }
    for (auto& v : out) {
        v = std::clamp(v, -1.0f, 1.0f);
    }
}

} // namespace picarat::platform
