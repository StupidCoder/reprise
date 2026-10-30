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
    popup_.reset(setup_.goalPopupHeight,
                 {setup_.goals[0].infoText, setup_.goals[1].infoText, setup_.goals[2].infoText});
}

void ResultsScreen::pressGoal(int i) {
    if (!active_ || i < 0 || i > 2) return;
    popup_.press(i);
    if (state_ == 6) timerMs_ = 0;  // guiEvent @0x1ec53a: the ring starts again
}

void ResultsScreen::updateFrames(int dtMs) {
    if (!active_) return;
    card_.update(dtMs);
    retryFrame_.update(dtMs);
    continue_.update(dtMs);
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
    for (BounceFrame* f : {&card_, &retryFrame_, &continue_, &goalFrames_[0], &goalFrames_[1], &goalFrames_[2]})
        if (f->takeWobble()) sounds_.push_back(0x8a);
}

bool ResultsScreen::leave(bool retry) {
    if (!canLeave()) return false;
    retry_ = retry;
    continue_.goOut();
    card_.goOut();
    retryFrame_.goOut();
    state_ = 7;
    timerMs_ = 0;
    return true;
}

void ResultsScreen::update(int dtMs) {
    if (!active_) return;
    timerMs_ = int(double(timerMs_) + double(dtMs) * 0.75);
    const float score = float(setup_.score), bonus = float(setup_.bonus);
    // A count from 0 to `target` over results_count_time (1000 ms) of the screen's timer.
    auto count = [&](float target) {
        float v = target / 1000.0f * float(timerMs_);
        if (target > 0.0f) return std::min(std::max(0.0f, v), target);
        v = std::max(target, v);
        return v > 0.0f ? 0.0f : v;
    };
    switch (state_) {
    case 0:
    case 1:
        alpha_ = std::min(1.0f, alpha_ + float(dtMs) * 0.00499999989f);
        if (alpha_ == 1.0f) {
            state_ = state_ == 0 ? 1 : 2;
            timerMs_ = 0;
        }
        break;
    case 2:
        // The points (the count sound starts once, SoundList 0x87).
        if (!countSoundStarted_) {
            countSoundStarted_ = true;
            if (score + bonus > 0.0f) sounds_.push_back(0x91);  // looped (playSound(0x91, true))
        }
        points_ = count(score);
        if (points_ >= score || hurry_) {
            points_ = score;
            state_ = 3;
            timerMs_ = 0;
        }
        total_ = std::max(0.0f, points_ + bonusShown_);
        break;
    case 3:
        bonusShown_ = count(bonus);
        if (bonusShown_ >= bonus || hurry_) {
            bonusShown_ = bonus;
            // (Stopping a sound that never started does nothing: no points, no count sound.)
            if (score + bonus > 0.0f) sounds_.push_back(-0x87);
            state_ = 4;
            timerMs_ = 0;
            // Medals won this time pop and light up; the buttons come in.
            for (std::size_t i = 0; i < 3; ++i) {
                if (setup_.goals[i].won && !setup_.goals[i].wonBefore) goalFrames_[i].goQuick();
                lit_[i] = setup_.goals[i].won;
            }
            continue_.goIn();
            retryFrame_.goIn();
        }
        total_ = std::max(0.0f, points_ + bonusShown_);
        break;
    case 4:
        if (timerMs_ >= 1400 || hurry_) {
            state_ = 5;
            timerMs_ = 0;
        }
        break;
    case 5:
        // A new best holds the screen for 500 ms (100 when hurried); bettering an earlier
        // completion also shows the badge, growing from twice its size.
        if (setup_.improvedScore && !badgeShown_) {
            badgeShown_ = true;
            badgeScale_ = 2.0f;
        }
        if (!setup_.newBest || timerMs_ > (hurry_ ? 100 : 500)) {
            state_ = 6;
            timerMs_ = 0;
        }
        break;
    case 6:
        // The ring round the continue button fills over results_time_out (14 s); then the
        // button starts pulsing.
        outline_ = std::min(1.0f, float(timerMs_) / 14000.0f);
        if (timerMs_ > 14000 && timerMs_ >= 250 && !pulse_.pulsing()) {
            pulse_.start(0);
            outline_ = 0.0f;
        }
        break;
    case 7:
        alpha_ = std::max(0.0f, alpha_ + float(dtMs) * -0.00999999978f);
        break;
    default:
        break;
    }
    if (state_ >= 5) badgeScale_ = std::max(1.0f, badgeScale_ + float(dtMs) * -0.00499999989f);
    // The badge landing plays the reward once (update @0x1ed4b4: scale within 0.999..1.001).
    if (badgeShown_ && !badgeLanded_ && badgeScale_ > 0.999000013f && badgeScale_ < 1.00100005f) {
        badgeLanded_ = true;
        sounds_.push_back(0x90);
    }
}

}  // namespace doc::game
