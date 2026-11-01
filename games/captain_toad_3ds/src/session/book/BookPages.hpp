// =============================================================================================
// BookPages.hpp - one page of the course book and its parts
// =============================================================================================
//
// Every spread of the book is a right page and a left page (CoursePageR / CoursePageL, the part
// templates page_holder/Parts_01 and Parts_00 of CourseBook_TOP). The game builds a page class
// per stage type (0x1b5268): courses (type 通常: 0x1d261c right, 0x1c44f4 left), illustrations
// and illustrated cutscenes (挿絵 / 挿絵デモ: the "Separate" classes 0x2177c0 / 0x210e6c), bonus
// courses (特殊(ボーナス): 0x20168c / 0x1fabe0) and the other special courses (0x211dd8 /
// 0x20d078). They share one interface (vtable +0x68 ...) and differ in what they play.
//
// Pages link to their neighbours (0x136d3c): a right page knows the next spread's left page
// (`toward`) and right page (`beyond`); a left page the previous spread's right page (`toward`)
// and left page (`beyond`). A turn hands over through these links: a page that has lifted away
// tells the neighbouring page on the other side to come down; a page that has come down sends the
// page it covers to the stack. The first spread's left page and the last spread's right page have
// no links and show the book's cover.
//
// States (course right page; the other classes alike, addresses in BookPages.cpp):
//   Hidden    root pane off (all pages start here; 0x42955c)
//   Current   Page_IDLE, becoming the page at rest under a turning one (0x4297c0)
//   Rest      Page_IDLE, root shown (0x429598)
//   Stack     Page_DUMMY: only the page edge in the stack (0x4296b0)
//   Leave     Page_NEXT on the right (0° → −90°), Page_PREVIOUS on the left (0° → 90°)
//   Arrive    Page_PREVIOUS on the right (−90° → 0°), Page_NEXT on the left (90° → 0°)
//   NewIntro  the course-open: Page_NEW_INTRO in the PageNew group, then back to the state before
// Tags that only the original layouts have ("Wait", "HideWait" on the illustration pages) play
// nothing on these layouts: such a page keeps its last pose.
//
// A course page's contents follow the course's save record (0x2bc870: bit 0 opened, bit 1
// cleared, bit 2 waits for gems, bit 3 newly opened, bit 8 pixel-toad medal, bit 16 complete;
// refreshed by 0x2aaae4 on the right page and 0x2af500 on the left): an unopened course shows
// none of its parts; an opened one its name, goal star, gems, records and the thumbnail button;
// a newly opened one the NEW overlay and the NEW stamp on its thumbnail.
// =============================================================================================
#pragma once
#include "importer/Stages.hpp"
#include "session/book/BookActor.hpp"

#include <memory>
#include <string>
#include <vector>

namespace book {

// The stage types that make pages (0x1b5268 / the spread type at +0x1c: 1 course, 2 illustration,
// 3 special, 4 bonus).
enum class PageKind { Course, Illustration, Special, Bonus };

// What the book needs to know about a spread's course: its StageList entry and save record.
struct PageInfo {
    const captain_toad::CourseEntry* entry = nullptr;
    PageKind kind = PageKind::Course;
    int courseNumber = 0;     // the number on the page ("1" for the season's first course)
    bool opened = false;      // bit 0: its page has been opened in the book
    bool cleared = false;     // bit 1
    bool gemLocked = false;   // bit 2: waits for gems
    bool fresh = false;       // bit 3: opened and not played yet
    bool medal = false;       // bit 8: the pixel-toad medal (not ported: never)
    bool complete = false;    // bit 16: completed (not ported: never)
    bool cutscene = false;    // 挿絵デモ: an illustrated cutscene (its pages loop the NEW frame)
    int gems = 0;             // collected gems, bits 0-2
};

// A part of a page with a shown and a hidden state (the info parts: 0x41e5ec / 0x41e6ac level
// name, 0x429b48 / 0x429b74 goal star, 0x429ccc / 0x429d8c gems, 0x432f64 / 0x433024 challenge,
// 0x429ee4 / 0x429f94 records, 0x432920 / 0x432970 pixel-toad medal, 0x431c68 / 0x431d14 the
// course-complete stamp). Shown and hidden set the part's root pane and play a tag.
class PagePart : public LayoutActor {
public:
    PagePart(std::string name, captain_toad::ui::Instance* layout, Journal& j, const char* hideTag, bool hideRoot,
             const char* showTag, bool showRoot);
    void update() override { nerve_.update(*this); }
    void appear() { appearLayout(); }
    void show() { nerve_.set(&PagePart::shown); }
    void hide() { nerve_.set(&PagePart::hidden); }
    void setRoot(bool v) { showPane("root", v); }

private:
    void hidden(int step);
    void shown(int step);
    const char* hideTag_;
    bool hideRoot_;
    const char* showTag_;
    bool showRoot_;
    captain_toad::RecoveredNerve::Keeper<PagePart> nerve_;
};

// The A-button icon on a course page (IconAParts; states 0x41301c ...).
class IconA : public LayoutActor {
public:
    IconA(std::string name, captain_toad::ui::Instance* layout, Journal& j);
    void update() override { nerve_.update(*this); }
    void appear() { appearLayout(); }
    void show();       // 0x2aa244: OFF, then INTRO → IDLE
    void press();      // 0x413104
    void hideIcon();   // 0x2aaa70 -> 0x412f84: OUTRO, then OFF
    bool shown() const { return nerve_.is(&IconA::intro) || nerve_.is(&IconA::idle); }

private:
    void off(int step);
    void intro(int step);
    void idle(int step);
    void pressed(int step);
    void done(int step);
    void outro(int step);
    captain_toad::RecoveredNerve::Keeper<IconA> nerve_;
};

// The course thumbnail button ("ステージ決定" on InfoLevelThumbnail; 0x2d2b78, states 0x431688 ...).
class Thumbnail : public LayoutActor {
public:
    Thumbnail(std::string name, captain_toad::ui::Instance* layout, Journal& j);
    void update() override { nerve_.update(*this); }
    void appear() { appearLayout(); }
    void focus();                           // 0x4317cc: Thumbnail_IDLE (slot Main), root on
    void hide();                            // 0x431688: root off
    void press();                           // 0x1f98dc: Thumbnail_PRESS
    void markNew();                         // 0x2ab294: ThumbnailCourse_NEW (slot NEW)
    void lock();                            // 0x1d22d8: SetThumbnail_LOCKED
    void stamp(bool fresh, bool cleared);   // 0x2aa904 / 0x2aa8d4 / 0x2aa870
    bool decided() const { return nerve_.is(&Thumbnail::decidedState); }

private:
    void hiddenState(int step);
    void focusState(int step);
    void pressState(int step);
    void decidedState(int step);
    int overlay_ = 1;   // +0x50: 1 normal, 2 locked
    captain_toad::RecoveredNerve::Keeper<Thumbnail> nerve_;
};

class Page : public LayoutActor {
public:
    enum class Side { Right, Left };
    Page(std::string name, std::unique_ptr<captain_toad::ui::Instance> layout, Journal& j, Side side,
         const PageInfo& info, captain_toad::ui::Package& pkg);

    void update() override { nerve_.update(*this); }
    // Parts in the game's update order (after the page).
    std::vector<LayoutActor*> parts();

    Side side() const { return side_; }
    PageKind kind() const { return info_.kind; }
    const PageInfo& info() const { return info_; }
    Page* toward = nullptr;   // +0x74
    Page* beyond = nullptr;   // +0x78
    // The page's place in the book (+0x68 / +0x6c, set by the book).
    float x = 0, y = 0;

    void appear();            // vtable +8
    void arrive();            // +0x68
    void leave();             // +0x6c
    void showCurrent();       // +0x70
    void rest();              // +0x74
    void toStack();           // +0x78
    void hide();              // +0x7c
    void end();               // +0x64: End (back to the title)
    void hideThumbnail();     // +0xa4: the thumbnail button goes
    void unfocus();           // 0x2f8ee8: the A icon goes (the book closes)
    bool turning() const;     // +0x80
    bool settled() const;     // +0x84
    bool opening() const;     // +0x88
    void startNewLoop();      // +0x9c
    void startCourseOpen();   // 0x17c028
    void lockForGems();       // 0x17c658
    bool focus();             // 0x17bdec: the book took input with this spread open
    void choose();            // 0x17bda4
    bool decided() const;     // 0x2f8d08

private:
    void setTexts();
    void refreshRight();     // 0x2aaae4
    void refreshLeft();      // 0x2af500
    void refreshChapter();   // 0x29ac30 (illustration right page)
    void holdNewOverlay();
    void playTurn(const char* tag, float frame);
    // States.
    void hidden(int step);
    void current(int step);
    void restState(int step);
    void stack(int step);
    void leaving(int step);
    void arriving(int step);
    void arrived(int step);
    void newIntro(int step);
    void endState(int step);

    std::unique_ptr<captain_toad::ui::Instance> own_;
    Side side_;
    PageInfo info_;
    captain_toad::ui::Package& pkg_;
    captain_toad::RecoveredNerve::Keeper<Page> nerve_;
    void (Page::*afterOpen_)(int) = nullptr;   // the state to return to after the course-open
    // Course right page: thumbnail +0x90, records +0x94, complete +0x98, icon +0x9c, medal +0xb8.
    // Course left page: name +0x90, goal +0x94, gems +0x98, challenge +0x9c.
    std::unique_ptr<Thumbnail> thumbnail_;
    std::unique_ptr<IconA> icon_;
    std::unique_ptr<PagePart> records_, complete_, medal_, levelName_, goal_, gemsPart_, challenge_;
    // Illustration right page: the chapter title +0x90 (CourseInfoChapterPartsR; states 0x4306e4
    // Hide, 0x4307b8 Wait) and the crown +0x94 (the InfoCrown pane; 0x42aef8 / 0x42af48).
    std::unique_ptr<PagePart> chapter_, crown_;
};

}   // namespace book
