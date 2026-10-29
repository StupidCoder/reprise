#include "gg_engine/audio/music_player.hpp"

namespace gg_engine {
namespace {
std::vector<gg::MusicTrack> track_list(std::span<const SoundTestTrack> tracks) {
    std::vector<gg::MusicTrack> list;
    for (const auto& t : tracks) {
        list.push_back({t.title, t.description});
    }
    return list;
}

// How long every music channel may rest before the song counts as over:
// some jingles end in an endless loop of rests (still writing "volume off"
// to the chip) instead of switching their channels off.
constexpr unsigned held_silence = 120;
} // namespace

MusicPlayer::MusicPlayer(const SoundTest& game, const std::filesystem::path& pack)
    : gg::MusicPlayer(track_list(game.tracks)), game_(game),
      assets_(Assets::load(game.cartridge, pack)) {}

void MusicPlayer::start(unsigned n) {
    voice_.reset();
    resting_ = 0;
    sound_ = std::make_unique<SoundDriver>(assets_, game_.driver);
    sound_->init();
    if (game_.tracks[n].song < 0) {
        voice_.emplace(assets_, game_.voice);
        voice_->start();
    } else {
        sound_->request(u8(game_.tracks[n].song), game_.song_slot);
    }
}

void MusicPlayer::release() {
    voice_.reset();
    sound_.reset();
}

std::vector<float> MusicPlayer::frame(gg::Psg& psg, bool& active) {
    if (voice_) {
        auto samples = voice_->render(psg, frame_clocks);
        active = voice_->playing();
        return samples;
    }
    sound_->tick();
    for (const auto& w : sound_->take_writes()) {
        if (w.port == 0x06) {
            psg.stereo(w.value);
        } else {
            psg.write(w.value);
        }
    }
    // The music channels (tone 1-3, noise): on (bit 7) and not resting (bit 4).
    bool on = false, sounding = false;
    for (unsigned c = sound_channel::tone1; c <= sound_channel::noise; ++c) {
        const auto flags = sound_->state.channels[c].flags;
        on = on || (flags & 0x80);
        sounding = sounding || ((flags & 0x80) && !(flags & 0x10));
    }
    resting_ = sounding ? 0 : resting_ + 1;
    active = on && resting_ < held_silence;
    return psg.render(frame_clocks);
}

} // namespace gg_engine
