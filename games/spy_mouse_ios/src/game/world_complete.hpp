// The world's results after its boss fight is won for the first time (SceneWorldCompleteResults,
// layout guis/world_complete.gui): the iris closes on the bonus pattern while the level runs on
// purely, and the card says "World N Completed" with the goals the world's levels have won
// ("3/33") and either a call to play the world again or praise for winning them all.
// Reconstructed from SceneGame::updateGameMode @0x103368 (pushed instead of the level results in
// mode 2 on the first completion), SceneWorldCompleteResults::showScene @0x2199f8,
// ::updateDynamicLabels @0x218d14, ::update @0x218f90 and ::guiEvent @0x2187c8.
#pragma once

#include "game/results.hpp"

namespace doc::game {

class WorldCompleteScreen {
public:
    struct Setup {
        int world = 0;        // level data +0xc (0 for world 1)
        int goalsWon = 0;     // the world's levels' won goals (+0x138, +0x150, +0x168)
        int goalsTotal = 0;   // ... and their goals (+0x124, +0x13c, +0x154 set)
    };

    // showScene: the card and the continue button bounce in.
    void start(const Setup& setup) {
        setup_ = setup;
        active_ = true;
        card_ = BounceFrame{};
        continue_ = BounceFrame{};
        // (PLUS_FRAME_CONT pulses on its own from the start: first after 12 s, run lbf_win.)
        pulse_.start(12000);
    }
    // The goals as the profile has them once the level is recorded.
    void setGoals(int won, int total) {
        setup_.goalsWon = won;
        setup_.goalsTotal = total;
    }
    // Scene::update: the frames' animations.
    void updateFrames(int dtMs) {
        card_.update(dtMs);
        continue_.update(dtMs);
        pulse_.update(dtMs);
    }

    bool active() const { return active_; }
    const Setup& setup() const { return setup_; }
    // String 0x256 when every goal is won, else 0x25b.
    bool allGoals() const { return setup_.goalsWon == setup_.goalsTotal; }
    const BounceFrame& card() const { return card_; }
    const BounceFrame& continueFrame() const { return continue_; }
    float pulseScaleX() const { return pulse_.scaleX(); }
    // Frames squashing for the first time in their bounce (their wobble sound).
    int takeWobbles() {
        int n = 0;
        if (card_.takeWobble()) ++n;
        if (continue_.takeWobble()) ++n;
        return n;
    }
    float pulseScaleY() const { return pulse_.scaleY(); }

private:
    Setup setup_;
    bool active_ = false;
    BounceFrame card_, continue_;
    PlusPulse pulse_;
};

}  // namespace doc::game
