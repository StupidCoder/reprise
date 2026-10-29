#include "platform/audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace doc::platform {

Audio::~Audio() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

bool Audio::open() {
    if (stream_) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::feed, this);
    if (!stream_) return false;
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Audio::feed(void* self, SDL_AudioStream* stream, int additional, int) {
    auto* audio = static_cast<Audio*>(self);
    const int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) return;
    audio->buffer_.assign(std::size_t(frames) * 2, 0.0f);
    audio->mix(audio->buffer_.data(), frames);
    SDL_PutAudioStreamData(stream, audio->buffer_.data(), frames * int(sizeof(float) * 2));
}

void Audio::mix(float* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto add = [&](Voice& v) {
        if (!v.pcm) return false;
        const std::size_t n = v.pcm->frames();
        if (n == 0) return false;
        for (int f = 0; f < frames; ++f) {
            if (v.at >= n) {
                if (!v.loop) return false;
                v.at = 0;
            }
            out[f * 2] += v.pcm->samples[v.at * 2] * v.volume;
            out[f * 2 + 1] += v.pcm->samples[v.at * 2 + 1] * v.volume;
            ++v.at;
        }
        return true;
    };
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](Voice& v) { return !add(v); }), voices_.end());
    if (music_) {
        // The track decodes as it goes; a loop starts over at its end (gapless: the source
        // trims the encoder's priming and padding).
        musicBuffer_.resize(std::size_t(frames) * 2);
        int done = 0;
        bool restarted = false;
        while (done < frames) {
            const int got = music_->read(musicBuffer_.data() + done * 2, frames - done);
            done += got;
            if (done == frames) break;
            if (!musicLoop_ || (got == 0 && restarted)) {
                music_.reset();
                break;
            }
            music_->rewind();
            restarted = got == 0;
        }
        for (int i = 0; i < done * 2; ++i) out[i] += musicBuffer_[std::size_t(i)];
    }
    for (int i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

void Audio::render(float* out, int frames) {
    std::fill(out, out + std::size_t(frames) * 2, 0.0f);
    if (pull_) mix(out, frames);
}

int Audio::play(std::shared_ptr<const Pcm> pcm, bool loop, float volume) {
    if ((!stream_ && !pull_) || !pcm || pcm->frames() == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    Voice v;
    v.id = nextId_++;
    v.pcm = std::move(pcm);
    v.loop = loop;
    v.volume = volume;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void Audio::stop(int voice) {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice; }),
                  voices_.end());
}

bool Audio::playing(int voice) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice; });
}

void Audio::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
    music_.reset();
}

void Audio::stopEffects() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

void Audio::playMusic(std::shared_ptr<MusicSource> music, bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    music_ = std::move(music);
    musicLoop_ = loop;
}

void Audio::stopMusic() {
    std::lock_guard<std::mutex> lock(mutex_);
    music_.reset();
}

std::shared_ptr<Pcm> Audio::decodeWav(const std::vector<std::uint8_t>& bytes) {
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!io) return nullptr;
    SDL_AudioSpec spec;
    Uint8* data = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV_IO(io, true, &spec, &data, &length)) return nullptr;
    const SDL_AudioSpec target{SDL_AUDIO_F32, 2, kRate};
    Uint8* converted = nullptr;
    int convertedLength = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, data, int(length), &target, &converted, &convertedLength);
    SDL_free(data);
    if (!ok) return nullptr;
    auto pcm = std::make_shared<Pcm>();
    pcm->samples.resize(std::size_t(convertedLength) / sizeof(float));
    std::memcpy(pcm->samples.data(), converted, pcm->samples.size() * sizeof(float));
    SDL_free(converted);
    return pcm;
}

}  // namespace doc::platform
