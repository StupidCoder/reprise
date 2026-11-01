// =============================================================================================
// BookScene.hpp - the course book scene: entry, sequencers, input and the tick order
// =============================================================================================
//
// The scene (CourseSelectScene, initialiser 0x1b5268) builds the book, the touch screen and two
// sequencers, and runs them in the game's order each 30 Hz tick (book-order-a): the scene's state,
// the opening sequencer, the layout pass (every layout applies its frames and advances by 2),
// then the actors: the touch screen, its page buttons and the back button, the book, the arrows,
// and each spread's right page and parts, left page and parts.
//
// Scene states:
//   Entry           0x1b4ab4  waits for the opening sequencer; NewLoop; then by the save state
//   CourseOpenWait  0x424f08  runs the course-open sequencer until it is done
//   Message         0x1b497c  a pending message window (none on the ported paths)
//   BeforeIdle      0x1b64ac  CloseWipeFilter
//   Idle            0x1b63a8  runs the input manager; EnableInput at step 22
//   Chosen                    after the course is chosen, until the book's ChosenDone
// Opening sequencer (0x20d4a8 class): Appear the book → (Appeared) BottomIntro → Open the book
// → (Opened) five steps → Opened to the touch screen (its curtain) → (BottomReady) done.
// Course-open sequencer (0x20da98): step 30 SpotlightAppear, step 60 the course opens (save
// record bit 0, CourseOpen); after CourseOpenDone the next pending course opens at once, or
// SpotlightEnd and done 21 steps later.
// Input manager (0x1eab58): page buttons follow whether the book can turn; left / right turn the
// page; A on an open course chooses it; the back button leaves the book.
// =============================================================================================
#pragma once
#include "session/book/BookBottom.hpp"
#include "session/book/CourseBook.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace book {

struct BookSetup {
    std::vector<PageInfo> spreads;
    int lastPlayedSpread = 0;         // the spread of the last played course (0x2b89b8, 0x2f8854)
    bool returnedFromClear = false;   // 0x2b8470
    std::vector<int> pendingOpen;     // course ids the course-open sequencer opens, in order
    std::vector<int> gemLocked;       // course ids waiting for gems (MarkLocked at construction)
    int season = 1, lives = 2, gems = 0;
    bool animatedOpen = true;       // Appear + OpenBook; false: ShowOpen (0x2facbc)
    class Sound* sound = nullptr;   // music and sound effects (BookSound.hpp), if any
};

struct BookInput {
    bool left = false, right = false, a = false, b = false;   // held
    bool touch = false;
    float touchX = 0, touchY = 0;   // touch-screen pixels (0..320, 0..240, y down)
};

class BookScene {
public:
    BookScene(captain_toad::ui::Package& pkg, BookSetup setup, const char* journalPath = nullptr);
    ~BookScene();

    // The scene starts (0x1b617c): from the tick after the opening wipe, or at once.
    void start();
    // One 30 Hz tick.
    void tick(const BookInput& in);
    // While the opening wipe (WipeOpening) still plays its End: the opening sequencer's 0x434204
    // waits for it.
    std::function<bool()> wipeDone = [] {
        return true;
    };
    // Called when the course-open sequencer opens a course (the save record's bit 0, 0x138134).
    std::function<void(int courseId)> onReveal;

    void drawTop(float fov);
    void drawBottom();

    bool chosen() const { return chosenDone_; }
    int chosenSpread() const { return book_->current(); }
    bool backRequested() const { return back_; }
    int currentSpread() const { return book_->current(); }

private:
    void send(const std::string& from, LayoutActor* to, Msg m, int arg = 0);
    void toScene(const std::string& from, Msg m, int arg);
    std::vector<LayoutActor*> actors();
    void layoutPass();
    void runInput();

    // Scene states.
    void entryState(int step);
    void courseOpenWaitState(int step);
    void messageState(int step);
    void beforeIdleState(int step);
    void idleState(int step);
    void backState(int step);
    void closeState(int step);
    void closedState(int step);
    void chosenState(int step);
    // Opening sequencer.
    void openingFirst(int step);
    void openingAppeared(int step);
    void openingOpen(int step);
    void openingOpened(int step);
    void openingDone(int step);
    // Course-open sequencer.
    void courseOpenStart();
    void courseOpening(int step);
    void courseOpeningNext(int step);
    void courseOpenFinish(int step);
    void courseOpenDone(int step);

    captain_toad::ui::Package& pkg_;
    BookSetup setup_;
    Journal journal_;
    std::unique_ptr<captain_toad::ui::Instance> top_, bottomLayout_, backLayout_, filterLayout_;
    std::unique_ptr<Book> book_;
    std::unique_ptr<Bottom> bottom_;
    captain_toad::RecoveredNerve::Keeper<BookScene> nerve_;           // the scene
    captain_toad::RecoveredNerve::Keeper<BookScene> openingSeq_;      // 0x20d4a8
    captain_toad::RecoveredNerve::Keeper<BookScene> courseOpenSeq_;   // 0x20da98
    bool started_ = false;
    bool openingDone_ = false, courseOpenDone_ = false;
    size_t pendingIndex_ = 0;
    // Input manager.
    bool aAllowed_ = false;   // +0x11, from Idle step 22
    bool decided_ = false;
    BookInput prev_;
    struct Pressed {
        bool left = false, right = false, a = false, b = false, touch = false;
    } pressed_;
    float touch_[2] = {0, 0};
    bool chosenDone_ = false;
    void situation(const char* name);
    bool back_ = false;
};

}   // namespace book
