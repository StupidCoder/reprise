// The game's music: waves of the "*_bgm.sxd" sound banks, titled by the sound names that play them
// (resolved through requests, sequences, tone keymaps and interactive transitions, see
// data/SoundBank.hpp). Included: waves of at least `minSeconds`, and shorter named ones (jingles,
// win/lose stings). "_tv" names play the copy sent to the TV (social screen) output.
#pragma once

#include "audio/WaveDecoder.hpp"
#include "data/GameFiles.hpp"
#include "data/SoundBank.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace beacon::audio {

struct MusicTrack {
    std::string bank;                  // bank file stem, e.g. "canyon_bgm"
    std::size_t wave = 0;              // first layer
    std::vector<std::size_t> layers;   // waves started together (same title and length)
    std::uint32_t channels = 0, sampleRate = 0, samples = 0;
    bool loops = false;
    std::string title;               // best sound name (+ " [state]"), or "bank #wave"
    std::vector<std::string> uses;   // every "name [state]" that plays this wave
    bool tvOnly = false;             // only played by "_tv" names (TV output copy)
    double seconds() const { return sampleRate ? double(samples) / sampleRate : 0; }
};

class MusicCatalog {
public:
    void scan(const data::GameFiles& files, double minSeconds = 10, double minNamedSeconds = 1.5);
    // Data-only Reprise imports do not contain an executable.
    void scan(const std::vector<std::filesystem::path>& paths, double minSeconds = 10, double minNamedSeconds = 1.5);
    const std::vector<MusicTrack>& tracks() const { return tracks_; }
    // Sound names of a bank (without "_tv" variants).
    const std::vector<std::string>& bankNames(const std::string& bank) const;
    // The track a sound request plays (a use named exactly `request`, or its first interactive
    // state "request [state]"), or -1.
    int findRequest(const std::string& request) const;
    // An interactive request's level changes by variable state (e.g. the vocal layer follows
    // bgm_hero_state); empty for plain requests.
    std::vector<data::LevelRule> levelRules(const std::string& request) const;
    // Decodes a track; `loopStart`/`loopEnd` receive its loop points (or the whole track).
    bool decode(const MusicTrack& track, Pcm& out, std::size_t& loopStart, std::size_t& loopEnd,
                std::string* error = nullptr) const;

private:
    struct Bank {
        std::string stem;
        std::filesystem::path path;
        std::vector<std::string> names;
    };
    std::vector<Bank> banks_;
    std::vector<MusicTrack> tracks_;
    std::vector<data::LevelRule> levelRules_;
};

}   // namespace audio
