// Sound output through the platform's sound device. See audio_output.hpp.
#include "shell/audio_output.hpp"
#include "system/resources.hpp"
#include <algorithm>
#include <stdexcept>

namespace picarat::shell {

namespace {
// The DS mixes its sound at 32728 Hz (rounded); the device converts.
constexpr int kSampleRate = 32728;
} // namespace

AudioOutput::AudioOutput(std::filesystem::path assets, bool useDevice)
    : AudioOutput(
          [&] {
              // Packs from schema 8 on carry the sequenced sound files.
              if (!fileExists(assets / "sound" / "SY_999.SWD")) {
                  throw std::runtime_error("asset pack has no sound files; rebuild it");
              }
              return picarat::SoundLayout::adventure2(assets);
          }(),
          useDevice) {}

AudioOutput::AudioOutput(picarat::SoundLayout layout, bool useDevice) : layout_(std::move(layout)) {
    engine_ = std::make_unique<picarat::SoundEngine>(layout_);
    if (useDevice) {
        device_ = std::make_unique<platform::AudioStream>(
            kSampleRate, [this](std::int16_t* stereo, int frames) { fill(stereo, frames); });
    }
}

AudioOutput::~AudioOutput() {
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
    }
    // Close the device outside the lock: closing waits for a running fill().
    device_.reset();
}

void AudioOutput::fill(std::int16_t* stereo, int frames) noexcept {
    std::lock_guard lock(mutex_);
    std::fill_n(stereo, std::size_t(frames) * 2, std::int16_t(0));
    if (closing_ || !error_.empty()) {
        return;
    }
    try {
        renderLocked({stereo, std::size_t(frames) * 2});
    } catch (const std::exception& error) {
        error_ = error.what(); // reported to the game thread by check()
    }
}

void AudioOutput::renderLocked(std::span<std::int16_t> stereo) {
    engine_->render(stereo);
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        history_[historyAt_] = (stereo[i] + stereo[i + 1]) / 65536.0f;
        historyAt_ = (historyAt_ + 1) % kHistory;
    }
}

void AudioOutput::reset() {
    // A native restart (title, collection, checkpoint load) corresponds to a
    // fresh boot: the sound system is rebuilt (0x0207f384 initialisation,
    // banks reloaded from the pack) and any stream is dropped.
    std::lock_guard lock(mutex_);
    events_.clear();
    engine_ = std::make_unique<picarat::SoundEngine>(layout_);
}

void AudioOutput::streamEvent(const char* name, int route, int id, int part) {
    std::string path = picarat::streamPath(route, id, part);
    const std::string prefix = "/data_lt2/stream/";
    if (path.rfind(prefix, 0) == 0) {
        path = path.substr(prefix.size());
    }
    std::lock_guard lock(mutex_);
    events_.push_back(std::string(name) + " " + path);
}

void AudioOutput::check() {
    std::lock_guard lock(mutex_);
    if (!error_.empty()) {
        throw std::runtime_error(error_);
    }
}

std::vector<std::int16_t> AudioOutput::renderOffline(unsigned frames) {
    if (device_) {
        throw std::runtime_error("offline rendering cannot consume device playback");
    }
    std::lock_guard lock(mutex_);
    std::vector<std::int16_t> pcm(std::size_t(frames) * 2);
    renderLocked(pcm);
    return pcm;
}

std::vector<float> AudioOutput::recent(std::size_t count) {
    std::lock_guard lock(mutex_);
    count = std::min(count, kHistory);
    std::vector<float> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = history_[(historyAt_ + kHistory - count + i) % kHistory];
    }
    return out;
}

std::vector<std::string> AudioOutput::events() {
    std::lock_guard lock(mutex_);
    return events_;
}

} // namespace picarat::shell
