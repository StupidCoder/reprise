#include "game/dossier.hpp"

namespace doc::game {

namespace {

// Each step eases an eighth of the way to its target (x * 7 + target) / 8; the paper a
// sixteenth (x * 15 + target) / 16 (@0x286f96).
Vec2 ease(Vec2 at, Vec2 target) { return (at * 7.0f + target) * 0.125f; }
float ease(float at, float target) { return (at * 7.0f + target) * 0.125f; }
Vec2 easePaper(Vec2 at, Vec2 target) { return (at * 15.0f + target) * 0.0625f; }

}  // namespace

void DossierScreen::open(int dossier, bool fromMenu, int dossiersFound) {
    *this = DossierScreen{};
    active_ = true;
    dossier_ = dossier;
    fromMenu_ = fromMenu;
    found_ = dossiersFound;
    // showScene: everything off its place (the folder far left, the screenshot below, the
    // portrait above), the pictures turned, then the targets for coming in.
    folder_ = {-450.0f, 5.0f};
    paper_ = {20.0f, 22.0f};
    portrait_ = {42.0f, 700.0f};
    screenshot_ = {40.0f, -200.0f};
    portraitTurn_ = 270.0f;
    screenshotTurn_ = 150.0f;
    steps_ = 1;
    dim_ = 0.0f;
    scrollIn();
    sounds_.push_back(0xae);
}

void DossierScreen::scrollIn() {
    folderTarget_ = {22.0f, 5.0f};
    paperTarget_ = {220.0f, 22.0f};
    screenshotTarget_ = {50.0f, 25.0f};
    portraitTarget_ = {45.0f, 150.0f};
    portraitTurnTarget_ = 0.0f;
    screenshotTurnTarget_ = 0.0f;
    comingIn_ = true;
}

void DossierScreen::scrollOut() {
    folderTarget_ = {-400.0f, 30.0f};
    paperTarget_ = {20.0f, 22.0f};
    portraitTarget_ = {42.0f, 800.0f};
    screenshotTarget_ = {40.0f, -400.0f};
    portraitTurnTarget_ = 270.0f;
    screenshotTurnTarget_ = 150.0f;
    comingIn_ = false;
}

void DossierScreen::back() {
    if (!active_ || note_.active) return;
    exiting_ = true;
    scrollOut();
    steps_ = 3;
}

bool DossierScreen::update(int dtMs) {
    if (!active_) return false;
    // (The note on top: only it runs.)
    if (note_.active) return true;
    if (dim_ < 0.5f) dim_ = dim_ + float(dtMs) * 0.00200000009f;
    if (exiting_ && (steps_ & 8)) {
        active_ = false;
        return false;
    }
    if (steps_ & 1) {
        folder_ = ease(folder_, folderTarget_);
        if ((folderTarget_ - folder_).lengthSquared() < 25.0f) steps_ = (steps_ & ~3u) | 2;
    }
    if (steps_ & 2) {
        paper_ = easePaper(paper_, paperTarget_);
        const float d = (paperTarget_ - paper_).lengthSquared();
        if (d < 21025.0f) {
            // The paper close to its place: the pictures start, with a swish.
            if ((steps_ & 0xc) == 0) sounds_.push_back(comingIn_ ? 0xaf : 0xb0);
            steps_ |= 4;
        }
        if (d < 25.0f) steps_ &= ~2u;
    }
    if (steps_ & 4) {
        portraitTurn_ = ease(portraitTurn_, portraitTurnTarget_);
        screenshotTurn_ = ease(screenshotTurn_, screenshotTurnTarget_);
        portrait_ = ease(portrait_, portraitTarget_);
        screenshot_ = ease(screenshot_, screenshotTarget_);
        if ((portraitTarget_ - portrait_).lengthSquared() < 25.0f &&
            (screenshotTarget_ - screenshot_).lengthSquared() < 25.0f) {
            const unsigned was = steps_;
            steps_ = (steps_ & ~0xcu) | 8;
            // Settled with the paper in place: the first dossier ever found (not opened from the
            // menu) explains itself ("You've found a dossier!", string 0x591, text 0x59a, "Okay").
            // (The pictures' step is armed again every tick while the paper is near, so this
            // waits for the paper; going out the page ends before the paper settles.)
            if (!(was & 2) && found_ == 1 && !fromMenu_) {
                note_.open(0, 0x591, 0x59a);
                note_.okText = 0x13d;
            }
        }
    }
    return true;
}

}  // namespace doc::game
