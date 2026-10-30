// The results screen after a won level (SceneGameResults, layout guis/game_results.gui):
// the card bounces in over the level while an iris closes onto the bonus pattern, the points
// and then the time bonus count up, the medals the run earned light up and the retry and
// continue buttons bounce in. Reconstructed from SceneGameResults::showScene @0x1ed7c0 and
// ::update @0x1ec634; the card's items are animated frames (GuiBounceFrameCM @0x1dd8cc).
#pragma once

#include "game/goal_popup.hpp"

#include <array>
#include <vector>
#include <string>

namespace doc::game {

// A frame that pops in with a squash and stretch (GuiBounceFrameCM; GuiProgressFrame
// @0x20ddb0.. for going in and out). Its progress runs 0..1 at 0.003 per ms going in and
// back at 0.005 per ms going out.
class BounceFrame {
public:
    void update(int dtMs);   // GuiProgressFrame::update @0x20dfa4
    void goIn();             // @0x20dd2c
    void goOut();            // @0x20dd78
    void goQuick();          // @0x20ddb0: a shown frame pops again from half way
    // Horizontal and vertical scale about the frame's centre (GuiBounceFrameCM::getScales
    // @0x1dd920): growing to full size by 0.4, then squashed, stretched and settled.
    void scales(float& sx, float& sy) const;
    // A frame squashing for the first time in this bounce plays the wobble once
    // (GuiBounceFrameCM::getScales @0x1dda12, flags +0xb2/+0xb3; reset when it settles).
    bool takeWobble();
    float progress() const { return progress_; }
    // GuiProgressFrame::finishedAnim @0x20ddec: still, and all the way in or out.
    bool finished() const {
        return !animating_ && (showing_ ? progress_ >= 0.999899983f : progress_ <= 9.99999975e-05f);
    }
    bool visible() const { return progress_ >= 0.0001f; }  // GuiBounceFrame::render @0x1d6f54
    // GuiProgressFrame::isShowing @0x20de2c: more than half in, or still moving.
    bool isShowing() const { return progress_ > 0.5f || animating_; }
    // A frame made hidden (all the way out, still).
    static BounceFrame hidden() {
        BounceFrame f;
        f.showing_ = false;
        f.animating_ = false;
        return f;
    }

private:
    float progress_ = 0.0f;
    bool showing_ = true, animating_ = true;  // frames start going in when created
    bool wobbled_ = false;
    bool lingered_ = false;  // +0x98
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(progress_, showing_, animating_, wobbled_, lingered_); }
};

// A frame that pulses what it holds now and then (GuiPlusFrame::update @0x20fa98): after its
// rest (12 s), its scale curve runs for 500 ms, then it rests again. The default curve
// (@0x20f95c) squashes and stretches: x 1 → 0.8 → 1.2 → 1, y 1 → 1.2 → 0.8 → 1 in thirds; the
// results' continue button has its own (OnUpdateScale_Cont @0x1eb0bc): both ways 1 → 0.7 →
// 1.5, 1 → 0.7 → 1.5 → 1.
class PlusPulse {
public:
    enum class Curve { Squash, Cont };
    explicit PlusPulse(Curve curve = Curve::Squash) : curve_(curve) {}
    void start(int restMs) {
        pulsing_ = true;
        restMs_ = restMs;
        t_ = 0.0f;
        sx_ = sy_ = 1.0f;
    }
    void update(int dtMs);
    bool pulsing() const { return pulsing_; }
    // GuiPlusFrame::update @0x20fa98: each finished pulse "presses" the frame (guiEvent 0 to
    // its scene); true once per finished pulse.
    bool takeFinished() {
        const bool f = finished_;
        finished_ = false;
        return f;
    }
    // The scales, 1 when resting.
    float scaleX() const { return pulsing_ && restMs_ <= 0 ? sx_ : 1.0f; }
    float scaleY() const { return pulsing_ && restMs_ <= 0 ? sy_ : 1.0f; }

private:
    Curve curve_;
    bool pulsing_ = false;
    int restMs_ = 12000;
    float t_ = 0.0f, sx_ = 1.0f, sy_ = 1.0f;
    bool finished_ = false;
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(curve_, pulsing_, restMs_, t_, sx_, sy_, finished_); }
};

// One of the level's three medals (LevelGoal; Profile::isGoalAchieved @0x172648).
struct ResultsGoal {
    int kind = -1;          // index into the goal kinds (0 score, 8 time, 9 paths, ...)
    bool wonBefore = false; // lit from the start
    bool won = false;       // lit once the counting is done; pops if new
    std::string infoText;   // its line in the info popup ("Score 95")
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(kind, wonBefore, won, infoText); }
};

class ResultsScreen {
public:
    struct Setup {
        int score = 0;          // LevelState +0x2c: points of the cheese taken home
        int bonus = 0;          // LevelState +0x20: seconds left (0 when the clock was off)
        int playTimeMs = 0;     // LevelState +0x34
        std::array<ResultsGoal, 3> goals;
        bool newBest = false;        // a better total than the best saved before (or none saved)
        bool improvedScore = false;  // ... on a level completed before: the badge
        int delivered = 0;           // LevelState +0x24: cheese brought into holes this run
        int cheeseBefore = 0;        // the profile's cheese balance before this level ...
        int cheeseAfter = 0;         // ... and with this level's cheese added
        float goalPopupHeight = 0;   // the info popup's picture
        // Quick saves (state_io.hpp).
        template <class Io>
        void io(Io& io) { io(score, bonus, playTimeMs, goals, newBest, improvedScore, delivered, cheeseBefore,
                               cheeseAfter, goalPopupHeight); }
    };
    void start(const Setup& setup);   // SceneGameResults::showScene @0x1ed7c0
    // The frames' animations (Scene::update, before the level's own update) ...
    void updateFrames(int dtMs);
    // ... and the screen's steps after the level's update (SceneGameResults::update).
    void update(int dtMs);
    // After both: the frames squashing for the first time play their wobble.
    void checkWobbles();
    // A press on continue or retry (SceneGameResults::guiEvent @0x1ec0a8): the card and the
    // buttons go out, the screen fades (state 8). False while the buttons are not in yet.
    bool leave(bool retry);
    // A medal tapped: its line joins the info popup's queue (GuiPopupGoalInfo::queueGoalInfo
    // @0x226830); in the last step the ring starts again.
    void pressGoal(int i);
    const GoalPopup& popup() const { return popup_; }
    bool canLeave() const { return active_ && state_ >= 4 && state_ != 8; }
    // A touch down that no button took (SceneGameResults::mouseEvent @0x1ec610, +0x118): from
    // now on the counts jump to their ends and the pauses shorten.
    void hurry() { if (active_) hurry_ = true; }
    // The screen has gone; the level is left or played again.
    bool done() const { return state_ == 8 && alpha_ <= 0.0f && card_.progress() <= 0.0f; }
    bool retry() const { return retry_; }
    // Effects since the last call (sound table numbers; negative: stop that effect).
    std::vector<int> takeSounds() { return std::move(sounds_); }

    bool active() const { return active_; }
    int state() const { return state_; }
    int timerMs() const { return timerMs_; }
    float alpha() const { return alpha_; }
    float points() const { return points_; }
    float bonusShown() const { return bonusShown_; }
    float total() const { return total_; }
    int playTimeMs() const { return setup_.playTimeMs; }
    float outline() const { return outline_; }  // ring round the continue button, 0..1
    bool badgeShown() const { return badgeShown_; }
    float badgeScale() const { return badgeScale_; }  // +0x154, 2 → 1
    // (1.1.1) The cheese badge (+0x150, 2.5 → 2 → 1) and its count (+0x148).
    bool cheeseShown() const { return cheeseShown_; }
    float cheeseScale() const { return cheeseScale_; }
    int cheeseCount() const { return int(cheeseCount_); }
    // The continue button's pulse (GuiPlusFrame @0x20fa98, OnUpdateScale_Cont @0x1eb0bc):
    // its scale, 1 when resting.
    float pulseScale() const { return pulse_.scaleX(); }
    bool goalLit(int i) const { return lit_[std::size_t(i)]; }
    const ResultsGoal& goal(int i) const { return setup_.goals[std::size_t(i)]; }
    const BounceFrame& card() const { return card_; }
    const BounceFrame& retryFrame() const { return retryFrame_; }
    const BounceFrame& continueFrame() const { return continue_; }
    const BounceFrame& cheeseFrame() const { return cheeseFrame_; }  // FRAME_CHEESE_COUNT
    int delivered() const { return setup_.delivered; }
    const BounceFrame& goalFrame(int i) const { return goalFrames_[std::size_t(i)]; }

private:
    // SceneGameResults::setState @0x1ebab4: a new step starts its timer from 0.
    void setState(int state);
    void updateCheeseCount(int dtMs);

    Setup setup_;
    bool active_ = false;
    int state_ = 0;
    int timerMs_ = 0;            // +0x128, runs at three quarters of the game's speed
    float alpha_ = 0.0f;         // +0x140
    float points_ = 0.0f;        // +0x12c
    float bonusShown_ = 0.0f;    // +0x134
    float total_ = 0.0f;         // +0x13c
    float outline_ = 0.0f;       // +0x168
    bool countSoundStarted_ = false;
    bool retry_ = false;
    bool hurry_ = false;         // +0x120
    bool badgeShown_ = false;
    float badgeScale_ = 0.0f;    // +0x154 (0 until the step that shows it)
    bool badgeLanding_ = false;  // +0x164
    bool cheeseShown_ = false;   // item +0xf8 shown
    float cheeseScale_ = 2.5f;   // +0x150
    bool cheeseLanding_ = false; // +0x165
    float cheeseCount_ = 0.0f, cheeseTarget_ = 0.0f;  // +0x148, +0x14c
    PlusPulse pulse_{PlusPulse::Curve::Cont};
    std::array<bool, 3> lit_{};
    std::vector<int> sounds_;
    GoalPopup popup_;
    BounceFrame card_, retryFrame_, continue_, cheeseFrame_;
    std::array<BounceFrame, 3> goalFrames_;
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(setup_, active_, state_, timerMs_, alpha_, points_, bonusShown_, total_, outline_,
                           countSoundStarted_, retry_, hurry_, badgeShown_, badgeScale_, badgeLanding_,
                           cheeseShown_, cheeseScale_, cheeseLanding_, cheeseCount_, cheeseTarget_, pulse_,
                           lit_, sounds_, popup_, card_, retryFrame_, continue_, cheeseFrame_, goalFrames_); }
};

}  // namespace doc::game
