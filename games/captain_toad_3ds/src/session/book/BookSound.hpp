// =============================================================================================
// BookSound.hpp - what the course book sounds like: its music and its sound effects
// =============================================================================================
//
// The front end's sounds are data (DATA/Sound/FrontEnd, importer/SoundExport.hpp):
//   * sound effects belong to "users" in the sound database SeDataBase (CourseBook, CoursePageR,
//     ProductSequence, WipeStart, ...). A user plays a PlayInfo by name (0x2fa878 / 0x2f8994 /
//     0x2f80a4, called from the actors' code), or by an action: startAction (0x2fad5c) hands the
//     started tag to the actor's sound keeper (0x297ff0), and each PlayInfoInAction of that tag
//     plays as the action's frame passes its StartFrame (CourseBook "OpenBook": "Open" at frame 47,
//     the book's opening thud). The keeper is the opening's (audio/OpeningSoundKeeper.hpp).
//   * the music is the BGM line "CourseSelect": BgmPlayInfoList CourseSelect<season> names the
//     stream (Stm_Kp_bgm_Book01 ... 04). Situations (BgmSituationInfoList) change its volume:
//     ChangeVolume moves it towards TargetVolume by VolumeDiff per game tick (book-sound-a:
//     VolumeDownJingle at call 258 halves the music in 25 ticks, VolumeNormal at 318 brings it
//     back in 132). Choosing a course stops it with a 45-frame fade (input manager 0x1eab58 ->
//     0x299b4c -> 0x229f18, 0x32d328).
// Observed, not found in code: the stream's line level 77 (as the opening's 84 / 80 / 144; fitted
// to the capture of book-sound-a, residual 5e-5 at the DSP rate).
// =============================================================================================
#pragma once
#include "audio/OpeningSoundKeeper.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace book {

class Sound {
public:
    explicit Sound(const std::filesystem::path& dir);

    // A layout actor started `tag` (0x2fad5c -> 0x297ff0): its PlayInfoInAction entries play as the
    // action's frame (2 per tick) passes their StartFrame.
    void action(const std::string& actor, const std::string& db, const std::string& tag);
    // A PlayInfo of the user `db`. `param` >= 0 is the input of the PlayInfo's curves (CourseBook
    // NextPage / PrevPage: the book's turn value, 0.85 .. 1.15, which sets their volume).
    void play(const std::string& db, const std::string& playInfo, float param = -1);

    // The music: CourseSelect<season> (0x1b617c -> 0x229e98), a situation by name (0x2f6b38), the
    // stop with a fade in 60 Hz frames (0x229f18).
    void startMusic(int season);
    void situation(const std::string& name);
    void stopMusic(int fadeFrames);

    // Clocks: tick() once per 30 Hz game tick (keepers, situations, sound objects), frame() once per
    // 60 Hz frame (the middleware's scripts, the music's fade); render() pulls DSP frames.
    void tick();
    void frame();
    void render(int16_t* stereo, int dspFrames);
    captain_toad::audio::SoundSystem& system() { return *sys_; }

    static constexpr int kMusicLevel = 77;   // observed line level of the CourseSelect streams

private:
    struct Clock {
        std::string db, tag;
        float frame = 0;
        float end = 0;   // the last StartFrame: the clock is dropped after it
        bool started = false;
    };
    std::unique_ptr<captain_toad::audio::SoundSystem> sys_;
    std::unique_ptr<captain_toad::OpeningSoundKeeper::Keeper> keeper_;
    std::map<std::string, Clock> clocks_;   // by actor and tag
    std::map<std::string, std::string> courseSelect_;
    struct Situation {
        float target = 1, step = 0;
    };
    std::map<std::string, Situation> situations_;
    std::string music_;
    float volume_ = 1, target_ = 1, step_ = 0;
    int fadeFrames_ = 0, fadeAge_ = 0;
    captain_toad::OpeningSoundKeeper::Owner& owner(const std::string& name, const std::string& db);
};

}   // namespace book
