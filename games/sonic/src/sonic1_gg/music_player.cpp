#include "sonic1_gg/music_player.hpp"
#include "sonic1_gg/catalog.hpp"

namespace sonic1_gg {
namespace {
std::vector<gg::MusicTrack> track_list() {
    std::vector<gg::MusicTrack> tracks;
    for (const auto& t : music_tracks) {
        tracks.push_back({t.title, t.description});
    }
    return tracks;
}
} // namespace

MusicPlayer::MusicPlayer(const std::filesystem::path& p)
    : gg::MusicPlayer(track_list()), data_(load_sound(p)),
      voice_data_(read_file(p / "flow_voice.bin")) {
    if (voice_data_.size() != 0x175f) {
        throw std::runtime_error("The logo voice assets need repair. Import the ROM again.");
    }
}

void MusicPlayer::start(unsigned n) {
    voice_.reset();
    sound_ = std::make_unique<SoundDriver>(data_);
    if (music_tracks[n].music_id < 0) {
        voice_ = std::make_unique<LogoVoice>(voice_data_);
        voice_->start();
    } else {
        sound_->music(unsigned(music_tracks[n].music_id));
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
    for (auto v : sound_->take_writes()) {
        psg.write(v);
    }
    active = sound_->playing();
    return psg.render(frame_clocks);
}
} // namespace sonic1_gg
