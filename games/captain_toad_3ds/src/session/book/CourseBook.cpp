// See CourseBook.hpp.
#include "session/book/CourseBook.hpp"

#include "session/book/BookSound.hpp"

#include <algorithm>

namespace book {

// ---- Arrows (CourseBookArrows) --------------------------------------------------------------

// Constructor 0x19ecd8: the first state is the loop (PageArrow_LOOP, traced at the book's start).
Arrows::Arrows(captain_toad::ui::Instance* layout, Journal& j) : LayoutActor("arrows", layout, j) {
    nerve_.set(&Arrows::looping);
}

void Arrows::show() {
    nerve_.set(&Arrows::showing);
}

// 0x2f9478: only from the intro or the loop.
void Arrows::hide() {
    if (nerve_.is(&Arrows::looping) || nerve_.is(&Arrows::showing)) {
        nerve_.set(&Arrows::hiding);
    }
}

void Arrows::showSides(bool left, bool right) {
    showPane("MasterL", left);
    showPane("MasterR", right);
}

void Arrows::outro() {
    showPane("root", false);
    start("PageArrow_OUTRO");
}

// 0x4220f4: PageArrow_INTRO held at its first frame, played from step 20, then the loop.
void Arrows::showing(int step) {
    if (step == 0) {
        hold("PageArrow_INTRO");
    }
    if (step == 20) {
        start("PageArrow_INTRO");
    }
    if (step > 19 && ended()) {
        nerve_.set(&Arrows::looping);
    }
}

// 0x4220b8
void Arrows::looping(int step) {
    if (step == 0) {
        start("PageArrow_LOOP");
    }
}

// 0x421ffc: the intro or the loop stops, PageArrow_OUTRO.
void Arrows::hiding(int step) {
    if (step != 0) {
        return;
    }
    if (playing("PageArrow_INTRO")) {
        stop("PageArrow_INTRO");
        start("PageArrow_OUTRO");
    } else if (playing("PageArrow_LOOP")) {
        stop("PageArrow_LOOP");
        start("PageArrow_OUTRO");
    }
}

// ---- Filter (FilterCourseOpen) ----------------------------------------------------------------

Filter::Filter(std::string name, captain_toad::ui::Instance* layout, Journal& j)
    : LayoutActor(std::move(name), layout, j) {}

// 0x2f8a5c with duration -1: Appear at its own pace.
void Filter::appearNow() {
    appearLayout();
    start("Appear");
    nerve_.set(&Filter::appearing);
}

// 0x2f8b64
void Filter::end() {
    if (!alive_) {
        appearLayout();
    }
    start("End");
    nerve_.set(&Filter::ending);
}

// 0x419a28
void Filter::appearing(int step) {
    if (step != 0 && ended("Appear")) {
        nerve_.set(&Filter::waitState);
    }
}

// 0x419ab4
void Filter::waitState(int step) {
    if (step == 0) {
        start("Wait");
    }
}

// 0x419944
void Filter::ending(int) {
    if (ended()) {
        nerve_.set(&Filter::gone);
    }
}

// 0x419a88: the actor dies (no longer drawn).
void Filter::gone(int step) {
    if (step == 0) {
        alive_ = false;
        layout_->setShown(false);
    }
}

// ---- Book -------------------------------------------------------------------------------------

Book::Book(captain_toad::ui::Instance* layout, captain_toad::ui::Package& pkg, Journal& j, std::vector<PageInfo> infos,
           int lastPlayedSpread, std::function<void(Msg, int)> send, captain_toad::ui::Instance* filterLayout)
    : LayoutActor("book", layout, j), pkg_(pkg), send_(std::move(send)), lastPlayed_(lastPlayedSpread) {
    // 0x1b5268: a right and a left page per spread; their layouts are instances of the book's
    // page templates.
    for (size_t i = 0; i < infos.size(); i++) {
        Spread s;
        s.info = infos[i];
        const std::string n = "page" + std::to_string(i);
        s.right = std::make_unique<Page>(n + "R", std::make_unique<captain_toad::ui::Instance>(pkg, "CoursePageR"), j,
                                         Page::Side::Right, s.info, pkg);
        s.left = std::make_unique<Page>(n + "L", std::make_unique<captain_toad::ui::Instance>(pkg, "CoursePageL"), j,
                                        Page::Side::Left, s.info, pkg);
        spreads_.push_back(std::move(s));
    }
    // 0x136d3c: links. A right page knows the next spread's left and right pages, a left page the
    // previous spread's right and left pages.
    for (size_t i = 0; i < spreads_.size(); i++) {
        if (i + 1 < spreads_.size()) {
            spreads_[i].right->toward = spreads_[i + 1].left.get();
            spreads_[i].right->beyond = spreads_[i + 1].right.get();
        }
        if (i > 0) {
            spreads_[i].left->toward = spreads_[i - 1].right.get();
            spreads_[i].left->beyond = spreads_[i - 1].left.get();
        }
    }
    if (captain_toad::ui::Instance* a = layout_->part("Arrows")) {
        arrows_ = std::make_unique<Arrows>(a, j);
    }
    if (filterLayout) {
        filter_ = std::make_unique<Filter>("filter", filterLayout, j);
    }
    placePagesOnLayout();
}

std::vector<LayoutActor*> Book::children() {
    std::vector<LayoutActor*> out;
    if (arrows_) {
        out.push_back(arrows_.get());
    }
    for (auto& s : spreads_) {
        out.push_back(s.right.get());
        for (LayoutActor* p : s.right->parts()) {
            out.push_back(p);
        }
        out.push_back(s.left.get());
        for (LayoutActor* p : s.left->parts()) {
            out.push_back(p);
        }
    }
    if (filter_) {
        out.push_back(filter_.get());
    }
    return out;
}

// The pages are drawn as the book's page parts: the left pages in book order, the right pages in
// reverse order (the pages nearest the open spread on top), each at its place.
void Book::placePagesOnLayout() {
    std::vector<captain_toad::ui::Instance*> left, right;
    const int n = int(spreads_.size());
    for (int i = 0; i < n; i++) {
        Spread& s = spreads_[size_t(i)];
        s.left->layout()->setOffset(s.left->x, s.left->y);
        s.right->layout()->setOffset(s.right->x, s.right->y);
        left.push_back(s.left->layout());
        right.push_back(spreads_[size_t(n - 1 - i)].right->layout());
    }
    placeArrows();
    layout_->setPartInstances("Parts_00", left);
    layout_->setPartInstances("Parts_01", right);
    // The template panes are hidden in the layout; each page actor draws its own copy (shown here
    // through the template panes).
    layout_->writeVisible("Parts_00", n > 0);
    layout_->writeVisible("Parts_01", n > 0);
}

// The arrows stand at the open spread's place: in the original they move 2 units per spread with
// the pages (front-right3-a: three turns, the arrows at x ±156 plus the open spread's x). The code
// that places them was not located; hypothesis: the same page offset as the pages.
void Book::placeArrows() {
    const int n = int(spreads_.size());
    if (arrows_ && n > 0) {
        const Spread& c = spreads_[size_t(std::clamp(current_, 0, n - 1))];
        arrows_->layout()->setOffset(c.right->x, c.right->y);
    }
}

// ---- Messages (0x135e00) -------------------------------------------------------------------------

void Book::receive(Msg m, int arg) {
    switch (m) {
    case Msg::Appear:
        appear();
        return;
    case Msg::ShowOpen:
        showOpen();
        return;
    case Msg::MarkLocked:
        for (auto& s : spreads_) {
            if (s.info.entry->courseId == arg) {
                s.right->lockForGems();
            }
        }
        return;
    default:
        break;
    }
    if (!alive_) {
        return;
    }
    switch (m) {
    case Msg::Open:
        cover_ = Cover::Opening;   // 0x18b4c4
        nerve_.set(&Book::openState);
        break;
    case Msg::EnableInput:
        input_ = true;
        if (arrows_) {
            arrows_->setRootVisible(true);   // 0x19e9f4
            arrows_->show();
        }
        focusCurrent();
        break;
    case Msg::DisableInput:
        input_ = false;
        if (arrows_) {
            arrows_->outro();
            arrows_->hide();
        }
        break;
    case Msg::NewLoop:
        for (auto& s : spreads_) {
            s.right->startNewLoop();
            s.left->startNewLoop();
        }
        break;
    case Msg::Chosen:
        // 0x2f8d74 branch: only in Wait, on an open course whose pages have settled.
        if (nerve_.is(&Book::waitState) && canChoose() && allSettled()) {
            Spread& s = spreads_[size_t(current_)];
            if (!s.right->decided()) {
                s.right->choose();
            }
            nerve_.set(&Book::chosenState);
        }
        break;
    case Msg::Close:
        nerve_.set(&Book::closeRequestState);
        break;
    case Msg::CourseOpen:
        playSound("PgOpenNewPage");   // 0x135d80
        openCourse_ = arg;
        nerve_.set(&Book::courseOpenState);
        break;
    case Msg::SpotlightAppear:
        if (filter_ && !filter_->alive()) {
            filter_->appearNow();
        }
        break;
    case Msg::SpotlightEnd:
        if (filter_ && filter_->waiting()) {
            filter_->end();
        }
        break;
    case Msg::CloseWipeFilter:
        break;   // the wipe filter is not used on these paths
    default:
        break;
    }
}

bool Book::canChoose() const {
    if (spreads_.empty()) {
        return false;
    }
    const Spread& s = spreads_[size_t(current_)];
    return s.info.kind != PageKind::Illustration && s.info.opened;
}

bool Book::allSettled() const {
    for (const auto& s : spreads_) {
        if (!s.right->settled() || !s.left->settled()) {
            return false;
        }
    }
    return true;
}

// Appear (vtable +8, 0x136b08): the season's colour, the cover and every page appear; the right
// pages wait at the closed book's place x0 = N − 1; the open spread is the last played course's.
void Book::appear() {
    start("Season" + std::to_string(std::min(spreads_.empty() ? 1 : spreads_[0].info.entry->season, 3)), "Color");
    appearLayout();
    cover_ = Cover::Wait;   // 0x18b32c: the cover appears and waits at once
    if (arrows_) {
        arrows_->appear();
    }
    for (auto& s : spreads_) {
        s.right->appear();
        s.left->appear();
    }
    const float x0 = float(int(spreads_.size()) - 1);
    for (auto& s : spreads_) {
        s.right->x = x0;
        s.right->y = 0;
    }
    current_ = std::clamp(lastPlayed_, 0, std::max(0, int(spreads_.size()) - 1));
    placePagesOnLayout();
    nerve_.set(&Book::appearState);
}

// 0x136848: the book shown open (returning from a course): every page at its final state.
void Book::showOpen() {
    current_ = std::clamp(lastPlayed_, 0, std::max(0, int(spreads_.size()) - 1));
    const int n = int(spreads_.size());
    for (int i = 0; i < n; i++) {
        Spread& s = spreads_[size_t(i)];
        s.right->x = s.left->x = float(2 * i + 1 - n);
        s.right->y = s.left->y = 0;
        s.right->appear();
        s.left->appear();
    }
    for (int i = 0; i < n; i++) {
        Spread& s = spreads_[size_t(i)];
        if (i < current_) {
            s.right->hide();
            s.left->toStack();
        } else if (i == current_) {
            s.right->rest();
            s.left->rest();
        } else {
            s.right->toStack();
            s.left->hide();
        }
    }
    appearLayout();
    if (arrows_) {
        arrows_->appear();
    }
    placePagesOnLayout();
    send_(Msg::Opened, 0);
    nerve_.set(&Book::waitState);
}

void Book::setInput(bool on) {
    input_ = on;
}

// 0x2f9004: with input on, the arrows come in and the open course is focused.
void Book::focusCurrent() {
    if (!input_) {
        return;
    }
    if (arrows_) {
        arrows_->show();
        arrows_->showSides(current_ > 0, current_ < int(spreads_.size()) - 1);
    }
    if (!spreads_.empty() && spreads_[size_t(current_)].info.opened) {
        spreads_[size_t(current_)].right->focus();
    }
}

// 0x2f846c: one spread forward (from Wait or during a turn), not past the last spread. The
// "NextPage" sound's pitch moves by 0.03 between 0.85 and 1.15 (sound not ported yet).
bool Book::turnNext() {
    if (!input_ || current_ >= int(spreads_.size()) - 1) {
        return false;
    }
    if (!nerve_.is(&Book::waitState) && !nerve_.is(&Book::forwardState) && !nerve_.is(&Book::turnEndState)) {
        return false;
    }
    if (pitchReset_) {
        pitchReset_ = false;
        pitch_ = 1.0f;
        pitchStep_ = 0.03f;
    }
    playSound("NextPage", pitch_);   // 0x2f8994; the pitch steps after it
    pitch_ += pitchStep_;
    if (pitch_ >= 1.15f) {
        pitchStep_ = -0.03f;
    } else if (pitch_ <= 0.85f) {
        pitchStep_ = 0.03f;
    }
    target_ = current_;
    nerve_.set(&Book::forwardState);
    return true;
}

// 0x2f8620: one spread back.
bool Book::turnPrevious() {
    if (!input_ || current_ <= 0) {
        return false;
    }
    if (!nerve_.is(&Book::waitState) && !nerve_.is(&Book::backState) && !nerve_.is(&Book::turnEndState)) {
        return false;
    }
    // The same pitch cycle as a forward turn, with "PrevPage".
    if (pitchReset_) {
        pitchReset_ = false;
        pitch_ = 1.0f;
        pitchStep_ = 0.03f;
    }
    playSound("PrevPage", pitch_);   // 0x2f8994
    pitch_ += pitchStep_;
    if (pitch_ >= 1.15f) {
        pitchStep_ = -0.03f;
    } else if (pitch_ <= 0.85f) {
        pitchStep_ = 0.03f;
    }
    target_ = current_;
    nerve_.set(&Book::backState);
    return true;
}

// ---- States ------------------------------------------------------------------------------------

// 0x412400: AppearBook; once it has ended, Appeared every tick.
void Book::appearState(int step) {
    if (step == 0) {
        start("AppearBook");
    }
    if (ended("AppearBook")) {
        send_(Msg::Appeared, 0);
    }
}

// 0x137050: OpenBook; the pages' first states (before the open spread: turned away; the open
// one current; later ones in the right stack); at step 12 the first left page comes down. Every
// step the spreads move from x0 = N − 1 towards 2i − (N − 1) over 24 steps. Done when all pages
// have settled, OpenBook no longer plays and the cover waits.
void Book::openState(int step) {
    const int n = int(spreads_.size());
    if (step == 0) {
        start("OpenBook");
        for (int i = 0; i < n; i++) {
            Spread& s = spreads_[size_t(i)];
            if (i < current_) {
                s.right->leave();
            } else if (i == current_) {
                s.right->showCurrent();
            } else {
                s.right->toStack();
            }
        }
    }
    if (step == 12 && n > 0) {
        spreads_[0].left->arrive();
    }
    if (cover_ == Cover::Opening) {
        cover_ = Cover::Wait;
    }
    const float x0 = float(2 * (n - 1)) * 0.5f;
    const float t = std::min(float(step) / 24.f, 1.f);
    for (int i = 0; i < n; i++) {
        const float target = 2.f * (float(i) + float(1 - n) * 0.5f);
        const float x = x0 + (target - x0) * t;
        spreads_[size_t(i)].right->x = spreads_[size_t(i)].left->x = x;
        spreads_[size_t(i)].right->y = spreads_[size_t(i)].left->y = 0;
    }
    placePagesOnLayout();
    for (const auto& s : spreads_) {
        if (!s.right->settled() || !s.left->settled()) {
            return;
        }
    }
    if (playing("OpenBook") || cover_ != Cover::Wait) {
        return;
    }
    send_(Msg::Opened, 0);
    nerve_.set(&Book::waitState);
}

// 0x412300: WaitBook; input on.
void Book::waitState(int step) {
    if (step == 0) {
        start("WaitBook");
        input_ = true;
    }
}

// 0x4125b0: the open spread's right page leaves (its left page's +0x98: nothing on these
// pages), the arrows go; one spread per tick until past the target.
void Book::forwardState(int) {
    Spread& s = spreads_[size_t(current_)];
    s.right->leave();
    if (arrows_) {
        arrows_->hide();
    }
    ++current_;
    if (current_ <= target_) {
        return;
    }
    nerve_.set(&Book::turnEndState);
}

// 0x412514
void Book::backState(int) {
    Spread& s = spreads_[size_t(current_)];
    s.left->leave();
    if (arrows_) {
        arrows_->hide();
    }
    --current_;
    if (target_ <= current_) {
        return;
    }
    nerve_.set(&Book::turnEndState);
}

// 0x4127c4 / 0x4126e8: when every page has settled: TurnDone, the arrows' sides, Wait; with input
// on the arrows come in and the open course is focused.
void Book::turnEndState(int) {
    if (!allSettled() || playing("OpenBook")) {
        return;
    }
    if (arrows_) {
        arrows_->showSides(current_ > 0, current_ < int(spreads_.size()) - 1);
    }
    send_(Msg::TurnDone, 0);
    nerve_.set(&Book::waitState);
    focusCurrent();
}

// 0x135cd0: the spread of course openCourse_ shows the opening ("PgOpenNewPage" sound), then
// CourseOpenDone and Wait.
void Book::courseOpenState(int step) {
    int index = 0;
    for (int i = 0; i < int(spreads_.size()); i++) {
        if (spreads_[size_t(i)].info.entry->courseId == openCourse_) {
            index = i;
            break;
        }
    }
    Spread& s = spreads_[size_t(index)];
    if (step == 0) {
        s.info.opened = true;
        s.right->startCourseOpen();
        s.left->startCourseOpen();
    }
    if (step > 0 && !s.right->opening() && !s.left->opening()) {
        send_(Msg::CourseOpenDone, 0);
        nerve_.set(&Book::waitState);
    }
}

// 0x412470: the arrows go ("PgCourseDecided" sound); at step 50, once the course's press has
// played, ChosenDone.
void Book::chosenState(int step) {
    if (step == 0 && arrows_) {
        arrows_->hide();
    }
    if (step == 0 && sound_) {
        sound_->play("ProductSequence", "PgCourseDecided");   // 0x2f80a4 on the sequence's user
    }
    if (spreads_[size_t(current_)].right->decided() && step == 50) {
        send_(Msg::ChosenDone, 0);
    }
}

// ---- Closing (back to the title) ----------------------------------------------------------------

// 0x412398: the arrows go; the open spread lets go of its input (0x2f8ee8: the A icon out); after
// 10 steps CloseBook.
void Book::closeRequestState(int step) {
    if (step == 0) {
        if (arrows_) {
            arrows_->hide();
        }
        spreads_[size_t(current_)].right->unfocus();
    }
    if (step >= 10) {
        nerve_.set(&Book::closeBookState);
    }
}

// 0x1375e0: CloseBook. Step 0: every right page from the open spread on turns away (+0x6c), the
// later spreads' thumbnail buttons go (+0xa4). Every step the spreads move from their open place
// 2i - (N - 1) to the closed side's 1 - N over 24 steps (the reverse of OpenBook). On the last
// spread the previous spread's left page shows (+0x70). When CloseBook has ended, End.
void Book::closeBookState(int step) {
    const int n = int(spreads_.size());
    if (step == 0) {
        start("CloseBook");
        for (int i = 0; i < n; i++) {
            if (i >= current_) {
                spreads_[size_t(i)].right->leave();
            }
            if (i > current_) {
                spreads_[size_t(i)].right->hideThumbnail();
            }
        }
    }
    const float closed = float(1 - n);
    const float t = std::min(float(step) / 24.f, 1.f);
    for (int i = 0; i < n; i++) {
        const float open = 2.f * (float(i) + float(1 - n) * 0.5f);
        const float x = open + (closed - open) * t;
        spreads_[size_t(i)].right->x = spreads_[size_t(i)].left->x = x;
        spreads_[size_t(i)].right->y = spreads_[size_t(i)].left->y = 0;
    }
    placePagesOnLayout();
    if (current_ == n - 1 && current_ > 0) {
        spreads_[size_t(current_ - 1)].left->showCurrent();
    }
    if (ended("CloseBook")) {
        nerve_.set(&Book::endState);
    }
}

// 0x412238: Curtain_OUTRO (slot Curtain), EndBook, every page ends (0x2f8440); once EndBook has
// ended, Closed to the scene every tick.
void Book::endState(int step) {
    if (step == 0) {
        start("Curtain_OUTRO", "Curtain");
        start("EndBook");
        for (auto& s : spreads_) {
            s.right->end();
            s.left->end();
        }
    }
    if (ended("EndBook")) {
        send_(Msg::Closed, 0);
    }
}

}   // namespace book
