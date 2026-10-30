// Plays the session's sound events (game/sound.hpp) on the platform mixer: effects by their
// sound-table number (files read from the user's game, decoded once), ambience files, music.
#pragma once

#include "data/game_files.hpp"
#include "game/sound.hpp"
#include "platform/audio.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace doc::app {

class SoundOutput {
public:
    SoundOutput(const data::GameFiles& files, platform::Audio& audio);
    void play(const std::vector<game::SoundEvent>& events);
    // SoundList::playSoundOnce on the game clock: drops "once" events whose effect is still
    // sounding (by its length since it last started at `nowMs`), so replays decide like the
    // original whatever the audio device does. Also keeps SoundList's list of eight playing
    // effects: a ninth steals a voice (adding a stop). Keeps the events that go on.
    std::vector<game::SoundEvent> filter(const std::vector<game::SoundEvent>& events, std::int64_t nowMs);
    void stopAll();
    void stopEffects();  // everything but the music
    // Silent: events change only what would be sounding (the music and looping effects), as
    // when a quick save is restored by running the scene again; resume() then starts those.
    void setSilent(bool silent) { silent_ = silent; }
    void resume();
    // The file an event names (its table entry for effects).
    std::string fileOf(const game::SoundEvent& e) const;
    // Files that could not be read or decoded, and how many events were played.
    const std::vector<std::string>& missing() const { return missing_; }
    int played() const { return played_; }

private:
    std::shared_ptr<const platform::Pcm> load(const std::string& file);
    // A new decoder on a music file (its bytes read once); null when it cannot be played.
    std::shared_ptr<platform::MusicSource> openMusic(const std::string& file);

    const data::GameFiles& files_;
    platform::Audio& audio_;
    std::vector<std::string> table_;
    std::map<std::string, std::shared_ptr<const platform::Pcm>> cache_;
    std::map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>> musicFiles_;
    std::map<std::string, int> voices_;  // the latest voice of each file
    std::map<std::string, std::int64_t> soundingUntil_;  // game-clock end of each file's last start
    // SoundList's playing list (_g_vCurrentSounds): at most eight effects, each with a
    // priority (5, loops 10); entries leave once their effect has stopped sounding.
    struct Listed {
        std::string file;
        int priority = 5;
    };
    std::vector<Listed> listed_;
    bool sounding(const std::string& file, std::int64_t nowMs) const;
    std::vector<std::string> missing_;
    int played_ = 0;
    bool silent_ = false;
    game::SoundEvent music_;                         // the music playing (kind Music), if any
    bool musicOn_ = false;
    std::map<std::string, game::SoundEvent> loops_;  // looping effects playing, by file
};

}  // namespace doc::app
