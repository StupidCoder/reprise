#include "sonic_advance_gba/game/sound.hpp"

namespace sonic_advance_gba {

void Sound::start(u16 number) {
    requests_.push_back({SoundRequest::Kind::start_song, number});
    engine_->start_song(number);
}

void Sound::stop(u16 number) {
    requests_.push_back({SoundRequest::Kind::stop_song, number});
    engine_->stop_song(number);
}

void Sound::restart_tracks(u8 player) {
    requests_.push_back({SoundRequest::Kind::restart_tracks, player});
    engine_->reset_tracks(player);
}

void Sound::play_on(u8 player, u32 header, s8 pan) {
    // 0801E43C
    requests_.push_back({SoundRequest::Kind::play_header, player});
    engine_->start(player, header);
    restart_tracks(player);
    engine_->set_volume(player, 0xFFFF, 0x80);
    engine_->set_pan(player, 0xFFFF, pan);
}

void Sound::start_header(u8 player, u32 header) {
    requests_.push_back({SoundRequest::Kind::play_header, player});
    engine_->start(player, header);
}

void Sound::fade_out(u8 player, u16 speed) {
    requests_.push_back({SoundRequest::Kind::fade_out, player});
    engine_->fade_out(player, speed);
}

void Sound::restore_music(bool fast) {
    restart_tracks(0);
    engine_->set_volume(0, 0xFF, 4);
    engine_->fade_in(0, 4);
    if (fast) {
        engine_->set_tempo(0, 0x200);
    }
}

} // namespace sonic_advance_gba
