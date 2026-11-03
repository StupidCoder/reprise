// Movie prompts (class movie_prompt, vtable 0x16a4608, constructor 0x58b560, setup 0x58bb10 /
// 0x58bd20, update 0x58c3e0). Findings: .analysis/docs/objects.md "Movie prompts".
//
// A prompt shows a tutorial movie (MovieName under data/common/video, .mp4) on a hologram board
// (prompt_board) held by a HoloBoard device (class at 0x2009e0, type 1: the mup_co_holoboard
// model and a sub-board of locators, mup_holoboard_board_noborder_temp) once the player reaches
// it, until the player has done what it shows:
//   0 waiting:   ShowTrigger fires (an empty link counts as fired) -> 1, timer 0.
//   1 delay:     after ShowDelayTime the device is placed at the prompt and opened (0x203300:
//                device "open_to_idle", sub-board "open") -> 2. A hide request meanwhile -> 0.
//   2 starting:  the device is open (its state 2: the sub-board's open clip has ended) -> the
//                movie starts (0x58ce00), the board shows, clip "show" -> 3.
//   3 showing:   "show" ends -> "idle" (loop) -> 4.
//   4 playing:   a hide request -> 5, timer 0.
//   5 lingering: after 0.5 s (0x120c014) "hide" -> 7.
//   7 hiding:    "hide" ends -> the movie stops (0x58c250), the board hides, the device closes
//                (0x2035f0: "close") -> 6.
//   6 stopping:  the device is closed (its state 0: the close clips have ended) -> 8 (done).
// A hide request (checked every update outside 0 and 8, 0x58c9a0) is the HideTrigger firing, or
// the hero being in the state the prompt teaches: HeroMove Walk (2), HeroJump Jump (3), HeroHover
// Hover (6), HeroPunch Punch (8), HeroThrow Throw (0x15). Generic prompts wait for the trigger.
#pragma once

#include "game/LevelRuntime.hpp"
#include "math/Transform.hpp"

#include <cstdint>
#include <string>

namespace beacon::game {

struct Level;
enum class HeroState : std::uint8_t;

// PromptType's values (the enum of movie_prompt.lox).
enum class PromptType : int {
    HeroMove = 0, HeroJump = 1, HeroHover = 2, HeroPunch = 3, HeroThrow = 4, VrPlayerHeadbutt = 5,
    GadgetRopeFlick = 6, GadgetRopePull = 7, GadgetSlingShot = 8, LostBot = 9, SoccerHeading = 10,
    Generic = 100
};

struct MoviePromptSetup {
    std::size_t object = 0;
    math::Mat4 world = math::identity();
    PromptType type = PromptType::Generic;
    std::string movie;            // MovieName, e.g. "prompt/mup_prompt_move_hero"
    LevelLink show, hide;
    float showDelay = 0;          // ShowDelayTime
    bool onTouchPad = false;      // Location TouchPad: shown on the controller (not ported)
};
MoviePromptSetup moviePromptSetup(const Level& level, const LevelRuntime& runtime, std::size_t index);

class MoviePrompt {
public:
    enum class Phase { Waiting, Delay, Starting, Showing, Playing, Lingering, Stopping, Hiding, Done };

    explicit MoviePrompt(MoviePromptSetup setup) : setup_(std::move(setup)) {}

    // What the drawn parts report back (MoviePromptView), read by the next update.
    struct Feedback {
        bool showEnded = false, hideEnded = false;   // the board's show / hide clip has ended
        bool deviceOpen = false, deviceClosed = false;
    };
    void update(float dt, const LevelRuntime& runtime, HeroState hero, const Feedback& view);

    const MoviePromptSetup& setup() const { return setup_; }
    Phase phase() const { return phase_; }
    bool boardShown() const { return shown_; }
    bool moviePlaying() const { return playing_; }
    // The device: opened at the end of the delay, closed after the board's hide clip.
    bool deviceOpened() const { return deviceOpened_; }
    // The board's clip ("show", "idle", "hide"; empty before the first) and a counter that changes
    // with every new request.
    const std::string& clip() const { return clip_; }
    std::uint32_t clipSerial() const { return clipSerial_; }

private:
    bool hideRequested(const LevelRuntime& runtime, HeroState hero) const;
    void play(const char* name);

    MoviePromptSetup setup_;
    Phase phase_ = Phase::Waiting;
    float timer_ = 0;
    bool hideRequest_ = false;   // +0x27ba
    bool shown_ = false, playing_ = false, deviceOpened_ = false;
    std::string clip_;
    std::uint32_t clipSerial_ = 0;
};

}   // namespace game
