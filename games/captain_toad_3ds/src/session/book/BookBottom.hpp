// =============================================================================================
// BookBottom.hpp - the touch screen under the book: curtain, counters, page and back buttons
// =============================================================================================
//
// The touch screen (CourseSelect actor, constructor 0x38ca50, message handler 0x38c11c; layout
// CourseBook_BOTTOM) has its own state machine:
//   Appear     0x38c6cc (message Appear): the counters, the season's colour, PageButtons_INTRO
//              held at its first frame, "Off"/"On"; the pixel-toad panes hidden
//   Start      0x42d904: waits for its last action, then Wait
//   Wait       0x42d850
//   Curtain    0x42da70 (message Opened): Curtain_INTRO
//   Buttons    0x38bdb0: the back button appears, PageButtons_INTRO; then BottomReady
//   Leave      0x42d954 (message Chosen): End, Buttons_OUTRO, PageButtons_OUTRO; then Curtain_OUTRO
// The amiibo and pixel-toad buttons only come in when those features are available; they are not
// (not ported), so their panes stay hidden as in the traced runs.
// The back button is the separate screen ButtonBack (0x1349d8); the page buttons are the
// ButtonBookPageParts parts btn_pageL / btn_pageR (states 0x41dd8c ...), enabled while the book
// can turn that way (the input manager, 0x1eab58).
// =============================================================================================
#pragma once
#include "session/book/BookActor.hpp"

#include <functional>
#include <memory>

namespace book {

// A button part (ButtonBackParts / ButtonBookPageParts: Appear 0x41dbfc, Disable 0x41dd8c, Enable
// 0x41dd1c, Wait 0x41db6c).
class ButtonPart : public LayoutActor {
public:
    ButtonPart(std::string name, captain_toad::ui::Instance* layout, Journal& j);
    void update() override { nerve_.update(*this); }
    void appearNow();   // Appear, then Disable
    void enableNow();   // 0x38bc88: Wait at once (from Disable)
    void disable();     // 0x2a3fc0
    void decide();      // 0x41dc6c: Decide ("ButtonBackDecided"), then Wait
    bool enabled() const { return nerve_.is(&ButtonPart::waiting); }
    bool decided() const { return nerve_.is(&ButtonPart::decidedState); }   // 0x417750

private:
    void appearing(int step);
    void disabled(int step);
    void waiting(int step);
    void deciding(int step);
    void decidedState(int step);
    captain_toad::RecoveredNerve::Keeper<ButtonPart> nerve_;
};

class BackButton : public LayoutActor {
public:
    BackButton(captain_toad::ui::Instance* layout, Journal& j);
    ButtonPart* button() { return button_.get(); }
    void update() override { nerve_.update(*this); }
    void bindIntro();   // 0x13476c: Buttons_INTRO (slot Back) while not shown yet
    void appear();      // 0x1347a8
    void leave();       // 0x411cf8
    // True when the layout point (touch-screen pixels centred, y up) hits the button.
    bool hit(float x, float y) const;

private:
    void intro(int step);
    void wait(int step);
    void outro(int step);
    std::unique_ptr<ButtonPart> button_;
    captain_toad::RecoveredNerve::Keeper<BackButton> nerve_;
};

class PageButton : public LayoutActor {
public:
    PageButton(std::string name, captain_toad::ui::Instance* layout, Journal& j);
    void update() override { nerve_.update(*this); }
    void appear() { appearLayout(); }
    void enable();    // 0x41dd1c (from Disable only)
    void disable();   // 0x41dd8c
    bool enabled() const { return !nerve_.is(&PageButton::disabled); }
    bool hit(float x, float y) const;

private:
    void disabled(int step);
    void enabling(int step);
    void waiting(int step);
    captain_toad::RecoveredNerve::Keeper<PageButton> nerve_;
};

class Bottom : public LayoutActor {
public:
    // `send`: messages to the owner (the scene).
    Bottom(captain_toad::ui::Instance* layout, captain_toad::ui::Instance* back, Journal& j,
           std::function<void(Msg, int)> send, int season, int lives, int gems);
    void update() override { nerve_.update(*this); }
    void receive(Msg m, int arg);
    // Actors after this one in the update order.
    std::vector<LayoutActor*> children();
    BackButton& back() { return *back_; }
    // 0x2a3878: the pixel-toad button in or out (Button_PixelToad_INTRO / _OUTRO when it changes).
    void setPixelToad(bool on);
    PageButton* pageButton(bool right) { return right ? right_.get() : left_.get(); }

private:
    void appear();
    void startState(int step);
    void waitState(int step);
    void curtainState(int step);
    void buttonsState(int step);
    void leaveState(int step);
    void curtainOutState(int step);

    std::function<void(Msg, int)> send_;
    std::unique_ptr<BackButton> back_;
    std::unique_ptr<PageButton> left_, right_;
    captain_toad::RecoveredNerve::Keeper<Bottom> nerve_;
    int season_, lives_, gems_;
    bool pixelToad_ = true;   // +0x84 +0x38
};

}   // namespace book
