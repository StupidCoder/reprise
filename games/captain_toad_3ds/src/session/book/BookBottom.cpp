// See BookBottom.hpp.
#include "session/book/BookBottom.hpp"

#include <algorithm>

namespace book {

namespace {

std::u16string number(int n) {
    std::u16string s;
    for (char c : std::to_string(n)) {
        s += char16_t(c);
    }
    return s;
}

}   // namespace

// ---- Button parts ---------------------------------------------------------------------------------

ButtonPart::ButtonPart(std::string name, captain_toad::ui::Instance* layout, Journal& j)
    : LayoutActor(std::move(name), layout, j) {
    nerve_.set(&ButtonPart::disabled);
}

// 0x189bac: the part appears with Appear (0x41dbfc), then is disabled.
void ButtonPart::appearNow() {
    appearLayout();
    nerve_.set(&ButtonPart::appearing);
}

void ButtonPart::enableNow() {
    if (!nerve_.is(&ButtonPart::waiting)) {
        nerve_.set(&ButtonPart::waiting);
    }
}

void ButtonPart::disable() {
    if (!nerve_.is(&ButtonPart::disabled)) {
        nerve_.set(&ButtonPart::disabled);
    }
}

void ButtonPart::decide() {
    nerve_.set(&ButtonPart::deciding);
}

// 0x41dc6c: Decide and its sound; when it has ended, 0x41ddc8 (one tick) and Wait.
void ButtonPart::deciding(int step) {
    if (step == 0) {
        start("Decide");
        playSound("ButtonBackDecided");
    }
    if (ended()) {
        nerve_.set(&ButtonPart::decidedState);
    }
}

void ButtonPart::decidedState(int) {
    nerve_.set(&ButtonPart::waiting);
}

void ButtonPart::appearing(int step) {
    if (step == 0) {
        start("Appear");
    }
    if (ended()) {
        nerve_.set(&ButtonPart::disabled);
    }
}

void ButtonPart::disabled(int step) {
    if (step == 0) {
        start("Disable");
    }
}

void ButtonPart::waiting(int step) {
    if (step == 0) {
        start("Wait");
    }
}

// ---- Back button (ButtonBack) -------------------------------------------------------------------

BackButton::BackButton(captain_toad::ui::Instance* layout, Journal& j) : LayoutActor("back", layout, j) {
    if (captain_toad::ui::Instance* b = layout_->part("btn_back")) {
        button_ = std::make_unique<ButtonPart>("back.button", b, j);
    }
    nerve_.set(&BackButton::intro);
}

void BackButton::bindIntro() {
    start("Buttons_INTRO", "Back");
}

// 0x1347a8: Buttons_INTRO, "Off"/"On" (OnOff, no such tag here), appear.
void BackButton::appear() {
    if (button_) {
        button_->appearNow();
    }
    start("Buttons_INTRO");
    start("Off", "OnOff");
    appearLayout();
    nerve_.set(&BackButton::intro);
}

void BackButton::leave() {
    nerve_.set(&BackButton::outro);
}

bool BackButton::hit(float x, float y) const {
    captain_toad::ui::Instance* b = layout_ ? layout_->part("btn_back") : nullptr;
    return alive_ && b && b->contains("Hit", x, y);
}

// 0x411d74: waits for its last action ("Back" sound at frame 82 of Buttons_INTRO: not ported).
void BackButton::intro(int) {
    if (alive_ && ended()) {
        nerve_.set(&BackButton::wait);
    }
}

// 0x411d38
void BackButton::wait(int step) {
    if (step == 0) {
        start("Wait");
    }
}

// 0x411cf8
void BackButton::outro(int step) {
    if (step == 0) {
        start("Buttons_OUTRO", "Back");
    }
}

// ---- Page buttons (ButtonBookPageParts) ---------------------------------------------------------

PageButton::PageButton(std::string name, captain_toad::ui::Instance* layout, Journal& j)
    : LayoutActor(std::move(name), layout, j) {
    nerve_.set(&PageButton::disabled);
}

void PageButton::enable() {
    if (nerve_.is(&PageButton::disabled)) {
        nerve_.set(&PageButton::enabling);
    }
}

void PageButton::disable() {
    if (!nerve_.is(&PageButton::disabled)) {
        nerve_.set(&PageButton::disabled);
    }
}

bool PageButton::hit(float x, float y) const {
    return layout_ && layout_->contains("Hit", x, y);
}

// 0x41dd8c
void PageButton::disabled(int step) {
    if (step == 0) {
        start("Disable");
    }
}

// 0x41dd1c
void PageButton::enabling(int step) {
    if (step == 0) {
        start("Enable");
    }
    if (ended()) {
        nerve_.set(&PageButton::waiting);
    }
}

// 0x41db6c
void PageButton::waiting(int step) {
    if (step == 0) {
        start("Wait");
    }
}

// ---- Touch screen (CourseSelect) ----------------------------------------------------------------

Bottom::Bottom(captain_toad::ui::Instance* layout, captain_toad::ui::Instance* back, Journal& j,
               std::function<void(Msg, int)> send, int season, int lives, int gems)
    : LayoutActor("bottom", layout, j), send_(std::move(send)), season_(season), lives_(lives), gems_(gems) {
    back_ = std::make_unique<BackButton>(back, j);
    if (captain_toad::ui::Instance* l = layout_->part("btn_pageL")) {
        left_ = std::make_unique<PageButton>("pageButtonL", l, j);
    }
    if (captain_toad::ui::Instance* r = layout_->part("btn_pageR")) {
        right_ = std::make_unique<PageButton>("pageButtonR", r, j);
    }
    nerve_.set(&Bottom::startState);
}

std::vector<LayoutActor*> Bottom::children() {
    // book-order-a: the left page button (0x358cf1c8) before the right one (0x358cf034).
    std::vector<LayoutActor*> out;
    if (left_) {
        out.push_back(left_.get());
    }
    if (right_) {
        out.push_back(right_.get());
    }
    out.push_back(back_.get());
    if (back_->button()) {
        out.push_back(back_->button());
    }
    return out;
}

// Messages (0x38c11c).
void Bottom::receive(Msg m, int) {
    switch (m) {
    case Msg::Appear:
        appear();
        nerve_.set(&Bottom::startState);
        break;
    case Msg::Chosen:
    case Msg::Leave:   // 0x38c11c: both leave by 0x42d954
        nerve_.set(&Bottom::leaveState);
        break;
    case Msg::Opened:
        if (alive_) {
            nerve_.set(&Bottom::curtainState);
        }
        break;
    case Msg::BottomIntro:
        if (alive_) {
            start("In", "OnOff");
            back_->bindIntro();
        }
        break;
    default:
        break;
    }
}

void Bottom::setPixelToad(bool on) {
    if (pixelToad_ != on) {
        pixelToad_ = on;
        start(on ? "Button_PixelToad_INTRO" : "Button_PixelToad_OUTRO");
    }
}

// 0x38c6cc
void Bottom::appear() {
    showPane("PTFocus", false);
    if (captain_toad::ui::Instance* counters = layout_->part("counters")) {
        counters->setText("TxtItem01", number(gems_));
        counters->setText("TxtPlayer01", number(lives_));
    }
    start("counters.LifeCounter_EPISODE" + std::to_string(std::min(season_, 3)));
    start("Season" + std::to_string(std::min(season_, 3)), "Color");
    hold("PageButtons_INTRO");
    hold("Appear");
    start("Off", "OnOff");
    showPane("pixeltoad", false);
    showPane("amiibo", false);
    appearLayout();
    if (left_) {
        left_->appear();
    }
    if (right_) {
        right_->appear();
    }
}

// 0x42d904: once the last action has ended ("ChapterImage_Appear" stopped), Wait.
void Bottom::startState(int) {
    if (alive_ && ended()) {
        stop("ChapterImage_Appear");
        nerve_.set(&Bottom::waitState);
    }
}

// 0x42d850
void Bottom::waitState(int step) {
    if (step == 0) {
        start("Wait");
    }
}

// 0x42da70
void Bottom::curtainState(int step) {
    if (step == 0) {
        start("Curtain_INTRO", "Curtain");
    }
    if (ended()) {
        nerve_.set(&Bottom::buttonsState);
    }
}

// 0x38bdb0: the amiibo and pixel-toad buttons are not available (their panes hidden); the back
// button appears; PageButtons_INTRO; when that has ended, BottomReady and Wait.
void Bottom::buttonsState(int step) {
    if (step == 0) {
        showPane("amiibo", false);
        showPane("pixeltoad", false);
        back_->appear();
        start("PageButtons_INTRO");
    }
    if (ended()) {
        send_(Msg::BottomReady, 0);
        nerve_.set(&Bottom::waitState);
    }
}

// 0x42d954: End, Buttons_OUTRO, PageButtons_OUTRO; then the curtain closes.
void Bottom::leaveState(int step) {
    if (step == 0) {
        back_->leave();
        start("End");
        start("Buttons_OUTRO");
        start("PageButtons_OUTRO");
    }
    if (ended()) {
        nerve_.set(&Bottom::curtainOutState);
    }
}

// 0x42d7f8
void Bottom::curtainOutState(int step) {
    if (step == 0) {
        start("Curtain_OUTRO");
        start("End");
    }
}

}   // namespace book
