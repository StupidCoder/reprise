// (1.1.1) The power-up bar over the level (see powerup_bar.hpp): its GUI buttons, opening and
// closing it, and using a power-up (SceneGame::guiEvent @0x103d80).
#include "game/session.hpp"

namespace doc::game {

void Session::setPowerups(const std::array<PowerupRecord, kPowerupCount>& records, bool tutorialDone, bool metDigger,
                          bool lateLevel) {
    powerups_ = records;
    powerupTutorialDone_ = tutorialDone;
    metDigger_ = metDigger;
    lateLevel_ = lateLevel;
    // SceneGame::startGame @0x1067b0: an attempt starts with the temporary ones off
    // (Profile::turnOffTemporaryPowerups), and Digger's tutorial starts by itself while the
    // button's has not run, outside challenges, for a player who has met him (the store's
    // introduction) or in a level from world 1's sixth or a later world's second on (the
    // profile's current level: Profile +8, +9; level data +0xc world, +0x10 its index).
    for (PowerupRecord& r : powerups_)
        if (r.kind == 1) r.active = false;
    bar_.setTutorial(0);
    if (!tutorialDone && (metDigger || lateLevel)) {
        // (Then it counts as run, and neither the finger nor the level intro is pushed.)
        setPowerupTutorial(1);
        powerupTutorialDone_ = true;
        // (With no intro on top, the boot's iris event reaches the game: everything pulses.)
        if (intro_.active && attempts_ == 0 && !cutscene_.active) irisShutOnBoot();
        intro_.active = false;
    }
}

// SceneGame::setPowerupTutorialState @0xf73ec.
void Session::setPowerupTutorial(int state) {
    PowerupBar::Tutorial& d = bar_.digger();
    int text = -1;
    switch (state) {
    case 0:
        d.byButton = false;
        break;
    case 1:
        // The HUD's pills out, the pause button hidden (the power-up button too, unless it
        // opened the bar); Digger slides in.
        hud_.retractAll(0);
        hud_.pauseHidden = true;
        powerupButtonHidden_ = !d.byButton;
        d.digger.goIn();
        text = metDigger_ ? 0x6aa : 0x6a8;
        break;
    case 2:
        if (d.byButton) {
            text = 0x6ac;
            break;
        }
        // The power-up button shows, highlighted with an arrow ("try it now").
        powerupButtonHidden_ = false;
        powerupButtonHighlightMs_ = 0;
        powerupButtonHighlighted_ = true;
        setPowerupTutorialText(0x6ab);
        d.continueShown = false;
        bar_.setTutorial(state);
        d.continueDelayMs = 0;
        return;
    case 3:
        // None to use: the store button is highlighted ("hit the button down there").
        d.continueShown = false;
        storeHighlighted_ = true;
        storeHighlightMs_ = 0;
        text = d.byButton ? 0x6b1 : 0x6b0;
        break;
    case 4:
        text = 0x6ae;
        break;
    case 5:
        // Digger slides out; the HUD's buttons come back.
        if (d.digger.goingIn() || d.digger.progress() > 0.0f) d.digger.goOut();
        hud_.pauseHidden = false;
        powerupButtonHidden_ = false;
        break;
    default:
        break;
    }
    if (text >= 0) setPowerupTutorialText(text);
    if (state != 2) d.continueShown = false;
    // The continue button waits 750 ms after states 1, 4 and 2-by-button.
    d.continueDelayMs = (state == 1 || state == 4 || (state == 2 && d.byButton)) ? 750 : 0;
    bar_.setTutorial(state);
}

void Session::setPowerupTutorialText(int text) { bar_.digger().text = text; }

// The level's highlighted buttons pulse on their own clocks (GuiScaleButton::update +0xb0).
void Session::updatePowerupHighlights(int dtMs) {
    if (powerupButtonHighlighted_) powerupButtonHighlightMs_ += dtMs;
    if (storeHighlighted_) storeHighlightMs_ += dtMs;
    bar_.updateHighlights(dtMs);
}

bool Session::powerupAvailable(Powerup t) const {
    const PowerupRecord& r = powerups_[std::size_t(t)];
    return r.kind < 2 && (r.count > 0 || r.count == -1);
}

// SceneGame::getPowerupEnabled @0xf6ee8 (without its message).
bool Session::powerupEnabled(Powerup t) const {
    const std::string& mode = level_.info().mode;
    if (mode == "infinite" && (t == Powerup::Banana || t == Powerup::Invinciball)) return false;
    if (mode == "boss" && t != Powerup::Kiska) return false;
    if (t == Powerup::Kiska) {
        // Not for secret levels, nor levels skipped or completed before (HYPOTHESIS: the
        // level data's +0x6f is the secret flag; skips are not recorded by the port yet).
        if (level_.info().secret || wonBefore_) return false;
    }
    if (t == Powerup::XRay) {
        // Refused when every cat is the boss cat (0x36) and nothing hidden (0x1e) is there.
        bool onlyBoss = true;
        for (const Cat& c : cats_) onlyBoss = onlyBoss && c.type() == 0x36;
        bool hidden = false;
        for (const LevelObject& o : objects_) hidden = hidden || o.type == 0x1e;
        if (onlyBoss && !cats_.empty() && !hidden) return false;
        if (cats_.empty() && !hidden) return false;
    }
    // Dropped where the mouse stands: not while it hides.
    if ((t == Powerup::Banana || t == Powerup::FlourBomb) && mouse_.state == 2) return false;
    return true;
}

// The GUI item under a screen point: the card's arrow while a card shows (the rows and the
// button are disabled then), else the bar's rows while it is in, then the power-up button.
int Session::powerupItemAt(Vec2 screen) const {
    auto inScaleButton = [&](float x, float y, float w, float h) {
        // (GuiScaleButton: its touch box is 1.25 x its size from its corner.)
        return screen.x >= x && screen.x < x + w * 1.25f && screen.y >= y && screen.y < y + h * 1.25f;
    };
    const int tutorial = bar_.tutorial();
    if (tutorial >= 1 && tutorial <= 5) {
        // Digger's tutorial: its continue button once shown (BTN_CONTINUE at (145, 5) from the
        // bottom, 203.5 x 32: y down 283), the power-up button he asks for (state 2), the store
        // button he points at (state 3).
        const PowerupBar::Tutorial& d = bar_.digger();
        if (d.continueShown && inScaleButton(145.0f, 283.0f, 203.5f, 32.0f)) return PowerupBar::kContinue;
        if (tutorial == 2 && !d.byButton && inScaleButton(kPowerupTouch[0], kPowerupTouch[1], 46.0f, 46.0f))
            return PowerupBar::kPowerupButton;
        const float storeY = PowerupBar::kTop + bar_.offsetY(kBarHeight) + kBarHeight - 14.0f - 32.0f;
        if (tutorial == 3 && inScaleButton(PowerupBar::kLeft + 10.0f, storeY, 100.0f, 32.0f)) return PowerupBar::kStore;
        return PowerupBar::kNone;
    }
    if (tutorial == 6) {
        // (GuiScaleButton: its touch box is 1.25 x its size from its corner, as for the
        // power-up button.)
        const bool in = bar_.card().progress() >= 1.0f;
        const float x = PowerupBar::kArrowLeft, y = PowerupBar::kArrowTop;
        if (in && screen.x >= x && screen.x < x + PowerupBar::kArrowWidth * 1.25f && screen.y >= y &&
            screen.y < y + PowerupBar::kArrowHeight * 1.25f)
            return PowerupBar::kArrow;
        return PowerupBar::kNone;
    }
    if (bar_.showing()) {
        const int row = bar_.rowAt(screen, kBarHeight);
        if (row >= 0) return PowerupBar::kRowFirst + Profile::kBarOrder[std::size_t(row)];
        const float storeY = PowerupBar::kTop + bar_.offsetY(kBarHeight) + kBarHeight - 14.0f - 32.0f;
        if (inScaleButton(PowerupBar::kLeft + 10.0f, storeY, 100.0f, 32.0f)) return PowerupBar::kStore;
    }
    // The power-up button (hud/hud_powerup.png at (1, 2), 24 x 24) answers within its touch
    // box (GuiScaleButton).
    if (screen.x >= kPowerupTouch[0] && screen.x < kPowerupTouch[0] + kPowerupTouch[2] && screen.y >= kPowerupTouch[1] &&
        screen.y < kPowerupTouch[1] + kPowerupTouch[3])
        return PowerupBar::kPowerupButton;
    return PowerupBar::kNone;
}

// GuiButton::mouseEvent @0x1d7438: a press on a button marks it pressed; its release over the
// same button plays the button sound and sends the event. In play the level takes the touch as
// well (run l26_win: a path drawn from (18, 42), inside the power-up button's box, lifted over
// the button at 5650 opens the bar; lifted elsewhere at 2939, it does not); while the bar or
// Digger shows, the level does not run and gets no touches.
bool Session::powerupTouchDown(Vec2 screen) {
    guiPressed_ = powerupItemAt(screen);
    return bar_.showing() || bar_.tutorial() != 0;
}

bool Session::powerupTouchUp(Vec2 screen) {
    const int pressed = guiPressed_;
    guiPressed_ = PowerupBar::kNone;
    const bool overlay = bar_.showing() || bar_.tutorial() != 0;
    if (pressed != PowerupBar::kNone && powerupItemAt(screen) == pressed) {
        sound(sfx::UiButton);
        pressPowerupItem(pressed);
    }
    return overlay;
}

// SceneGame::showPowerupInfoTutorial @0xf793c: the first power-up in the bar's order that is
// there (some left, or unlimited), usable here and not yet introduced gets its card (state 6;
// the card goes in again from nothing) and is marked introduced.
bool Session::showPowerupCard() {
    for (int type : Profile::kBarOrder) {
        PowerupRecord& r = powerups_[std::size_t(type)];
        if (!(r.count == -1 || r.count > 0) || !powerupEnabled(Powerup(type)) || r.introduced) continue;
        bar_.setCardType(type);
        bar_.card() = BounceFrame::hidden();
        bar_.card().goIn();
        bar_.setTutorial(6);
        bar_.restartArrowHighlight();
        r.introduced = true;
        return true;
    }
    return false;
}

void Session::showPowerupBar(bool byTutorial) {
    // SceneGame::showPowerupSlide @0xf7f44: in, the list at its top, the HUD's pills out.
    (void)byTutorial;
    bar_.slide().goIn();
    bar_.scrollToTop();
    hud_.retractAll(0);
}

void Session::hidePowerupBar() {
    // guiEvent: the bar out, the HUD's pills back (and brightened).
    bar_.slide().goOut();
    hud_.protractAll(0);
}

void Session::pressPowerupItem(int item) {
    if (item == PowerupBar::kContinue) {
        // guiEvent 0xeb3fb22: Digger's next line.
        const int t = bar_.tutorial();
        if (t == 4) {
            setPowerupTutorial(5);
            showPowerupCard();
        } else if (t == 2) {
            setPowerupTutorial(metDigger_ ? 4 : 3);
        } else if (t == 1) {
            setPowerupTutorial(2);
        }
        return;
    }
    if (item == PowerupBar::kStore) {
        // guiEvent case 5: out of the tutorial's state 3, then the store (menu_store; not
        // part of the port yet).
        if (bar_.tutorial() == 3) {
            storeHighlighted_ = false;
            setPowerupTutorial(0);
        }
        storeRequested_ = true;
        return;
    }
    if (item == PowerupBar::kPowerupButton) {
        // guiEvent case 1: only while the mouse is in play.
        if (!mouseInPlay_) return;
        if (!bar_.slide().animatingIn() && bar_.slide().progress() <= 0.0f) {
            if (bar_.tutorial() == 2) {
                // Asked for by Digger: the bar opens and he goes on.
                powerupButtonHighlighted_ = false;
                setPowerupTutorial(metDigger_ ? 4 : 3);
                showPowerupBar(true);
                return;
            }
            showPowerupBar(false);
            // The first press runs Digger's tutorial (state 1), later ones show a "New
            // Power-up!" card for one not yet introduced (showPowerupInfoTutorial).
            if (!powerupTutorialDone_) {
                bar_.digger().byButton = true;
                setPowerupTutorial(1);
                powerupTutorialDone_ = true;
            } else {
                showPowerupCard();
            }
        } else {
            hidePowerupBar();
        }
        return;
    }
    if (item == PowerupBar::kArrow) {
        // guiEvent case 4: the next new power-up's card, or the tutorial is over: the card
        // goes, the buttons answer again and the list goes back to its top.
        if (!showPowerupCard()) {
            bar_.card().goOut();
            bar_.setTutorial(0);
            bar_.scrollToTop();
        }
        return;
    }
    const int type = item - PowerupBar::kRowFirst;
    if (type < 0 || type >= kPowerupCount) return;
    const Powerup t = Powerup(type);
    if (!powerupEnabled(t) || !powerupAvailable(t)) return;  // (their messages: not yet)
    PowerupRecord& r = powerups_[std::size_t(type)];
    switch (t) {
    case Powerup::RollerSkate:
        if (r.active) return;
        // Player::startSkateboard @0x1235a8.
        if (!mouse_.skating) {
            mouse_.skating = true;
            mouse_.skateScale = 0.300000012f;
        }
        break;
    case Powerup::Invinciball:
        if (r.active) return;
        // Player::startInvinciball @0x12313c (what it does to twine, catnip and the cats:
        // not reconstructed yet).
        if (!mouse_.inBall) {
            mouse_.inBall = true;
            mouse_.ballScale = 0.300000012f;
        }
        break;
    case Powerup::Kiska:
        return;  // (the level skip: not reconstructed yet)
    default:
        break;
    }
    // Profile::usePowerup @0x1712a0.
    if (r.kind == 1) r.active = true;
    ++r.used;
    if (r.count != -1) --r.count;
    powerupUsed_ = true;  // g_LevelState +0x5c
    hidePowerupBar();
}

}  // namespace doc::game
