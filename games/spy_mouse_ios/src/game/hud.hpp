// The in-level HUD's state (SceneGame +0xb8..+0x120, +0x136b0/+0x136b4, +0x13850..+0x13864 and
// Player +0x4e4): three items (the trail pill, the score pill and the score-bonus panel) that
// slide between an "in" and an "out" offset, a fade timer that brightens the HUD after events,
// the score shown counting up to the real score, the trail's hand multiplier opening on the
// trail pill and its bounce. Driven by game events; drawn by render/hud_view.
#pragma once

#include "game/geometry.hpp"
#include "game/hands.hpp"

namespace doc::game {

struct HudItem {
    Vec2 position;      // current offset
    Vec2 target;
    Vec2 out, in;       // off screen / on screen offsets
    int timerMs = 0;    // > 0: slides in when it runs out; < 0: slides out
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(position, target, out, in, timerMs); }
};

class Hud {
public:
    enum Item { kTrail = 0, kScore = 1, kBonus = 2, kItems = 3 };

    // SceneGame::initHUD @0xfd0ac: the pills start 54 above their place, the bonus panel one
    // panel width to the right, and stay there (they come in when the intro ends).
    void init(float bonusPanelWidth);

    // SceneGame::updateHUD @0xfe3ac (item timers and easing) and the counters of
    // SceneGame::update @0x103af0.
    void update(int dtMs) {
        updateItems(dtMs);
        updateCounters(dtMs);
    }
    // SceneGame::updateHUD @0xfe3ac alone (the items and the pause button), which is what runs
    // under the pause menu.
    void updateItems(int dtMs);
    void updateCounters(int dtMs);
    // SceneGame::updateGameMode @0x10305e: the multiplier slot opens (to 20) while the hand is
    // worth more than 1, and the bounce runs 3 seconds.
    void updateHand(int dtMs, float handBonus);
    // Player::update @0x1269d6: the shown score climbs 0.5 per ms to the score.
    void countScore(int dtMs, int score, bool popupShowing);

    void show() { fadeMs = 3300; }                      // SceneGame::showHud
    void protractAll(int delayMs);                      // @0xf7360
    void retractAll(int delayMs);                       // @0xf73ac
    void bounceHand() { handBounce = 0.0f; }            // SceneGame::bounceHand

    HudItem items[kItems];
    int fadeMs = 0;            // +0x13864
    int scoreBounceMs = 0;     // +0x13850
    int glowMs = 0;            // +0x1385c: the glow behind a pill after a pulse
    bool glowLeft = false;     // +0x13860: behind the score pill (else the bonus panel)
    float handBounce = -1.0f;  // +0x136b0 (-1: idle)
    float handWidth = 0.0f;    // +0x136b4
    float shownScore = 0.0f;   // Player +0x4e4
    PokerHand hand = PokerHand::Bluff;  // Player +0x54c (the trail's current hand)
    int topType = -1, secondType = -1;   // Player +0x550/+0x554 (cheese kinds 0..4 of the hand)
    bool exitBonus = false;    // Player +0x510 (the last trail went into a home hole)
    // The pause button fades in and out (SceneGame +0x5770c/+0x57708, 0.005 per ms); it goes
    // away when the mouse enters the home hole (ObjControllerHideable::touchPlayer @0x1b4e2c).
    // SceneGame::showScene (0xe9800) shows it at full alpha when the level starts.
    bool pauseVisible = true;
    float pauseAlpha = 1.0f;
    bool pauseHidden = false;  // under the pause menu (SceneGame::hidePauseButton)
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(items, fadeMs, scoreBounceMs, glowMs, glowLeft, handBounce, handWidth, shownScore,
                           hand, topType, secondType, exitBonus, pauseVisible, pauseAlpha, pauseHidden); }
};

}  // namespace doc::game
