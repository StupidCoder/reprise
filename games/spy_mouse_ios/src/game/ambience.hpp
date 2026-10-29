// Ambient sound of a level: a looping background and occasional one-shot sounds at random
// intervals (the original's AmbiencePlayer: load @0x21098c, update @0x210650). Which scheme a
// map floor uses and what a scheme contains is data: res/mapNN/amb_schemes.xml.brd and
// res/sound/sound/amb/<scheme>/amb.xml.brd.
#pragma once

#include "data/game_files.hpp"

#include <string>
#include <vector>

namespace doc::game {

class GameRandom;

// A scheme as stored in the game's files.
struct AmbienceScheme {
    struct Group {
        float minDelay = 0, maxDelay = 0;  // seconds
        std::vector<std::string> sounds;   // paths under res/
        std::vector<int> lengthsMs;        // each sound's length (from its WAV header)
    };
    std::vector<std::string> loops;
    std::vector<Group> groups;

    // The scheme of `floor` on map `mapNumber` (empty if the map has none).
    static AmbienceScheme load(const data::GameFiles& files, int mapNumber, int floor);
    // A scheme by its folder under sound/sound/amb/ ("world1" for the first world's map).
    static AmbienceScheme loadNamed(const data::GameFiles& files, const std::string& name);
};

class Ambience {
public:
    // Starts a scheme; draws each group's first delay.
    void start(const AmbienceScheme& scheme, GameRandom& random);
    // Counts down; returns the sounds (paths under res/) that start this tick.
    std::vector<std::string> update(int dtMs, GameRandom& random);
    const std::vector<std::string>& loops() const { return scheme_.loops; }
    // One-shots still sounding: unloading the player stops their sources too (a source that
    // has finished is unbound by then).
    // `laterMs`: as it will be that much later (a reload inside a scene's update comes before
    // this frame's ambience step, but the sounds have played the frame's time).
    std::vector<std::string> sounding(int laterMs = 0) const;
    void stop() { scheme_ = {}, timersMs_.clear(), playing_.clear(), stopped_ = true; }
    bool stopped() const { return stopped_; }

private:
    AmbienceScheme scheme_;
    struct Playing {
        std::string file;
        int leftMs = 0;
    };
    std::vector<Playing> playing_;
    std::vector<int> timersMs_;
    bool stopped_ = false;
};

}  // namespace doc::game
