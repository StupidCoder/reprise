// See BookScene.hpp.
#include "session/book/BookScene.hpp"

#include "session/book/BookSound.hpp"

#include <algorithm>

namespace book {

BookScene::BookScene(captain_toad::ui::Package& pkg, BookSetup setup, const char* journalPath)
    : pkg_(pkg), setup_(std::move(setup)), journal_(journalPath) {
    top_ = std::make_unique<captain_toad::ui::Instance>(pkg_, "CourseBook_TOP");
    bottomLayout_ = std::make_unique<captain_toad::ui::Instance>(pkg_, "CourseBook_BOTTOM");
    backLayout_ = std::make_unique<captain_toad::ui::Instance>(pkg_, "ButtonBack");
    if (pkg_.hasLayout("FilterCourseOpen_TOP")) {
        filterLayout_ = std::make_unique<captain_toad::ui::Instance>(pkg_, "FilterCourseOpen_TOP");
    }
    auto toOwner = [this](const char* from) {
        return [this, from](Msg m, int arg) {
            toScene(from, m, arg);
        };
    };
    book_ = std::make_unique<Book>(top_.get(), pkg_, journal_, setup_.spreads, setup_.lastPlayedSpread, toOwner("book"),
                                   filterLayout_.get());
    bottom_ = std::make_unique<Bottom>(bottomLayout_.get(), backLayout_.get(), journal_, toOwner("bottom"),
                                       setup_.season, setup_.lives, setup_.gems);
    // Sound users (SeDataBase): the book, its pages (by side), the back button's part.
    if (Sound* s = setup_.sound) {
        book_->setSound(s, "CourseBook");
        for (int i = 0; i < book_->spreadCount(); i++) {
            book_->spread(i).right->setSound(s, "CoursePageR");
            book_->spread(i).left->setSound(s, "CoursePageL");
        }
        if (ButtonPart* b = bottom_->back().button()) {
            b->setSound(s, "ButtonBackParts");
        }
        bottom_->setSound(s, "CourseSelectLayout");
    }
    // 0x1b5268: courses that wait for gems are marked at construction.
    for (int id : setup_.gemLocked) {
        send("scene", book_.get(), Msg::MarkLocked, id);
    }
}

BookScene::~BookScene() = default;

void BookScene::send(const std::string& from, LayoutActor* to, Msg m, int arg) {
    if (journal_.on()) {
        journal_.message(from, to ? to->name() : "scene", m, arg);
    }
    if (to == book_.get()) {
        book_->receive(m, arg);
    } else if (to == bottom_.get()) {
        bottom_->receive(m, arg);
    }
}

// Messages to the owner (the scene's handler forwards them to its sequencers).
void BookScene::toScene(const std::string& from, Msg m, int arg) {
    if (journal_.on()) {
        journal_.message(from, "scene", m, arg);
    }
    switch (m) {
    case Msg::Appeared:   // 0x20d4a8: in the first state
        if (openingSeq_.is(&BookScene::openingFirst)) {
            openingSeq_.set(&BookScene::openingAppeared);
        }
        break;
    case Msg::Opened:
        if (openingSeq_.is(&BookScene::openingFirst) || openingSeq_.is(&BookScene::openingOpen)) {
            openingSeq_.set(&BookScene::openingOpened);
        }
        break;
    case Msg::BottomReady:
        if (openingSeq_.is(&BookScene::openingOpened)) {
            openingSeq_.set(&BookScene::openingDone);
        }
        break;
    case Msg::CourseOpenDone:   // 0x20d99c: the next pending course, or finish
        if (courseOpenSeq_.is(&BookScene::courseOpening) || courseOpenSeq_.is(&BookScene::courseOpeningNext)) {
            if (++pendingIndex_ < setup_.pendingOpen.size()) {
                courseOpenSeq_.set(&BookScene::courseOpeningNext);
            } else {
                courseOpenSeq_.set(&BookScene::courseOpenFinish);
            }
        }
        break;
    case Msg::Closed:
        if (nerve_.is(&BookScene::closeState)) {
            nerve_.set(&BookScene::closedState);
        }
        break;
    case Msg::ChosenDone:
        chosenDone_ = true;
        break;
    default:
        break;
    }
}

// 0x1b617c: the touch screen appears; the opening sequencer is attached; Entry.
void BookScene::start() {
    started_ = true;
    if (setup_.sound) {
        setup_.sound->startMusic(setup_.season);   // 0x229e98: BGM CourseSelect<season>
    }
    send("scene", bottom_.get(), Msg::Appear);
    openingSeq_.set(&BookScene::openingFirst);
    nerve_.set(&BookScene::entryState);
}

std::vector<LayoutActor*> BookScene::actors() {
    std::vector<LayoutActor*> out;
    out.push_back(bottom_.get());
    for (LayoutActor* a : bottom_->children()) {
        out.push_back(a);
    }
    out.push_back(book_.get());
    for (LayoutActor* a : book_->children()) {
        out.push_back(a);
    }
    return out;
}

// Every layout applies its current frames, then advances by 2 (0x37f328).
void BookScene::layoutPass() {
    top_->calc();
    bottomLayout_->calc();
    backLayout_->calc();
    if (filterLayout_) {
        filterLayout_->calc();
    }
    for (int i = 0; i < book_->spreadCount(); i++) {
        book_->spread(i).right->layout()->calc();
        book_->spread(i).left->layout()->calc();
    }
}

void BookScene::tick(const BookInput& in) {
    ++journal_.tick;
    if (!started_) {
        layoutPass();
        prev_ = in;
        return;
    }
    // Input is read by the input manager inside the scene's idle state.
    const BookInput prev = prev_;
    prev_ = in;
    pressed_.left = in.left && !prev.left;
    pressed_.right = in.right && !prev.right;
    pressed_.a = in.a && !prev.a;
    pressed_.b = in.b && !prev.b;
    pressed_.touch = in.touch && !prev.touch;
    touch_[0] = in.touchX;
    touch_[1] = in.touchY;

    layoutPass();
    nerve_.update(*this);
    openingSeq_.update(*this);
    for (LayoutActor* a : actors()) {
        if (a->alive()) {
            a->update();
        }
    }
}

// ---- Scene states -------------------------------------------------------------------------------

// 0x1b4ab4: once the opening sequencer is done: NewLoop to the book, then by the save state:
// returning from a clear with pages to open → the course-open; otherwise a pending message, or
// straight to the idle state.
void BookScene::entryState(int) {
    if (!openingSeq_.is(&BookScene::openingDone)) {
        return;
    }
    send("scene", book_.get(), Msg::NewLoop);
    if (setup_.returnedFromClear) {
        if (!setup_.pendingOpen.empty()) {
            courseOpenStart();
            nerve_.set(&BookScene::courseOpenWaitState);
            return;
        }
        nerve_.set(&BookScene::messageState);
        return;
    }
    nerve_.set(&BookScene::idleState);
}

// 0x424f08: runs the course-open sequencer; when it is done, Message.
void BookScene::courseOpenWaitState(int) {
    courseOpenSeq_.update(*this);
    if (courseOpenSeq_.is(&BookScene::courseOpenDone)) {
        nerve_.set(&BookScene::messageState);
    }
}

// 0x1b497c: no message window on the ported paths.
void BookScene::messageState(int) {
    nerve_.set(&BookScene::beforeIdleState);
}

// 0x1b64ac: CloseWipeFilter ("VolumeNormal" cue), then Idle.
void BookScene::beforeIdleState(int step) {
    if (step == 0) {
        send("scene", book_.get(), Msg::CloseWipeFilter);
        situation("VolumeNormal");   // 0x1b64b4
    }
    nerve_.set(&BookScene::idleState);
}

// 0x1b63a8: runs the input manager; at step 22 page input and the arrows come in.
void BookScene::idleState(int step) {
    runInput();
    if (step == 22) {
        send("scene", book_.get(), Msg::EnableInput);
        aAllowed_ = true;
    }
    if (decided_ && !nerve_.is(&BookScene::backState)) {
        nerve_.set(&BookScene::chosenState);
    }
}

void BookScene::chosenState(int) {}

// 0x424fc8: waits for the back button's Decide (0x417750).
void BookScene::backState(int) {
    ButtonPart* b = bottom_->back().button();
    if (!b || b->decided()) {
        nerve_.set(&BookScene::closeState);
    }
}

// 0x425044: Close to the book (0x336478), Leave to the touch screen; then the book's Closed.
void BookScene::closeState(int step) {
    if (step == 0) {
        send("scene", book_.get(), Msg::Close);
        send("scene", bottom_.get(), Msg::Leave);
    }
}

// 0x424ee0: the music fades over 60 frames (0x29ec40) and the scene ends: the title follows.
void BookScene::closedState(int step) {
    if (step == 0) {
        if (setup_.sound) {
            setup_.sound->stopMusic(60);
        }
        back_ = true;
    }
}

// The input manager (0x1eab58), one tick.
void BookScene::runInput() {
    // The back button's part is enabled while the book waits (0x2a39e0: the book in Wait), the
    // page buttons while the book can turn that way (0x2a3864 / 0x2a3834).
    if (ButtonPart* b = bottom_->back().button()) {
        if (book_->waiting()) {
            b->enableNow();
        } else {
            b->disable();
        }
    }
    // The pixel-toad feature is not available: its button goes out (0x2a3878 with 0).
    bottom_->setPixelToad(false);
    const bool canBack = book_->current() > 0;
    const bool canNext = book_->current() < book_->spreadCount() - 1;
    if (PageButton* l = bottom_->pageButton(false)) {
        canBack ? l->enable() : l->disable();
    }
    if (PageButton* r = bottom_->pageButton(true)) {
        canNext ? r->enable() : r->disable();
    }
    if (decided_) {
        return;
    }
    // Touch-screen layout coordinates: centred, y up.
    const float lx = touch_[0] - 160, ly = 120 - touch_[1];
    // B or a touch on the back button while it is enabled (0x1eab58 -> 0x41dc6c): the button
    // decides and the scene goes back to the title (0x424fc8).
    ButtonPart* backPart = bottom_->back().button();
    if (backPart && backPart->enabled() && (pressed_.b || (pressed_.touch && bottom_->back().hit(lx, ly)))) {
        backPart->decide();
        decided_ = true;
        nerve_.set(&BookScene::backState);
        return;
    }
    // A on an open course: Chosen to the book, the scene and the touch screen (0x2a3ed4).
    if (pressed_.a && aAllowed_ && book_->canChoose()) {
        if (setup_.sound) {
            setup_.sound->stopMusic(45);   // 0x1eab58 -> 0x299b4c(45): the music fades
        }
        send("input", book_.get(), Msg::Chosen);
        toScene("input", Msg::Chosen, 0);
        send("input", bottom_.get(), Msg::Chosen);
        if (ButtonPart* b = bottom_->back().button()) {
            b->disable();
        }
        decided_ = true;
        return;
    }
    bool previous = pressed_.left, next = pressed_.right;
    if (pressed_.touch) {
        PageButton* l = bottom_->pageButton(false);
        PageButton* r = bottom_->pageButton(true);
        previous = previous || (l && l->enabled() && l->hit(lx, ly));
        next = next || (r && r->enabled() && r->hit(lx, ly));
    }
    if (previous) {
        book_->turnPrevious();
    } else if (next) {
        book_->turnNext();
    }
}

// ---- Opening sequencer (0x20d4a8) ---------------------------------------------------------------

// 0x20d5e0: the book appears (or shows itself open: 0x33a320, done after 30 steps).
void BookScene::openingFirst(int step) {
    if (step == 0) {
        send("opening", book_.get(), setup_.animatedOpen ? Msg::Appear : Msg::ShowOpen);
    }
    if (!setup_.animatedOpen && step > 29) {
        openingSeq_.set(&BookScene::openingDone);
    }
}

// 0x434204: once the opening wipe has finished, BottomIntro, then open the book.
void BookScene::openingAppeared(int) {
    if (!wipeDone()) {
        return;
    }
    send("opening", bottom_.get(), Msg::BottomIntro);
    openingSeq_.set(&BookScene::openingOpen);
}

// 0x4341c4
void BookScene::openingOpen(int step) {
    if (step == 0) {
        send("opening", book_.get(), Msg::Open);
    }
}

// 0x434270: five steps after the book opened, the touch screen's curtain.
void BookScene::openingOpened(int step) {
    if (step == 5) {
        send("opening", bottom_.get(), Msg::Opened);
    }
}

void BookScene::openingDone(int) {
    openingDone_ = true;
}

// ---- Course-open sequencer (0x20da98) ------------------------------------------------------------

void BookScene::courseOpenStart() {
    pendingIndex_ = 0;
    send("courseOpen", book_.get(), Msg::CloseWipeFilter);
    situation("VolumeNormal");   // 0x20da98
    courseOpenSeq_.set(&BookScene::courseOpening);
}

// 0x4342b0: step 30 the spotlight ("VolumeDownJingle" cue); step 60 the course opens.
void BookScene::courseOpening(int step) {
    if (step == 30) {
        send("courseOpen", book_.get(), Msg::SpotlightAppear);
        situation("VolumeDownJingle");   // 0x259e78
    }
    if (step == 60) {
        situation("VolumeNormal");
        const int id = setup_.pendingOpen[pendingIndex_];
        if (onReveal) {
            onReveal(id);
        }
        send("courseOpen", book_.get(), Msg::CourseOpen, id);
    }
}

// 0x259e70: a further course opens at once.
void BookScene::courseOpeningNext(int step) {
    if (step == 0) {
        const int id = setup_.pendingOpen[pendingIndex_];
        if (onReveal) {
            onReveal(id);
        }
        send("courseOpen", book_.get(), Msg::CourseOpen, id);
    }
}

// 0x4342b8: SpotlightEnd; done after step 21.
void BookScene::courseOpenFinish(int step) {
    if (step == 0) {
        send("courseOpen", book_.get(), Msg::SpotlightEnd);
    }
    if (step > 21) {
        courseOpenSeq_.set(&BookScene::courseOpenDone);
    }
}

void BookScene::courseOpenDone(int) {}

// ---- Drawing --------------------------------------------------------------------------------------

void BookScene::drawTop(float fov) {
    top_->drawScreen(fov);
    if (filterLayout_) {
        filterLayout_->drawScreen(0);
    }
}

void BookScene::drawBottom() {
    bottomLayout_->drawScreen(0);
    backLayout_->drawScreen(0);
}

void BookScene::situation(const char* name) {
    if (setup_.sound) {
        setup_.sound->situation(name);   // 0x2f6b38
    }
}

}   // namespace book
