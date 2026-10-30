// (1.1.1) The in-level power-up bar (SceneGame::initGuiElements @0xf9172, guiEvent @0x103d80,
// update @0x103528; research notes .analysis/docs/powerups.md): the power-up button at the top
// left slides a list of the six power-ups down from the top (hud/popup_powerup.png, X-ray specs
// first), each row a button showing its count; a tap on a row uses that power-up and the bar
// goes back up. While the bar shows, the level waits (SceneGame::update runs only its paused
// part) and darkens; the HUD's pills go out and come back when it leaves.
//
// The layout is the original's (numbers from initGuiElements, positions checked against runs of
// the original): the bar's picture at (2, 27) (y down), its list 10 in and 59 above the
// picture's bottom, 92.5 x 187, one 50-high row per power-up, every second row on a light
// backing.
#pragma once

#include "game/geometry.hpp"
#include "game/profile.hpp"
#include "game/progress_frame.hpp"
#include "game/results.hpp"

#include <array>

namespace doc::game {

class PowerupBar {
public:
    // The bar's GUI items, by the original's ids (GuiItem ids; rows are 100 + type).
    enum Item { kNone = -1, kPowerupButton = 1, kArrow = 4, kStore = 5, kRowFirst = 100, kContinue = 0xeb3fb22 };

    // The bar slides with a GuiSlideFrame of rate 6, easing out going in and linear going out,
    // from 0 (out, above the screen) to 1; it starts out.
    PowerupBar() : slide_(6.0f, ProgressFrame::Mode::EaseOut, ProgressFrame::Mode::Linear) { slide_.setOut(); }

    const ProgressFrame& slide() const { return slide_; }
    ProgressFrame& slide() { return slide_; }
    // SceneGame::getPowerupsVisible @0xf7294.
    bool showing() const { return slide_.showing(); }
    // The list's scroll (points the rows are moved up by; 0 shows the first row at the top).
    float scroll() const { return scroll_; }
    void scrollToTop() { scroll_ = 0.0f; }

    // Screen geometry (y down) of the bar at full progress.
    static constexpr float kLeft = 2.0f, kTop = 27.0f;   // hud/popup_powerup.png, 118 x 292
    static constexpr float kListLeft = 12.0f, kListTop = 73.0f, kListWidth = 92.5f, kListHeight = 187.0f;
    static constexpr float kRowHeight = 50.0f;
    // The bar's vertical offset now (GuiSlideFrame::render: (1 - progress) x its height, up).
    float offsetY(float barHeight) const { return -(1.0f - slide_.progress()) * barHeight; }

    // The row (bar order index) under a screen point while the bar is in, or -1.
    int rowAt(Vec2 screen, float barHeight) const;

    // The tutorial's state (SceneGame +0x57c20; setPowerupTutorialState @0xf73ec): 0 off, 1..5
    // Digger's tutorial, 6 a power-up's "New Power-up!" card. While it is not 0 the level waits.
    int tutorial() const { return tutorial_; }
    void setTutorial(int state) { tutorial_ = state; }
    // The card (+0x57c04, ui/popup_powerup_info.png, a GuiBounceFrameCM with its wobble sound)
    // and the power-up it shows (+0x57c0c).
    const BounceFrame& card() const { return card_; }
    BounceFrame& card() { return card_; }
    int cardType() const { return cardType_; }
    void setCardType(int t) { cardType_ = t; }
    // The card's arrow is highlighted while the card shows (GuiScaleButton::setHighlight
    // @0x289f4c): its pressed picture over it at (sin(t x 0.01) + 1) / 2, t the time since
    // (counted in its update, +0xb0).
    // Digger's tutorial (layout guis/powerups_intro_tutorial.gui, SceneGame +0x57c24): his
    // slide frame (FRAME_CHARACTER: rate 6, easing out going in, from the right), the speech
    // bubble's bounce frame (TUTORIAL_FRAME) and the continue button (BTN_CONTINUE), shown and
    // highlighted 750 ms after states 1, 4 and 2-by-button (+0x57cbc). The layout is updated
    // only while the tutorial runs. `text` is the string Digger says (setPowerupTutorialText).
    struct Tutorial {
        ProgressFrame digger{6.0f, ProgressFrame::Mode::EaseOut, ProgressFrame::Mode::Linear};
        BounceFrame bubble;
        int text = -1;
        int continueDelayMs = 0;
        bool continueShown = false;
        int continueHighlightMs = 0;
        bool byButton = false;   // +0x139b9: opened by the power-up button
        // Quick saves (state_io.hpp).
        template <class Io>
        void io(Io& io) { io(digger, bubble, text, continueDelayMs, continueShown, continueHighlightMs, byButton); }
    };
    const Tutorial& digger() const { return digger_; }
    Tutorial& digger() { return digger_; }
    int arrowHighlightMs() const { return arrowHighlightMs_; }
    void restartArrowHighlight() { arrowHighlightMs_ = 0; }
    void updateHighlights(int dtMs) {
        if (tutorial_ == 6) arrowHighlightMs_ += dtMs;
    }
    // The layout's GUI update (SceneGame::update @0x103528, while the tutorial runs).
    void updateTutorialLayout(int dtMs) {
        digger_.digger.update(dtMs);
        digger_.bubble.update(dtMs);
        if (digger_.continueShown) digger_.continueHighlightMs += dtMs;
    }
    // The card's box at full size (y down): a child of the slide frame, 10 in from its right
    // and centred 10 below its middle.
    static constexpr float kCardLeft = 110.0f, kCardTop = 77.5f, kCardWidth = 272.0f, kCardHeight = 184.0f;
    // Its arrow button (item 4, ui/btn_arrow.png, 60 x 69) and touch box.
    static constexpr float kArrowLeft = 226.0f, kArrowTop = 213.0f, kArrowWidth = 60.0f, kArrowHeight = 69.0f;

private:
    ProgressFrame slide_;  // SceneGame +0x57bf4
    float scroll_ = 0.0f;  // the scroll frame's offset (+0x57bf8)
    int tutorial_ = 0;
    BounceFrame card_ = BounceFrame::hidden();
    int cardType_ = -1;
    int arrowHighlightMs_ = 0;
    Tutorial digger_ = [] {
        Tutorial t;
        t.digger.setOut();
        return t;
    }();
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(slide_, scroll_, tutorial_, card_, cardType_, arrowHighlightMs_, digger_); }
};

}  // namespace doc::game
