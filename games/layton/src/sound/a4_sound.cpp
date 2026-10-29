// The fourth adventure's sound requests. See a4_sound.hpp.
#include "sound/a4_sound.hpp"
#include "sound/sound_object.hpp"
#include <algorithm>

namespace picarat {
namespace {
float seconds(int frames) {
    return float(frames) / 60.0f;
}
} // namespace

Adventure4Sound::Adventure4Sound(GameSound& sound, std::map<int, std::array<int, 3>> sets)
    : sound_(sound), sets_(std::move(sets)) {
    // The sound object starts with SY bank 0, as the game's init does.
    banks_[0] = 0;
}

bool Adventure4Sound::selectSet(int set, int frames, unsigned flags) {
    // 020da39c: unknown sets change nothing.
    const auto it = sets_.find(set);
    if (it == sets_.end()) {
        return false;
    }
    set_ = set;
    const auto [music, ge, si] = it->second;
    selectMusic(music, frames, flags);
    loadBank(1, ge);
    loadBank(2, si);
    return true;
}

bool Adventure4Sound::loadBank(int group, int bank) {
    // 020da560: another bank replaces the group's; a negative one only
    // unloads it.
    if (group < 0 || group > 2) {
        return false;
    }
    auto& current = banks_[std::size_t(group)];
    if (bank == current) {
        return true;
    }
    sound_.unloadBank(unsigned(group));
    current = -1;
    if (bank >= 0) {
        sound_.loadBank(unsigned(group), bank);
        current = bank;
    }
    return true;
}

int Adventure4Sound::bank(int group) const {
    return group >= 0 && group < 3 ? banks_[std::size_t(group)] : -1;
}

void Adventure4Sound::playEffect(int id) {
    if (id < 0 || id >= 1000) {
        return;
    }
    sound_.playEffect(id);
    if (playing_.size() < 48 && std::find(playing_.begin(), playing_.end(), id) == playing_.end()) {
        playing_.push_back(id);
    }
}

void Adventure4Sound::stopEffect(int id, int frames) {
    sound_.stopEffect(id, seconds(frames));
}

bool Adventure4Sound::effectPlaying(int id) {
    return sound_.effectPlaying(id);
}

bool Adventure4Sound::anyEffectPlaying() {
    // 020d8bb0: the list keeps only what still plays.
    std::erase_if(playing_, [&](int id) { return !sound_.effectPlaying(id); });
    return !playing_.empty();
}

void Adventure4Sound::stopEffects(int frames) {
    for (int id : playing_) {
        sound_.stopEffect(id, seconds(frames));
    }
}

bool Adventure4Sound::selectMusic(int music, int frames, unsigned flags) {
    // 020da914: other music fades out first; music 0 is silence; it starts
    // at full volume unless flag 2.
    if (music < 0) {
        return false;
    }
    if (music == music_) {
        return false;
    }
    sound_.stopBgm(seconds(frames));
    music_ = music;
    if (music != 0) {
        sound_.switchBgm(music);
        if (!(flags & 2)) {
            sound_.playBgm(music, 1.0f, 0.0f);
        }
    }
    return true;
}

void Adventure4Sound::playMusic(float volume, int frames, unsigned flags) {
    if ((flags & 2) || music_ <= 0) {
        return;
    }
    sound_.playBgm(music_, volume, seconds(frames));
}

void Adventure4Sound::stopMusic(int frames) {
    sound_.stopBgm(seconds(frames));
}

void Adventure4Sound::musicVolume(float volume, int frames) {
    sound_.bgmVolume(volume, seconds(frames));
}

bool Adventure4Sound::musicPlaying() {
    return sound_.bgmBusy();
}

bool Adventure4Sound::musicFading() {
    return !sound_.bgmSettled();
}

void Adventure4Sound::openStream(Route route, int id, int a, int c) {
    // 020dacfc names the file; the event voice's three numbers travel as
    // id = a << 20 | b, part = c.
    const int request = route == Event ? (a << 20) | (id & 0xfffff) : id;
    streamOpen_ = sound_.prepareStream(int(route), request, c, route == Event);
}

void Adventure4Sound::startStream(float volume, int frames, unsigned flags) {
    if (flags & 2) {
        return;
    }
    sound_.startStream(volume, seconds(frames));
}

void Adventure4Sound::stopStream(int frames) {
    sound_.stopStreamFade(seconds(frames));
}

void Adventure4Sound::streamVolume(float volume, int frames) {
    sound_.streamVolume(volume, seconds(frames));
}

void Adventure4Sound::closeStream(int frames) {
    sound_.stopStream(seconds(frames));
    streamOpen_ = false;
}

bool Adventure4Sound::streamLoading() {
    return sound_.streamOpening();
}

bool Adventure4Sound::streamReady() {
    return streamOpen_ && sound_.streamOpen() && !sound_.streamOpening();
}

bool Adventure4Sound::streamPlaying() {
    return sound_.streamStarting() || sound_.streamBusy();
}

void Adventure4Sound::releaseAll(int frames) {
    for (int g = 0; g < 3; ++g) {
        loadBank(g, -1);
    }
    selectMusic(0, frames, 0);
    closeStream(frames);
}

} // namespace picarat
