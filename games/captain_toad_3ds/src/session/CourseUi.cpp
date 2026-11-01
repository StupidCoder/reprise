// See CourseUi.hpp.
#include "session/CourseUi.hpp"

#include "game/Camera.hpp"
#include "importer/UiExport.hpp"

#include "gpu/Device.hpp"

#include <algorithm>
#include <cmath>

namespace captain_toad::session {

namespace {

std::u16string number(int n) {
    std::u16string s;
    for (char c : std::to_string(n)) {
        s += char16_t(c);
    }
    return s;
}

const char* const kButtons[2] = {"ParButton0", "ParButton1"};

}   // namespace

CourseUi::CourseUi(const std::filesystem::path& dataDirectory, const std::filesystem::path& cacheDirectory,
                   const captain_toad::CourseEntry& course, int courseNumber)
    : season_("Season" + std::to_string(std::min(course.season, 3))) {
    ui_ = std::make_unique<captain_toad::ui::Package>(captain_toad::buildUi(dataDirectory, cacheDirectory, "Gameplay"));
    wipes_ = std::make_unique<captain_toad::ui::Package>(captain_toad::buildUi(dataDirectory, cacheDirectory, "Wipes"));
    // The texts set up front: the course's number and name on the pause screen.
    stageNumber_ = number(courseNumber);
    stageName_ = ui_->message("StageName", course.stage);
    bossStage_ = course.stage == "BossBird1Stage" || course.stage == "BossBird2Stage";
}

// ---- title card ------------------------------------------------------------------------------

void CourseUi::takeTitleCard(std::unique_ptr<captain_toad::ui::Package> package,
                             std::unique_ptr<captain_toad::ui::Instance> card) {
    cardPackage_ = std::move(package);
    card_ = std::move(card);
    cardEnd_ = 0;
}

void CourseUi::startTick(const Session::StartTick& t) {
    if (!card_) {
        return;
    }
    // End1 with the stage scene's first update (intro tick 1; the loader's 0x4d in the same game
    // frame), End2 from the tick the demo turns (card-msg-a: calls 1144 and 1345).
    if (cardEnd_ == 0 && t.introTick >= 1) {
        card_->startAction(bossStage_ ? "End4" : "End1");   // 0x3dfa4c (WipePos stays at the centre)
        cardEnd_ = 1;
    }
    if (cardEnd_ == 1 && t.turnStarted) {
        card_->startAction(bossStage_ ? "End5" : "End2");   // 0x3dfb84
        cardEnd_ = 2;
    }
    card_->calc();
    if (cardEnd_ == 2 && card_->ended(bossStage_ ? "End5" : "End2")) {
        card_.reset();   // 0x4119b8
        cardPackage_.reset();
    }
}

CourseUi::~CourseUi() = default;

// ---- pause menu -----------------------------------------------------------------------------

void CourseUi::openPause() {
    pauseTop_ = std::make_unique<captain_toad::ui::Instance>(*ui_, "PauseMenu_TOP");
    pauseBottom_ = std::make_unique<captain_toad::ui::Instance>(*ui_, "PauseMenu_BOTTOM");
    pauseTop_->startAction(season_);
    pauseTop_->startAction("Appear");
    pauseTop_->setText("TxtStage", stageNumber_);
    pauseTop_->setText("TxtCourseName_NA", stageName_);
    pauseBottom_->startAction("Appear");
    const std::u16string labels[2] = {ui_->message("LayoutMessage/PauseMenu", "PauseMenu_Button0"),
                                      ui_->message("LayoutMessage/PauseMenu", "PauseMenu_Button1")};
    for (int i = 0; i < 2; i++) {
        if (captain_toad::ui::Instance* b = pauseBottom_->part(kButtons[i])) {
            b->startAction(season_);
            b->setText("txt_button", labels[i]);
        }
    }
    if (captain_toad::ui::Instance* camera = pauseBottom_->part("ParButton4")) {
        camera->startAction("Normal");
    }
    cursor_ = 0;
    select(0);
    pause_ = Pause::Appear;
    t_ = 0;
    clock_ = 0;
    prevButtons_ = ~0u;   // the opening press does not count
    pending_ = Result::None;
}

void CourseUi::select(int button) {
    cursor_ = button;
    for (int i = 0; i < 2; i++) {
        if (captain_toad::ui::Instance* b = pauseBottom_->part(kButtons[i])) {
            b->startAction(i == button ? "Select" : "Wait");
        }
    }
}

void CourseUi::touch(bool down, float x, float y) {
    touchDown_ = down;
    touchPos_[0] = x;
    touchPos_[1] = y;
}

CourseUi::Result CourseUi::pauseAdvance(double seconds, const Controls& controls) {
    clock_ += seconds;
    int n = 0;
    while (clock_ >= 1.0 / 30 && n < 4 && pauseTop_) {
        clock_ -= 1.0 / 30;
        pauseTick(controls);
        ++n;
        if (pending_ != Result::None) {
            const Result r = pending_;
            pending_ = Result::None;
            if (r == Result::Resume || r == Result::Exit) {
                pauseTop_.reset();
                pauseBottom_.reset();
                dialog_.reset();
            }
            return r;
        }
    }
    if (clock_ > 1.0 / 30) {
        clock_ = 0;
    }
    return Result::None;
}

void CourseUi::pauseTick(const Controls& c) {
    using namespace captain_toad::RecoveredCamera;
    const uint32_t pressed = c.buttons & ~prevButtons_;
    prevButtons_ = c.buttons;
    int vertical = 0, horizontal = 0;
    if ((pressed & kDUp) || (c.stick[1] > 0.5f && prevStick_ <= 0.5f)) {
        vertical = -1;
    } else if ((pressed & kDDown) || (c.stick[1] < -0.5f && prevStick_ >= -0.5f)) {
        vertical = 1;
    }
    if (pressed & (kDLeft)) {
        horizontal = -1;
    } else if (pressed & (kDRight)) {
        horizontal = 1;
    }
    prevStick_ = c.stick[1];
    const bool tapped = touchDown_ && !prevTouch_;
    prevTouch_ = touchDown_;
    const float lx = touchPos_[0] - 160, ly = 120 - touchPos_[1];
    auto touched = [&](captain_toad::ui::Instance* in, const char* part) {
        captain_toad::ui::Instance* p = in ? in->part(part) : nullptr;
        return tapped && p && p->contains("bdy_button", lx, ly);
    };
    ++t_;
    switch (pause_) {
    case Pause::Appear:
        if (t_ >= 6) {   // Appear: 11 frames
            pauseBottom_->startAction("Wait");
            pause_ = Pause::Menu;
        }
        break;
    case Pause::Menu: {
        int decide = -1;
        if (vertical) {
            select(std::clamp(cursor_ + vertical, 0, 1));
        }
        for (int i = 0; i < 2; i++) {
            if (touched(pauseBottom_.get(), kButtons[i])) {
                select(i);
                decide = i;
            }
        }
        if (pressed & kA) {
            decide = cursor_;
        }
        if (pressed & kB) {
            decide = 0;
        }
        if (decide == 0) {
            pauseTop_->startAction("End");
            pauseBottom_->startAction("End");
            if (captain_toad::ui::Instance* b = pauseBottom_->part(kButtons[0])) {
                b->startAction("Decide");
            }
            pause_ = Pause::Closing;
            t_ = 0;
        } else if (decide == 1) {
            if (captain_toad::ui::Instance* b = pauseBottom_->part(kButtons[1])) {
                b->startAction("Decide");
            }
            // The leave confirmation: question, "Don't leave" (left) and "Leave" (right).
            dialog_ = std::make_unique<captain_toad::ui::Instance>(*ui_, "WindowDialogs_BOTTOM");
            dialog_->startAction("WindowConfirmDouble_INTRO");
            dialog_->startAction("LeftRight");
            dialog_->setText("wdw2_text_NA", ui_->message("WindowConfirmPauseMenu", "ReturnToMap"));
            if (captain_toad::ui::Instance* l = dialog_->part("ParButtonL")) {
                l->startAction(season_);
                l->setText("txt_button", ui_->message("WindowConfirmPauseMenu", "ReturnToMap_Left"));
            }
            if (captain_toad::ui::Instance* r = dialog_->part("ParButtonR")) {
                r->startAction(season_);
                r->setText("txt_button", ui_->message("WindowConfirmPauseMenu", "ReturnToMap_Right"));
            }
            confirm_ = 0;
            pause_ = Pause::Confirm;
            t_ = 0;
        }
        break;
    }
    case Pause::Confirm: {
        if (horizontal) {
            confirm_ = horizontal > 0 ? 1 : 0;
        }
        int decide = -1;
        if (touched(dialog_.get(), "ParButtonL")) {
            decide = 0;
        } else if (touched(dialog_.get(), "ParButtonR")) {
            decide = 1;
        } else if (pressed & kA) {
            decide = confirm_;
        } else if (pressed & kB) {
            decide = 0;
        }
        for (const char* side : {"ParButtonL", "ParButtonR"}) {
            if (captain_toad::ui::Instance* b = dialog_->part(side)) {
                b->startAction((side[9] == 'R') == (confirm_ == 1) ? "Select" : "Wait");
            }
        }
        if (decide == 0) {
            dialog_->startAction("WindowConfirmDouble_OUTRO");
            pause_ = Pause::Menu;
            select(cursor_);
            t_ = 0;
        } else if (decide == 1) {
            dialog_->startAction("WindowConfirmDouble_OUTRO");
            pauseTop_->startAction("End");
            pause_ = Pause::Leaving;
            t_ = 0;
        }
        break;
    }
    case Pause::Closing:
        if (t_ >= 11) {   // End: 21 frames
            pending_ = Result::Resume;
        }
        break;
    case Pause::Leaving:
        if (t_ >= 16) {
            pending_ = Result::Exit;
        }
        break;
    }
    for (captain_toad::ui::Instance* i : {pauseTop_.get(), pauseBottom_.get(), dialog_.get()}) {
        if (i) {
            i->calc();
        }
    }
}

// ---- course clear ---------------------------------------------------------------------------

void CourseUi::startClear() {
    clearTick_ = 0;
    clearClock_ = 0;
}

CourseUi::Result CourseUi::clearAdvance(double seconds) {
    clearClock_ += seconds;
    while (clearClock_ >= 1.0 / 30 && clearTick_ >= 0) {
        clearClock_ -= 1.0 / 30;
        ++clearTick_;
        if (clearTick_ == 96) {
            clearBanner_ = std::make_unique<captain_toad::ui::Instance>(*ui_, "HeadCourseClear_TOP");
            clearBanner_->startAction(season_);
            clearBanner_->startAction("Appear");
        }
        if (clearTick_ == 162) {
            // The exit wipe after the goal demo's end (step 160): WipeClear_TOP Season1 + Appear at
            // its natural speed (traced: bound at GoalPose step 162, Wait 39 ticks later; its End
            // opens on the book).
            clearWipe_ = std::make_unique<captain_toad::ui::Instance>(*wipes_, "WipeClear_TOP");
            clearWipe_->startAction(season_);
            clearWipe_->startAction("Appear");
        }
        if (clearBanner_) {
            clearBanner_->calc();
        }
        if (clearWipe_) {
            clearWipe_->calc();
        }
        if (clearWipe_ && clearTick_ > 162 && clearWipe_->ended("Appear")) {
            clearTick_ = -1;
            return Result::Cleared;
        }
    }
    return Result::None;
}

// ---- miss ---------------------------------------------------------------------------------------

void CourseUi::startMiss() {
    missTick_ = 0;
    missClock_ = 0;
    missBanner_ = std::make_unique<captain_toad::ui::Instance>(*ui_, "HeadMiss_TOP");
    missBanner_->startAction("Appear");
}

CourseUi::Result CourseUi::missAdvance(double seconds) {
    missClock_ += seconds;
    while (missClock_ >= 1.0 / 30 && missTick_ >= 0) {
        missClock_ -= 1.0 / 30;
        ++missTick_;
        if (missTick_ == 45) {
            missWipe_ = std::make_unique<captain_toad::ui::Instance>(*ui_, "WipeMiss_TOP");
            missWipe_->startAction("Appear");
        }
        for (captain_toad::ui::Instance* i : {missBanner_.get(), missWipe_.get()}) {
            if (i) {
                i->calc();
            }
        }
        if (missTick_ >= 80) {
            missTick_ = -1;
            missBanner_.reset();
            missWipe_.reset();
            return Result::Restart;
        }
    }
    return Result::None;
}

// ---- drawing --------------------------------------------------------------------------------

// Between two 30 Hz steps (each applies the frames, then moves them on by 2) a layout shows the
// frames `fraction` of a step past the last applied ones.
namespace {
void drawBetween(captain_toad::ui::Instance* i, bool smooth, double fraction) {
    if (!i) {
        return;
    }
    if (smooth) {
        i->applyAt(float(2.0 * std::clamp(fraction, 0.0, 1.0) - 2.0));
    }
    i->drawScreen(0);
}
}   // namespace

// The layouts keep the original 5:3 frame of the game view (wider or narrower with the wide view):
// banners, cards and menus inside it, the wipes covering it (Session::originalFrame).
namespace {
captain_toad::gpu::Rect frame(const captain_toad::gpu::Rect& v, bool cover) {
    const auto f = Session::originalFrame({v.x, v.y, v.w, v.h}, cover);
    return {f[0], f[1], f[2], f[3]};
}
}   // namespace

void CourseUi::drawTop() {
    captain_toad::gpu::Device& device = captain_toad::gpu::device();
    const captain_toad::gpu::Rect view = device.viewport();
    auto at = [&](captain_toad::ui::Instance* i, double fraction, bool cover) {
        if (!i) {
            return;
        }
        device.setViewport(frame(view, cover));
        drawBetween(i, smooth_, fraction);
    };
    at(card_.get(), cardAlpha_, false);
    at(clearBanner_.get(), clearClock_ * 30, false);
    at(clearWipe_.get(), clearClock_ * 30, true);
    at(missBanner_.get(), missClock_ * 30, false);
    at(missWipe_.get(), missClock_ * 30, true);
    at(pauseTop_.get(), clock_ * 30, false);
    device.setViewport(view);
}

void CourseUi::drawBottom() {
    for (captain_toad::ui::Instance* i : {pauseBottom_.get(), dialog_.get()}) {
        drawBetween(i, smooth_, clock_ * 30);
    }
}

}   // namespace captain_toad::session
