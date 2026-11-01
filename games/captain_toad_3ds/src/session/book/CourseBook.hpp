// =============================================================================================
// CourseBook.hpp - the book on the top screen: its spreads, turns, arrows and spotlight
// =============================================================================================
//
// The book (CourseBook actor, constructor 0x137958, message handler 0x135e00) holds the season's
// spreads and the index of the open one. Its states:
//   Appear     0x412400  AppearBook; once it has ended it reports Appeared every tick
//   Open       0x137050  OpenBook; the pages take their places (24 steps) and their first states
//   Wait       0x412300  WaitBook; page input is on
//   Forward    0x4125b0  the open spread's right page leaves; one spread per tick to the target
//   Back       0x412514  the open spread's left page leaves
//   TurnEnd    0x4127c4 / 0x4126e8  waits until every page has settled
//   CourseOpen 0x135cd0  a newly opened course's pages show it, then CourseOpenDone
//   Chosen     0x412470  the chosen course's press; ChosenDone at step 50
//   Close      0x412398  (message Close) the arrows go, the open spread lets go of its input
//   CloseBook  0x1375e0  CloseBook; every right page from the open spread on turns away and the
//                        spreads move back to the closed side (24 steps)
//   End        0x412238  Curtain_OUTRO, EndBook, every page ends; then Closed every tick
// The arrows (CourseBookArrows, 0x19ecd8) and the course-open spotlight (FilterCourseOpen,
// 0x2f7e30) are its child actors; the cover (CourseBookCover, 0x18b4d4) has no layout in this
// release and only its state machine matters (OpenBook waits for it).
// =============================================================================================
#pragma once
#include "session/book/BookPages.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace book {

// The page arrows above the book (CourseBookArrows; states 0x4220f4 / 0x4220b8 / 0x421ffc).
class Arrows : public LayoutActor {
public:
    Arrows(captain_toad::ui::Instance* layout, Journal& j);
    void update() override { nerve_.update(*this); }
    void appear() { appearLayout(); }
    void show();                             // 0x2f95d0
    void hide();                             // 0x2f9478
    void showSides(bool left, bool right);   // MasterL / MasterR (0x2f95a4 / 0x2f9578)
    void setRootVisible(bool v) { showPane("root", v); }
    void outro();   // 0x2f8f88: root off, PageArrow_OUTRO

private:
    void showing(int step);
    void looping(int step);
    void hiding(int step);
    captain_toad::RecoveredNerve::Keeper<Arrows> nerve_;
};

// An appear / wait / end filter layout (0x2f7e30: FilterCourseOpen, WipeFilter).
class Filter : public LayoutActor {
public:
    Filter(std::string name, captain_toad::ui::Instance* layout, Journal& j);
    void update() override { nerve_.update(*this); }
    void appearNow();                                                // 0x2f8a5c with the animation's own pace
    void end();                                                      // 0x2f8b64
    bool waiting() const { return nerve_.is(&Filter::waitState); }   // 0x2f8bd8

private:
    void appearing(int step);
    void waitState(int step);
    void ending(int step);
    void gone(int step);
    captain_toad::RecoveredNerve::Keeper<Filter> nerve_;
};

class Book : public LayoutActor {
public:
    struct Spread {
        PageInfo info;
        std::unique_ptr<Page> right, left;
    };
    // `send`: messages to the owner (the scene).
    Book(captain_toad::ui::Instance* layout, captain_toad::ui::Package& pkg, Journal& j, std::vector<PageInfo> spreads,
         int lastPlayedSpread, std::function<void(Msg, int)> send, captain_toad::ui::Instance* filterLayout);

    void update() override {
        nerve_.update(*this);
        placeArrows();
    }
    void receive(Msg m, int arg);
    // Actors in the game's update order after the book: the arrows, then each spread's right page
    // and its parts, then its left page and its parts; the spotlight while it lives.
    std::vector<LayoutActor*> children();

    // Page input (0x2f846c / 0x2f8620): turn one spread forward / back; false when not possible.
    bool turnNext();
    bool turnPrevious();
    bool inputOn() const { return input_; }
    bool waiting() const { return nerve_.is(&Book::waitState); }   // 0x2a39e0
    int current() const { return current_; }
    int spreadCount() const { return int(spreads_.size()); }
    const Spread& spread(int i) const { return spreads_[size_t(i)]; }
    bool canChoose() const;
    void placePagesOnLayout();
    void placeArrows();
    Filter* spotlight() { return filter_.get(); }

private:
    void appear();     // vtable +8, 0x136b08
    void showOpen();   // 0x136848
    void setInput(bool on);
    void focusCurrent();   // 0x2f9004
    bool allSettled() const;
    // States.
    void appearState(int step);
    void openState(int step);
    void waitState(int step);
    void forwardState(int step);
    void backState(int step);
    void turnEndState(int step);
    void courseOpenState(int step);
    void chosenState(int step);
    void closeRequestState(int step);
    void closeBookState(int step);
    void endState(int step);

    captain_toad::ui::Package& pkg_;
    std::vector<Spread> spreads_;
    std::unique_ptr<Arrows> arrows_;
    std::unique_ptr<Filter> filter_;
    std::function<void(Msg, int)> send_;
    captain_toad::RecoveredNerve::Keeper<Book> nerve_;
    int lastPlayed_ = 0;
    int current_ = 0;      // +0x74
    int target_ = 0;       // +0x78
    int openCourse_ = 0;   // +0x80: the course id the course-open shows
    bool input_ = false;   // +0x68
    // The cover (no layout here): Appear 0x41e2f4, opening 0x41e2a0 (one tick), Wait 0x41e2b4.
    enum class Cover { Appear, Opening, Wait } cover_ = Cover::Appear;
    // The "NextPage" sound's pitch (+0xc8 / +0xcc): 0.85 .. 1.15 in steps of 0.03.
    float pitch_ = 1.0f, pitchStep_ = 0.03f;
    bool pitchReset_ = false;   // +0xc4
};

}   // namespace book
