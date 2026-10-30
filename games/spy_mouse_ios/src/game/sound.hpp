// What the game asks the sound output to do (SoundList::playSound / playSoundOnce /
// stopSound @0x1db0.., MusicPlayer::play @0x1eb01c, the ambience player). The session collects
// these each tick; the app plays them. Effects are numbered as in the game's sound table
// (SoundList, 162 entries from the executable, see data::readSoundTable).
#pragma once

#include <string>

namespace doc::game {

struct SoundEvent {
    enum Kind {
        Play,       // an effect from the table (or a file for the ambience), restarted if playing
        PlayOnce,   // only if that effect is not playing already
        Stop,       // stop that effect (or ambience file)
        Music,      // switch the music to a file
        MusicStop,  // the music stops (MusicPlayer::pause)
    };
    Kind kind = Play;
    int effect = -1;      // sound table index, -1 when `file` is given
    std::string file;     // path under res/ (ambience sounds, music)
    bool loop = false;
};

// Effect numbers used by the reconstructed rules (index into the sound table).
namespace sfx {
constexpr int UiButton = 0x01;       // a GUI button released (GuiButton::s_nSnd)
constexpr int UiButton2 = 0x02;      // ... buttons given the other sound (setButtonAltSound)
constexpr int ExitReady = 0x23;      // the home hole opens
constexpr int WalkLeft1 = 0x2c;      // footsteps (+ rand() % 2)
constexpr int WalkRight1 = 0x2e;
constexpr int SwimLeft = 0x30;
constexpr int SwimRight = 0x31;
constexpr int UseExit = 0x46;        // into a home hole
constexpr int MouseDies = 0x4d;      // ui/mouse_dies: caught
constexpr int EatMunch = 0x49;       // ... bringing cheese
constexpr int UiSnap = 0x4b;         // after a pickup
constexpr int PickupFood = 0x6e;     // cheese picked up
constexpr int ScoreNote1 = 0x7c;     // score notes (table @0x525210)
constexpr int ScoreBonus = 0x89;
constexpr int UiWobble = 0x8a;       // a results frame squashes
constexpr int UiWoosh = 0x8c;        // level won
constexpr int UiReward = 0x90;       // the better-score badge lands
constexpr int ScoreCountLoop = 0x91; // results counting
constexpr int TextPop = 0x93;        // the "Mission Accomplished" text appears
constexpr int SneakLeft = 0x96;      // footsteps while holding something
constexpr int SneakRight = 0x97;
constexpr int Bush = 0xad;           // a bush starts rustling
}  // namespace sfx

}  // namespace doc::game
