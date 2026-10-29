#include "app/sound_output.hpp"

#include "data/program_tables.hpp"
#include "media/aac_stream.hpp"

#include <algorithm>
#include <cstdint>

namespace doc::app {
namespace {

// The game's music (.m4a: HE-AAC) as a mixer source, decoded while it plays.
class AacMusic final : public platform::MusicSource {
public:
    explicit AacMusic(std::unique_ptr<media::AacStream> stream) : stream_(std::move(stream)) {}
    int read(float* stereo, int frames) override { return stream_->read(stereo, frames); }
    void rewind() override { stream_->rewind(); }

private:
    std::unique_ptr<media::AacStream> stream_;
};

}  // namespace

SoundOutput::SoundOutput(const data::GameFiles& files, platform::Audio& audio)
    : files_(files), audio_(audio), table_(data::readSoundTable(files)) {}

std::shared_ptr<const platform::Pcm> SoundOutput::load(const std::string& file) {
    auto it = cache_.find(file);
    if (it != cache_.end()) return it->second;
    std::shared_ptr<const platform::Pcm> pcm;
    if (auto bytes = files_.readResource(file)) pcm = platform::Audio::decodeWav(*bytes);
    if (!pcm) missing_.push_back(file);
    return cache_[file] = pcm;
}

std::shared_ptr<platform::MusicSource> SoundOutput::openMusic(const std::string& file) {
    auto it = musicFiles_.find(file);
    if (it == musicFiles_.end()) {
        std::shared_ptr<const std::vector<std::uint8_t>> bytes;
        if (auto read = files_.readResource(file)) bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(*read));
        it = musicFiles_.emplace(file, bytes).first;
    }
    std::string error;
    auto stream = media::AacStream::open(it->second, platform::Audio::kRate, error);
    if (!stream) {
        if (std::find(missing_.begin(), missing_.end(), file) == missing_.end()) missing_.push_back(file);
        return nullptr;
    }
    return std::make_shared<AacMusic>(std::move(stream));
}

std::string SoundOutput::fileOf(const game::SoundEvent& e) const {
    if (e.effect >= 0 && e.effect < int(table_.size())) return table_[std::size_t(e.effect)];
    return e.file;
}

bool SoundOutput::sounding(const std::string& file, std::int64_t nowMs) const {
    auto it = soundingUntil_.find(file);
    return it != soundingUntil_.end() && it->second > nowMs;
}

std::vector<game::SoundEvent> SoundOutput::filter(const std::vector<game::SoundEvent>& events, std::int64_t nowMs) {
    std::vector<game::SoundEvent> kept;
    for (const game::SoundEvent& e : events) {
        const std::string file = fileOf(e);
        if (e.kind == game::SoundEvent::PlayOnce || e.kind == game::SoundEvent::Play) {
            const bool alreadySounding = sounding(file, nowMs);
            if (e.kind == game::SoundEvent::PlayOnce && alreadySounding) continue;
            if (e.effect >= 0) {
                // SoundList::updatePlayingList @0x1ef7a0. A full list gives up the entry of the
                // lowest priority, scanning from the last entry down to the third (the second is
                // never looked at; on a tie the earlier entry loses), and stops its effect.
                const Listed entry{file, e.loop ? 10 : 5};
                if (listed_.size() < 8) {
                    listed_.push_back(entry);
                } else {
                    std::size_t victim = 0;
                    for (std::size_t i = listed_.size() - 1; i >= 2; --i)
                        if (!(listed_[victim].priority < listed_[i].priority)) victim = i;
                    game::SoundEvent stop;
                    stop.kind = game::SoundEvent::Stop;
                    stop.file = listed_[victim].file;
                    soundingUntil_.erase(stop.file);
                    kept.push_back(stop);
                    listed_[victim] = entry;
                }
            }
            const auto pcm = file.empty() ? nullptr : load(file);
            const std::int64_t length = pcm ? std::int64_t(pcm->frames()) * 1000 / platform::Audio::kRate : 0;
            soundingUntil_[file] = e.loop ? INT64_MAX : nowMs + length;
        } else if (e.kind == game::SoundEvent::Stop) {
            // SoundList::stopSound: an effect that is not in the playing list has nothing to
            // stop (the broken boss droid asks every tick, run lbf_win).
            if (e.effect >= 0 && std::none_of(listed_.begin(), listed_.end(), [&](const Listed& l) { return l.file == file; }))
                continue;
            soundingUntil_.erase(file);
        }
        kept.push_back(e);
    }
    // SoundList::update @0x1efbc4 (App::update, after the scenes): effects that ended leave.
    std::erase_if(listed_, [&](const Listed& l) { return !sounding(l.file, nowMs); });
    return kept;
}

void SoundOutput::play(const std::vector<game::SoundEvent>& events) {
    for (const game::SoundEvent& e : events) {
        // What stays sounding (for resume()).
        const std::string name = fileOf(e);
        if (e.kind == game::SoundEvent::MusicStop) musicOn_ = false;
        if (e.kind == game::SoundEvent::Music) {
            music_ = e;
            musicOn_ = !name.empty();
        }
        if (e.kind == game::SoundEvent::Stop) loops_.erase(name);
        if ((e.kind == game::SoundEvent::Play || e.kind == game::SoundEvent::PlayOnce) && !name.empty()) {
            if (e.loop) loops_[name] = e;
            else loops_.erase(name);
        }
        if (silent_) continue;
        if (e.kind == game::SoundEvent::MusicStop) {
            audio_.stopMusic();
            continue;
        }
        const std::string file = name;
        if (file.empty()) continue;
        ++played_;
        switch (e.kind) {
        case game::SoundEvent::Music:
            audio_.playMusic(openMusic(file), e.loop);
            break;
        case game::SoundEvent::Stop:
            if (auto v = voices_.find(file); v != voices_.end()) audio_.stop(v->second);
            break;
        case game::SoundEvent::PlayOnce:
            if (auto v = voices_.find(file); v != voices_.end() && audio_.playing(v->second)) break;
            [[fallthrough]];
        case game::SoundEvent::Play:
            // SoundList::playSound restarts an effect that is still playing.
            if (auto v = voices_.find(file); v != voices_.end()) audio_.stop(v->second);
            voices_[file] = audio_.play(load(file), e.loop);
            break;
        case game::SoundEvent::MusicStop:  // (handled above)
            break;
        }
    }
}

void SoundOutput::stopAll() {
    audio_.stopAll();
    voices_.clear();
    loops_.clear();
    musicOn_ = false;
}

void SoundOutput::stopEffects() {
    audio_.stopEffects();
    voices_.clear();
    loops_.clear();
}

void SoundOutput::resume() {
    silent_ = false;
    audio_.stopAll();
    voices_.clear();
    if (musicOn_) audio_.playMusic(openMusic(fileOf(music_)), music_.loop);
    for (const auto& [file, e] : loops_) voices_[file] = audio_.play(load(file), true);
}

}  // namespace doc::app
