#pragma once
// The stream players as the game's scripts see them: what is prepared,
// started, paused, fading or stopping on each player, advanced once per
// game frame so
// that the scripts' waits ("while a jingle plays") last as long as on the
// console whether or not sound is output. The audio itself (a56_sound)
// follows the same commands.
//
// From the code: the scripts' play services (11045 / 11046 / 11047 /
// 11051 / 11048 -> 0x2bc7a0 / 0x2bc84c -> 0x201d9c) queue commands for
// the sound thread (0x288094): 7 stop, 6 prepare, 1 start; 11049 queues
// start (0x201fb8), 11050 pause (0x202044), 11043 / 11044 stop (0x20228c
// / 0x202210). A player (vtable 0x400ca8) holds a sound handle (+0x30),
// a started flag (+0x10, set by start 0x1c0088) and a stop flag (+0x11,
// set by stop 0x1c01a4 and pause 0x1c014c). The handle has a pause state
// (+0x8b: 0 playing, 1 pausing, 2 paused, 3 resuming; 0x295934) with its
// fade (+0x74..+0x80) and a volume fade (+0x6c / +0x70: a start's fade-in
// and a stop's fade-out); it is released when its sound ends - a stop's
// fade run out, or a stream without loop played to its end.
//
// The scripts number six players (0 BGM, 1 EVT, 2 JNG, 3 EVS, 4 MOV,
// 5 SUR; the names' prefixes, 0x411ed0); the sound manager has three:
// table 0x3e69b8 maps them 0, 2, 2, 2, 2, 1 - music, ambience, and one
// player shared by events, jingles, voices and movie sound (a voice
// started stops a jingle). The services below take the scripts' numbers.
//
// The console needs some frames to prepare a stream (card reads); here
// a stream is prepared at once.
#include "sound/a56_stream.hpp"
#include <string>

namespace picarat::adventure56 {

class StreamPlayers {
public:
    static constexpr int kPlayers = 3;

    // A script player number's internal player (0x3e69b8), -1 if none.
    static int internal(int scriptPlayer) {
        static const int kMap[6] = {0, 2, 2, 2, 2, 1};
        return scriptPlayer >= 0 && scriptPlayer < 6 ? kMap[scriptPlayer] : -1;
    }

    // The 3DS's frame rate (268111856 / 4481136).
    static constexpr double kFrameRate = 59.8261;

    // 0x201d9c: play `name` on a player. Unless the player already has
    // that stream (and it is not stopping, and a start asked for is
    // already done), the current one stops (over `fade` frames), the new
    // one is prepared and, with `autoStart`, started. True when something
    // was queued (the service's result).
    bool play(int player, const std::string& name, const StreamInfo& info, int fade,
              bool autoStart);
    // 0x1c0088 (11049): start what is prepared, or resume a paused stream.
    void start(int player, int fade);
    // 0x1c014c (11050): pause over `frames`.
    void pause(int player, int frames);
    // 0x1c01a4 (11043 / 11044): stop over `frames` (0 at once).
    void stop(int player, int frames);
    // One display frame: the sound system runs in real time, also while
    // the game steps at 30 Hz (2400 2: a jingle of 98 display frames holds
    // the garden's verdict 98 frames on the console, not 196).
    void tick();

    // The scripts' queries.
    bool preparing(int player) const;  // 11041 (0x345f88)
    bool playing(int player) const;    // 11042 (0x345fc4)
    bool fading(int player) const;     // 11056 / 11058 (0x345e60 / 0x345ed0)
    bool stopFading(int player) const; // 11057 (0x345f54)
    bool paused(int player) const;     // pausing or paused (0x295af0)

    const std::string& name(int player) const {
        return players_[std::size_t(internal(player))].name;
    }

private:
    struct Fade {
        int total = 0, done = 0;

        int remaining() const { return done < total ? total - done : 0; }
    };

    struct Player {
        bool handle = false; // the sound handle exists
        std::string name;
        StreamInfo info;
        bool started = false, stopping = false;
        int pauseState = 0; // 0 playing, 1 pausing, 2 paused, 3 resuming
        Fade pauseFade, volumeFade;
        bool fadeEnds = false; // the volume fade is a stop's
        double position = 0;   // frames into the stream
    };

    Player* at(int scriptPlayer) {
        const int i = internal(scriptPlayer);
        return i < 0 ? nullptr : &players_[i];
    }

    const Player* at(int scriptPlayer) const {
        const int i = internal(scriptPlayer);
        return i < 0 ? nullptr : &players_[i];
    }

    Player players_[kPlayers];
};

} // namespace picarat::adventure56
