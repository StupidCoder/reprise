#pragma once
// Parts the first adventure's move-limited puzzles (Match, Coin) share:
// the moves-left counter at the bottom of the lower screen, which hops
// when the count changes, and the "no more moves" panel.
#include "system/scene_kit.hpp"
#include <cmath>
#include <string>

namespace picarat::adventure1 {

// 02050ca0 / 02050d88 (Match) and 02052f48 / 020530d4 (Coin): a digit of
// matchnum.spr (animation moves + 1) centred at (0x34, 0xb4), half size;
// when the count changes it hops up 25 pixels and swells over a turn of
// 0.2 a frame.
class MovesCounter {
public:
    void load(SceneKit& kit) { digit_ = kit.load("matchnum.spr", 0, 0, 1); }

    // The count changed this frame.
    void hop() {
        hopping_ = true;
        phase_ = 0;
        swell_ = 1.0f;
    }

    void stop() { hopping_ = false; }

    void draw(SceneKit& kit, int moves) {
        int lift = 0;
        float scale = 0.5f;
        if (hopping_) {
            phase_ += 0.2f;
            lift = int(25.0f * std::sin(phase_ / 2.0f));
            swell_ += 0.5f * std::sin(phase_);
            if (phase_ >= 6.28318f) {
                hopping_ = false;
            }
            scale = swell_ / 2.0f;
        }
        kit.select(digit_, unsigned(moves + 1));
        digit_.x = 0x34;
        digit_.y = 0xb4 - lift;
        kit.drawScaled(digit_, scale); // 0200ca4c: centred
    }

private:
    SceneKit::SpriteObject digit_;
    bool hopping_ = false; // +0xbc / +0x230
    float swell_ = 1.0f;   // +0xc0 / +0x234
    float phase_ = 0;      // +0xc4 / +0x238
};

// The "no more moves" panel (?/nomoretouch.bgx on lower BG2, 0200a0dc /
// 02009650): shown while the player presses an unchanged piece with no
// moves left. The check at the start of a frame sees the last frame's
// state, and loading or clearing the layer shows a frame later still (the
// upload waits for the VBlank).
class NoMovesPanel {
public:
    void update(SceneKit& kit, bool refused) {
        kit.lowerOverlay = pending_;
        pending_ = refused ? kit.localized("?/nomoretouch") : std::string();
    }

    static void clear(SceneKit& kit) { kit.lowerOverlay.clear(); }

private:
    std::string pending_;
};

} // namespace picarat::adventure1
