#include "cannon_fodder_amiga/sound/music_director.hpp"
#include <algorithm>

namespace fodder::sound {

void MusicDirector::start_phase(const data::MissionMap& map) {
    *this = MusicDirector{};
    for (int y = 0; y < map.height && points_.size() < 10; ++y) {
        for (int x = 0; x < map.width && points_.size() < 10; ++x) {
            const int song = map.cells[std::size_t(y * map.width + x)] >> 13 & 7;
            if (song != 0) points_.push_back({x, y, song});
        }
    }
}

void MusicDirector::choose(int leader_tile_x, int leader_tile_y, const game::GeometryTables& geometry, Rjp1& player,
                           bool engine_sound) {
    // The nearest point of songs 1-4 (later points win only when nearer);
    // further than 15 tiles on either axis counts as out of reach.
    int nearest = 16, song = 0;
    for (const auto& p : points_) {
        if (p.song > 4) continue;
        const int d = geometry.distance(p.x - leader_tile_x, p.y - leader_tile_y, 15, 1000);
        if (d < nearest) {
            nearest = d;
            song = p.song;
        }
    }
    if (nearest <= 4) {
        request(song, 0x40, player, engine_sound);
    } else if (nearest < 10) {
        request(song, 0x38 - (nearest - 5) * 8, player, engine_sound);
    } else {
        request(1, std::min((nearest - 8) * 2, 0x30), player, engine_sound);
    }
}

void MusicDirector::request(int song, int volume, Rjp1& player, bool engine_sound) {
    if (wanted_ != 0) return; // a change is in progress
    if (song != song_) {
        target_volume_ = volume;
        wanted_ = song;
    } else {
        set_music_volume(player, volume, engine_sound);
    }
}

void MusicDirector::fade(Rjp1& player, bool engine_sound) {
    if (wanted_ == 0) return;
    int volume = player.channel_volume(0);
    if (wanted_ != song_) {
        // Down to 8, then the new song starts there.
        if (--volume < 8) {
            volume = 8;
            song_ = wanted_;
            player.start_song(song_);
        }
    } else if (volume >= target_volume_) {
        volume = target_volume_;
        wanted_ = 0;
    } else {
        ++volume;
    }
    set_music_volume(player, volume, engine_sound);
}

void MusicDirector::set_music_volume(Rjp1& player, int volume, bool engine_sound) {
    player.set_channel_volume_raw(0, volume);
    if (!engine_sound) player.set_channel_volume_raw(1, volume);
}

} // namespace fodder::sound
