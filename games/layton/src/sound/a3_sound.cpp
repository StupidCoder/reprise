// The third adventure's sound requests. See a3_sound.hpp.
#include "sound/a3_sound.hpp"
#include "sound/sound_object.hpp"

namespace picarat {
namespace {
float seconds(int frames) {
    return float(frames) / 60.0f;
}
} // namespace

Adventure3Sound::Adventure3Sound(GameSound& sound, std::map<int, std::array<int, 3>> sets)
    : sound_(sound), sets_(std::move(sets)) {}

void Adventure3Sound::playEffect(int id) {
    sound_.playEffect(id);
}

void Adventure3Sound::stopEffect(int id, int frames) {
    sound_.stopEffect(id, seconds(frames));
}

bool Adventure3Sound::effectPlaying(int id) {
    return sound_.effectPlaying(id);
}

bool Adventure3Sound::loadBank(int group, int number) {
    // A negative number keeps the bank; the same one stays loaded.
    if (number < 0) {
        return true;
    }
    if (number != banks_[std::size_t(group)]) {
        banks_[std::size_t(group)] = -1;
        sound_.loadBank(unsigned(group), number);
        banks_[std::size_t(group)] = number;
    }
    return true;
}

void Adventure3Sound::selectSet(int set, unsigned flags, int frames) {
    if (set < 0) {
        return;
    }
    set_ = set;
    auto it = sets_.find(set);
    if (it == sets_.end()) {
        return;
    }
    const auto [music, ge, si] = it->second;
    // 020d01a0: the same music again (unless flag 1) just comes back to
    // full volume over a second; other music is loaded and started.
    if (!(flags & 0x100)) {
        if (!(flags & 1) && music == music_) {
            playMusic(1.0f, 60, 0);
        } else if (loadMusic(music, frames)) {
            playMusic(1.0f, 0, flags);
        }
    }
    if (!(flags & 0x400)) {
        loadBank(1, ge);
    }
    if (!(flags & 0x800)) {
        loadBank(2, si);
    }
}

bool Adventure3Sound::loadMusic(int id, int frames) {
    if (id < 0) {
        return true;
    }
    if (id != music_) {
        sound_.stopBgm(seconds(frames));
        music_ = -1;
        sound_.switchBgm(id);
        music_ = id;
    }
    return true;
}

void Adventure3Sound::playMusic(float volume, int frames, unsigned flags) {
    if ((flags & 2) || music_ < 0) {
        return;
    }
    sound_.playBgm(music_, volume, seconds(frames));
}

void Adventure3Sound::stopMusic(int frames) {
    sound_.stopBgm(seconds(frames));
}

void Adventure3Sound::musicVolume(float volume, int frames) {
    sound_.bgmVolume(volume, seconds(frames));
}

bool Adventure3Sound::musicPlaying() {
    return sound_.bgmBusy();
}

void Adventure3Sound::openStream(Route route, int id, int part) {
    sound_.prepareStream(int(route), id, part, route == Event);
}

void Adventure3Sound::startStream(float volume, int frames, unsigned flags) {
    if (flags & 2) {
        return;
    }
    sound_.startStream(volume, seconds(frames));
}

void Adventure3Sound::stopStream(int frames) {
    sound_.stopStream(seconds(frames));
}

bool Adventure3Sound::streamOpening() {
    // 0205f4e4: state 1, the open still under way.
    return sound_.streamOpening();
}

bool Adventure3Sound::streamPlaying() {
    // 0205f4f8: state 2 (a start under way) or the driver's stream playing.
    return sound_.streamStarting() || sound_.streamBusy();
}

} // namespace picarat
