#include "game/results.hpp"

#include <algorithm>

namespace doc::game {

// --- BounceFrame --------------------------------------------------------------------------

void BounceFrame::update(int dtMs) {
    if (animating_) {
        // GuiBounceFrameCM::updateProgress @0x1dd8e8.
        progress_ += float(dtMs) * (showing_ ? 0.00300000003f : -0.00499999989f);
        if (showing_ && progress_ >= 1.0f) {
            progress_ = 1.0f;
            animating_ = false;
        } else if (!showing_ && progress_ <= 0.0f) {
            // Going out it stays one more update at 0 before it stops (+0x98).
            progress_ = 0.0f;
            if (lingered_) {
                lingered_ = false;
                animating_ = false;
            } else {
                lingered_ = true;
            }
        }
    }
    progress_ = std::clamp(progress_, 0.0f, 1.0f);
}

bool BounceFrame::takeWobble() {
    if (!animating_) wobbled_ = false;  // GuiBounceFrameCM::update @0x1dd8d6
    if (showing_ && progress_ > 0.4f && progress_ <= 0.6f && !wobbled_) {
        wobbled_ = true;
        return true;
    }
    return false;
}

void BounceFrame::goIn() {
    if (progress_ < 0.999899983f || animating_) {
        showing_ = true;
        animating_ = true;
    }
}

void BounceFrame::goOut() {
    showing_ = false;
    animating_ = true;
}

void BounceFrame::goQuick() {
    if (!animating_ && progress_ > 0.5f) {
        progress_ = 0.5f;
        showing_ = true;
        animating_ = true;
    }
}

void BounceFrame::scales(float& sx, float& sy) const {
    const float p = progress_;
    if (!showing_) {
        sx = sy = p;
    } else if (p <= 0.4f) {
        sx = sy = p * 2.5f;
    } else if (p <= 0.6f) {
        sx = (p - 0.4f) * -0.999999881f + 1.0f;
        sy = (p - 0.4f) * 1.00000012f + 1.0f;
    } else if (p <= 0.8f) {
        sx = (p - 0.6f) * 2.00000024f + 0.8f;
        sy = (p - 0.6f) * -2.00000024f + 1.20000005f;
    } else {
        sx = (p - 0.8f) * -1.00000036f + 1.20000005f;
        sy = p;
    }
}

// --- ResultsScreen ------------------------------------------------------------------------

void ResultsScreen::start(const Setup& setup) {
    *this = ResultsScreen{};
    setup_ = setup;
    active_ = true;
    // The retry and continue buttons wait for the counting (showScene: goOut); the medals
    // won before are lit at once (updateGoalItems(false) @0x1eb740).
    retryFrame_.goOut();
    continue_.goOut();
    for (std::size_t i = 0; i < 3; ++i) lit_[i] = setup_.goals[i].wonBefore;
    // (1.1.1) The cheese badge waits at 2.5 times its size; its count shows the balance from
    // before this level until it lands.
    cheeseScale_ = 2.5f;
    cheeseCount_ = cheeseTarget_ = float(setup_.cheeseBefore);
    popup_.reset(setup_.goalPopupHeight,
                 {setup_.goals[0].infoText, setup_.goals[1].infoText, setup_.goals[2].infoText});
}

void ResultsScreen::pressGoal(int i) {
    if (!active_ || i < 0 || i > 2) return;
    popup_.press(i);
    if (state_ == 7) timerMs_ = 0;  // guiEvent @0x1ec53a: the ring starts again
}

void ResultsScreen::updateFrames(int dtMs) {
    if (!active_) return;
    card_.update(dtMs);
    retryFrame_.update(dtMs);
    continue_.update(dtMs);
    cheeseFrame_.update(dtMs);
    for (BounceFrame& f : goalFrames_) f.update(dtMs);
    popup_.update(dtMs);
    pulse_.update(dtMs);
    // The continue button's pulse after results_time_out ends in a press of it
    // (SceneGameResults::guiEvent): the screen leaves for the map by itself.
    if (pulse_.takeFinished()) leave(false);
}

void PlusPulse::update(int dtMs) {
    if (!pulsing_) return;
    if (restMs_ > 0) {
        restMs_ -= dtMs;
        return;
    }
    t_ += float(dtMs) * 0.00200000009f;
    const float t = t_;
    if (curve_ == Curve::Cont) {
        float k;
        if (t <= 0.2f) k = t * -1.49999988f + 1.0f;
        else if (t <= 0.4f) k = (t + -0.2f) * 4.00000048f + 0.7f;
        else if (t <= 0.6f) k = (t + -0.4f) * -1.49999988f + 1.0f;
        else if (t <= 0.8f) k = (t + -0.6f) * 4.00000048f + 0.7f;
        else k = (t + -0.8f) * -2.50000024f + 1.5f;
        sx_ = sy_ = k;
    } else if (t <= 0.330000013f) {
        sx_ = t * -0.606060565f + 1.0f;
        sy_ = t * 0.606060743f + 1.0f;
    } else if (t <= 0.660000026f) {
        sx_ = (t + -0.330000013f) * 1.21212125f + 0.800000012f;
        sy_ = (t + -0.330000013f) * -1.21212125f + 1.20000005f;
    } else {
        sx_ = (t + -0.660000026f) * -0.588235497f + 1.20000005f;
        sy_ = (t + -0.660000026f) * 0.588235319f + 0.800000012f;
    }
    if (t_ >= 1.0f) {
        restMs_ = 12000;
        finished_ = true;
        t_ = 0.0f;
        sx_ = sy_ = 1.0f;
    }
}

void ResultsScreen::checkWobbles() {
    // The original plays the wobble while drawing the squashed frame, after the frame's updates.
    if (!active_) return;
    for (BounceFrame* f : {&card_, &retryFrame_, &continue_, &cheeseFrame_, &goalFrames_[0], &goalFrames_[1], &goalFrames_[2]})
        if (f->takeWobble()) sounds_.push_back(0x8a);
}

bool ResultsScreen::leave(bool retry) {
    if (!canLeave()) return false;
    retry_ = retry;
    pulse_ = PlusPulse{PlusPulse::Curve::Cont};  // (guiEvent @0x1ec14e: the pulse stops)
    continue_.goOut();
    card_.goOut();
    retryFrame_.goOut();
    cheeseFrame_.goOut();  // (popOutGuiItems @0x1eba64)
    setState(8);
    return true;
}

void ResultsScreen::update(int dtMs) {
    if (!active_) return;
    timerMs_ = int(double(timerMs_) + double(dtMs) * 0.75);
    updateCheeseCount(dtMs);
    const float score = float(setup_.score), bonus = float(setup_.bonus);
    // A count from 0 to `target` over results_count_time (1000 ms) of the screen's timer.
    auto count = [&](float target) {
        float v = target / 1000.0f * float(timerMs_);
        if (target > 0.0f) return std::min(std::max(0.0f, v), target);
        v = std::max(target, v);
        return v > 0.0f ? 0.0f : v;
    };
    const int pause = hurry_ ? 100 : 500;
    switch (state_) {
    case 0:
    case 1:
        // The card fades in (the second step also ends at a touch).
        alpha_ = std::min(1.0f, alpha_ + float(dtMs) * 0.00499999989f);
        if (alpha_ == 1.0f || (state_ == 1 && hurry_)) {
            alpha_ = 1.0f;
            setState(state_ == 0 ? 1 : 2);
        }
        break;
    case 2:
        // The points (the count sound starts once, looped).
        if (!countSoundStarted_) {
            countSoundStarted_ = true;
            if (score + bonus > 0.0f) sounds_.push_back(0x91);
        }
        points_ = count(score);
        if (points_ >= score || hurry_) {
            points_ = score;
            setState(3);
        }
        total_ = std::max(0.0f, points_ + bonusShown_);
        break;
    case 3:
        bonusShown_ = count(bonus);
        if (bonusShown_ >= bonus || hurry_) {
            bonusShown_ = bonus;
            sounds_.push_back(-0x91);
            // Medals won this time pop and light up; the buttons come in. A run that
            // delivered cheese shows the cheese badge first (LevelState +0x24).
            for (std::size_t i = 0; i < 3; ++i) {
                if (setup_.goals[i].won && !setup_.goals[i].wonBefore) goalFrames_[i].goQuick();
                lit_[i] = setup_.goals[i].won;
            }
            setState(setup_.delivered >= 1 ? 4 : 5);
            continue_.goIn();
            retryFrame_.goIn();
        }
        total_ = std::max(0.0f, points_ + bonusShown_);
        break;
    case 4:
        // The cheese badge drops in from twice its size; the count goes on after 500 ms.
        if (cheeseScale_ > 2.0f) {
            cheeseScale_ = 2.0f;
            cheeseShown_ = true;
            cheeseLanding_ = true;
        }
        if (timerMs_ > pause) setState(5);
        break;
    case 5:
        // A better total on a level completed before shows the best badge, growing from
        // twice its size; a new best holds the screen for 500 ms (100 when hurried).
        if (setup_.improvedScore && !badgeShown_) {
            badgeShown_ = true;
            badgeScale_ = 2.0f;
            badgeLanding_ = true;
        }
        if (!setup_.newBest || timerMs_ > pause) setState(7);
        break;
    case 7:
        // The ring round the continue button fills over results_time_out (14 s); then the
        // button starts pulsing.
        // (1.1.1 @0x1ec9e0: past the time-out the ring is cleared on every update.)
        outline_ = std::min(1.0f, float(timerMs_) / 14000.0f);
        if (timerMs_ > 14000 && timerMs_ >= 250) {
            if (!pulse_.pulsing()) pulse_.start(0);
            outline_ = 0.0f;
        }
        break;
    case 8:
        alpha_ = std::max(0.0f, alpha_ + float(dtMs) * -0.00999999978f);
        break;
    default:  // 6: a medal's info is open
        break;
    }
    if (state_ >= 5) badgeScale_ = std::max(1.0f, badgeScale_ + float(dtMs) * -0.00499999989f);
    if (state_ >= 4) cheeseScale_ = std::max(1.0f, cheeseScale_ + float(dtMs) * -0.00499999989f);
    // A badge landing plays the reward once (scale within 0.999..1.001); the cheese count then
    // climbs to the new balance.
    if (badgeLanding_ && badgeScale_ > 0.999000013f && badgeScale_ < 1.00100005f) {
        badgeLanding_ = false;
        sounds_.push_back(0x90);
    }
    if (cheeseLanding_ && cheeseScale_ > 0.999000013f && cheeseScale_ < 1.00100005f) {
        cheeseLanding_ = false;
        sounds_.push_back(0x90);
        cheeseTarget_ = float(setup_.cheeseAfter);
    }
}

void ResultsScreen::setState(int state) {
    state_ = state;
    timerMs_ = 0;
}

// SceneGameResults::updateCheeseDisplay @0x1eb658: the shown count moves a hundredth of the
// way per millisecond towards the balance, at least one a step.
void ResultsScreen::updateCheeseCount(int dtMs) {
    if (cheeseCount_ == cheeseTarget_) return;
    int step = int(float(dtMs) * (cheeseTarget_ - cheeseCount_) * 0.00999999978f);
    if (cheeseCount_ < cheeseTarget_) {
        cheeseCount_ += float(step < 1 ? 1 : step);
        if (cheeseCount_ > cheeseTarget_) cheeseCount_ = cheeseTarget_;
    } else {
        cheeseCount_ += float(step > -1 ? -1 : step);
        if (cheeseCount_ < cheeseTarget_) cheeseCount_ = cheeseTarget_;
    }
}

}  // namespace doc::game
