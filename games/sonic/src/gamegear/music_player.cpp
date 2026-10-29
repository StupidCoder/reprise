#include "gamegear/music_player.hpp"
#include <algorithm>
#include <stdexcept>

namespace gg {

void MusicPlayer::clear() {
    ticks_ = tail_ = position_ = 0;
    history_.fill(0);
    psg_ = Psg{};
}

void MusicPlayer::play(unsigned n) {
    if (n >= tracks_.size()) {
        throw std::runtime_error("Unknown music track");
    }
    track_ = n;
    clear();
    start(n);
    mode_ = Mode::Playing;
}

void MusicPlayer::toggle() {
    if (mode_ == Mode::Playing) {
        mode_ = Mode::Paused;
    } else if (mode_ == Mode::Paused) {
        mode_ = Mode::Playing;
    } else {
        play(track_);
    }
}

void MusicPlayer::stop() {
    mode_ = Mode::Stopped;
    clear();
    release();
}

void MusicPlayer::skip(int direction) {
    const int count = int(tracks_.size());
    play(unsigned((int(track_) + count + (direction < 0 ? -1 : 1)) % count));
}

std::vector<float> MusicPlayer::tick() {
    if (!playing()) {
        return {};
    }
    ++ticks_;
    bool active = false;
    auto samples = frame(psg_, active);
    for (std::size_t i = 0; i + 1 < samples.size(); i += 2) {
        history_[position_] = (samples[i] + samples[i + 1]) * 0.5f;
        position_ = (position_ + 1) % history_.size();
    }
    // Keep a short synthesis tail so naturally ending jingles do not click off.
    if (active) {
        tail_ = 0;
    } else if (++tail_ >= 6) {
        mode_ = Mode::Finished;
    }
    return samples;
}

std::array<std::array<float, 2>, 100> MusicPlayer::waveform() const {
    std::array<std::array<float, 2>, 100> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        float lo = 1, hi = -1;
        for (std::size_t j = 0; j < 20; ++j) {
            auto v = history_[(position_ + i * 20 + j) % history_.size()];
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        result[i] = {lo, hi};
    }
    return result;
}

} // namespace gg
