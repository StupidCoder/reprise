// The 3DS adventures' sound player. See a56_sound.hpp.
#include "sound/a56_sound.hpp"
#include "sound/a56_sound_archive.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace picarat::adventure56 {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kCacheSize = 8; // decoded streams kept (voices repeat)

// Game frames to output frames.
int outputFrames(int gameFrames) {
    return std::max(0, gameFrames) * SoundPlayer::kRate / 60;
}

} // namespace

SoundPlayer::SoundPlayer(fs::path soundFolder) : folder_(std::move(soundFolder)) {
    const fs::path archive = folder_ / "product.bcsar";
    if (fs::exists(archive)) {
        std::ifstream in(archive, std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        try {
            archive_ = std::make_unique<SoundArchive>(std::move(bytes));
        } catch (const std::exception&) {
            archive_.reset(); // streams still play
        }
    }
}

SoundPlayer::~SoundPlayer() = default;

std::shared_ptr<const Pcm> SoundPlayer::load(const std::string& name) {
    if (const auto it = cache_.find(name); it != cache_.end()) {
        return it->second;
    }
    const std::string path = archive_ ? archive_->externalFile(name) : std::string();
    const fs::path file = path.empty() ? folder_ / "stream" / (name + ".bcstm") : folder_ / path;
    if (!fs::exists(file)) {
        return nullptr;
    }
    std::ifstream in(file, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    std::shared_ptr<const Pcm> pcm;
    try {
        pcm = std::make_shared<const Pcm>(decodeStream(bytes));
    } catch (const std::exception&) {
        return nullptr;
    }
    cache_[name] = pcm;
    cacheOrder_.push_back(name);
    if (cacheOrder_.size() > kCacheSize) {
        cache_.erase(cacheOrder_.front());
        cacheOrder_.erase(cacheOrder_.begin());
    }
    return pcm;
}

float SoundPlayer::archiveVolume(const std::string& name) const {
    if (!archive_) {
        return 1.0f;
    }
    const int v = archive_->volume(name);
    return v < 0 ? 1.0f : float(v) / 127.0f;
}

void SoundPlayer::start(Voice& v, std::shared_ptr<const Pcm> pcm, const std::string& name,
                        int fadeInFrames) {
    v = Voice{};
    v.pcm = std::move(pcm);
    v.name = name;
    v.step = double(v.pcm->rate) / double(kRate);
    v.volume = archiveVolume(name);
    const int n = outputFrames(fadeInFrames);
    if (n > 0) {
        v.fade = 0;
        v.fadeStep = 1.0f / float(n);
    }
}

void SoundPlayer::fadeOut(Voice& v, int frames) {
    if (!v.pcm || v.done) {
        return;
    }
    const int n = outputFrames(frames);
    if (n <= 0) {
        v.done = true;
        return;
    }
    v.stopping = true;
    v.fadeStep = -v.fade / float(n);
}

void SoundPlayer::fadeOut(Effect& e, int frames) {
    if (e.done) {
        return;
    }
    const int n = outputFrames(frames);
    if (n <= 0) {
        e.player->release(); // the notes' own release
        return;
    }
    e.fadeStep = -e.fade / float(n);
}

bool SoundPlayer::playStream(int player, const std::string& name, int fadeInFrames) {
    if (player < 0 || player >= kStreamPlayers) {
        return false;
    }
    // Decode outside the lock: the audio thread keeps playing meanwhile.
    std::shared_ptr<const Pcm> pcm;
    {
        std::lock_guard lock(mutex_);
        if (const auto it = cache_.find(name); it != cache_.end()) {
            pcm = it->second;
        }
    }
    if (!pcm) {
        std::lock_guard lock(mutex_);
        pcm = load(name);
    }
    if (!pcm) {
        return false;
    }
    std::lock_guard lock(mutex_);
    start(streams_[player], pcm, name, fadeInFrames);
    return true;
}

void SoundPlayer::stopStream(int player, int fadeOutFrames) {
    if (player < 0 || player >= kStreamPlayers) {
        return;
    }
    std::lock_guard lock(mutex_);
    fadeOut(streams_[player], fadeOutFrames);
}

bool SoundPlayer::streamPlaying(int player) const {
    if (player < 0 || player >= kStreamPlayers) {
        return false;
    }
    std::lock_guard lock(mutex_);
    const Voice& v = streams_[player];
    return v.pcm && !v.done;
}

bool SoundPlayer::streamStopping(int player) const {
    if (player < 0 || player >= kStreamPlayers) {
        return false;
    }
    std::lock_guard lock(mutex_);
    const Voice& v = streams_[player];
    return v.pcm && v.stopping && !v.done;
}

void SoundPlayer::pauseStream(int player, int fadeFrames) {
    if (player < 0 || player >= kStreamPlayers) {
        return;
    }
    std::lock_guard lock(mutex_);
    Voice& v = streams_[player];
    if (!v.pcm || v.done) {
        return;
    }
    v.pausing = true;
    const int n = outputFrames(fadeFrames);
    if (n <= 0) {
        v.fade = 0;
        v.fadeStep = 0;
        v.paused = true;
    } else {
        v.fadeStep = -v.fade / float(n);
    }
}

void SoundPlayer::resumeStream(int player, int fadeFrames) {
    if (player < 0 || player >= kStreamPlayers) {
        return;
    }
    std::lock_guard lock(mutex_);
    Voice& v = streams_[player];
    if (!v.pcm || v.done) {
        return;
    }
    v.pausing = v.paused = false;
    const int n = outputFrames(fadeFrames);
    if (n <= 0) {
        v.fade = 1;
        v.fadeStep = 0;
    } else {
        v.fadeStep = (1 - v.fade) / float(n);
    }
}

bool SoundPlayer::hasSound(const std::string& name) const {
    return archive_ && archive_->has(name);
}

bool SoundPlayer::playEffect(const std::string& name) {
    if (!archive_ || effectsMuted_) {
        return false;
    }
    Effect e;
    e.player = archive_->openSequence(name); // decodes the bank's waves once
    if (!e.player) {
        return false;
    }
    e.name = name;
    e.volume = archiveVolume(name);
    std::lock_guard lock(mutex_);
    effects_.push_back(std::move(e));
    return true;
}

void SoundPlayer::stopEffect(const std::string& name, int fadeOutFrames) {
    std::lock_guard lock(mutex_);
    for (auto& v : effects_) {
        if (v.name == name) {
            fadeOut(v, fadeOutFrames);
        }
    }
}

bool SoundPlayer::effectPlaying(const std::string& name) const {
    std::lock_guard lock(mutex_);
    return std::any_of(effects_.begin(), effects_.end(),
                       [&](const Effect& e) { return e.name == name && !e.done; });
}

void SoundPlayer::stopEffects(int fadeOutFrames) {
    std::lock_guard lock(mutex_);
    for (auto& v : effects_) {
        fadeOut(v, fadeOutFrames);
    }
}

void SoundPlayer::setStreamVolume(int player, float volume) {
    std::lock_guard lock(mutex_);
    if (player >= 0 && player < kStreamPlayers) {
        playerVolume_[std::size_t(player)] = std::clamp(volume, 0.0f, 4.0f);
    }
}

void SoundPlayer::render(std::int16_t* stereo, int frames) {
    std::vector<float> mix(std::size_t(frames) * 2, 0.0f);
    std::lock_guard lock(mutex_);
    auto play = [&](Voice& v, float playerVolume) {
        if (!v.pcm || v.done || v.paused) {
            return;
        }
        const Pcm& p = *v.pcm;
        const std::size_t total = p.frames();
        if (total == 0) {
            v.done = true;
            return;
        }
        for (int i = 0; i < frames; ++i) {
            if (v.position >= double(total)) {
                if (p.loop) {
                    v.position = double(p.loopStart) +
                                 std::fmod(v.position - double(total), double(total - p.loopStart));
                } else {
                    v.done = true;
                    return;
                }
            }
            const std::size_t at = std::size_t(v.position);
            const float t = float(v.position - double(at));
            const std::size_t next = at + 1 < total ? at + 1 : (p.loop ? p.loopStart : at);
            const float gain = v.volume * v.fade * playerVolume / 32768.0f;
            float l, r;
            if (p.channels == 1) {
                l = r = (p.samples[at] * (1 - t) + p.samples[next] * t) * gain;
            } else {
                const std::size_t c = std::size_t(p.channels);
                l = (p.samples[at * c] * (1 - t) + p.samples[next * c] * t) * gain;
                r = (p.samples[at * c + 1] * (1 - t) + p.samples[next * c + 1] * t) * gain;
            }
            mix[std::size_t(i) * 2] += l;
            mix[std::size_t(i) * 2 + 1] += r;
            v.position += v.step;
            if (v.fadeStep != 0) {
                v.fade += v.fadeStep;
                if (v.fade >= 1) {
                    v.fade = 1;
                    v.fadeStep = 0;
                } else if (v.fade <= 0) {
                    v.fade = 0;
                    v.fadeStep = 0;
                    if (v.stopping) {
                        v.done = true;
                        return;
                    }
                    if (v.pausing) {
                        v.paused = true; // held where it is
                        return;
                    }
                }
            }
        }
    };
    for (std::size_t i = 0; i < std::size(streams_); ++i) {
        play(streams_[i], playerVolume_[i]);
    }
    effectMix_.resize(std::size_t(frames) * 2);
    for (auto& e : effects_) {
        if (e.done) {
            continue;
        }
        std::fill(effectMix_.begin(), effectMix_.end(), 0.0f);
        e.done = !e.player->render(effectMix_.data(), frames, kRate, e.volume);
        for (int i = 0; i < frames; ++i) {
            mix[std::size_t(i) * 2] += effectMix_[std::size_t(i) * 2] * e.fade;
            mix[std::size_t(i) * 2 + 1] += effectMix_[std::size_t(i) * 2 + 1] * e.fade;
            if (e.fadeStep != 0) {
                e.fade = std::max(0.0f, e.fade + e.fadeStep);
                if (e.fade == 0) {
                    e.done = true;
                    break;
                }
            }
        }
    }
    effects_.erase(
        std::remove_if(effects_.begin(), effects_.end(), [](const Effect& e) { return e.done; }),
        effects_.end());
    for (int i = 0; i < frames * 2; ++i) {
        stereo[i] = std::int16_t(std::clamp(mix[std::size_t(i)], -1.0f, 1.0f) * 32767.0f);
    }
}

} // namespace picarat::adventure56
