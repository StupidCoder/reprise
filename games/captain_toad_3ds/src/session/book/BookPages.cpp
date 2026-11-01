// See BookPages.hpp. Addresses: states and methods of the page classes (vtables 0x4c85b0 course
// right, 0x4c72f0 course left, 0x4cd678 illustration right, 0x4ccf7c illustration left).
#include "session/book/BookPages.hpp"

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

// ---- Page parts ---------------------------------------------------------------------------------

PagePart::PagePart(std::string name, captain_toad::ui::Instance* layout, Journal& j, const char* hideTag, bool hideRoot,
                   const char* showTag, bool showRoot)
    : LayoutActor(std::move(name), layout, j), hideTag_(hideTag), hideRoot_(hideRoot), showTag_(showTag),
      showRoot_(showRoot) {
    nerve_.set(&PagePart::hidden);
}

void PagePart::hidden(int step) {
    if (step == 0) {
        if (hideRoot_) {
            showPane("root", false);
        }
        if (hideTag_) {
            start(hideTag_);
        }
    }
}

void PagePart::shown(int step) {
    if (step == 0) {
        if (showTag_) {
            start(showTag_);
        }
        if (showRoot_) {
            showPane("root", true);
        }
    }
}

// ---- A icon (IconAParts) --------------------------------------------------------------------

IconA::IconA(std::string name, captain_toad::ui::Instance* layout, Journal& j)
    : LayoutActor(std::move(name), layout, j) {
    nerve_.set(&IconA::off);
}

// 0x41301c
void IconA::off(int step) {
    if (step == 0) {
        start("OFF");
    }
}

// 0x2aa244 (OFF bound at once), then 0x413094.
void IconA::show() {
    start("OFF");
    nerve_.set(&IconA::intro);
}

// 0x413094: INTRO, then IDLE once it has ended.
void IconA::intro(int step) {
    if (step == 0) {
        start("INTRO");
    }
    if (ended("INTRO")) {
        nerve_.set(&IconA::idle);
    }
}

// 0x413058
void IconA::idle(int step) {
    if (step == 0) {
        start("IDLE");
    }
}

void IconA::press() {
    nerve_.set(&IconA::pressed);
}

// 0x413104: stop the idle, PRESS, then nothing (0x413188).
void IconA::pressed(int step) {
    if (step == 0) {
        stop("IDLE");
        start("PRESS");
    }
    if (ended("PRESS")) {
        nerve_.set(&IconA::done);
    }
}

void IconA::done(int) {}

void IconA::hideIcon() {
    nerve_.set(&IconA::outro);
}

// 0x412f84: IDLE and INTRO stop, OUTRO; when it has ended, OFF (0x41301c).
void IconA::outro(int step) {
    if (step == 0) {
        stop("IDLE");
        stop("INTRO");
        start("OUTRO");
    }
    if (ended("OUTRO")) {
        nerve_.set(&IconA::off);
    }
}

// ---- Thumbnail button ("ステージ決定" on InfoLevelThumbnail) -------------------------------------

Thumbnail::Thumbnail(std::string name, captain_toad::ui::Instance* layout, Journal& j)
    : LayoutActor(std::move(name), layout, j) {
    // Constructor (traced at calls 1154-1160): the overlay's normal state.
    start("SetThumbnail_NORMAL", "ThumbnailOverlay");
    nerve_.set(&Thumbnail::hiddenState);
}

void Thumbnail::focus() {
    nerve_.set(&Thumbnail::focusState);
}

void Thumbnail::hide() {
    nerve_.set(&Thumbnail::hiddenState);
}

void Thumbnail::press() {
    nerve_.set(&Thumbnail::pressState);
}

void Thumbnail::markNew() {
    start("ThumbnailCourse_NEW", "NEW");
}

void Thumbnail::lock() {
    start("SetThumbnail_LOCKED", "ThumbnailOverlay");
    overlay_ = 2;
}

// 0x2aa904: a newly opened course (normal overlay) shows the NEW stamp (slot State) and its
// Clear_Master; otherwise the stamp stops (0x2aa870) or stays (0x2aa8d4 with bit 4) and
// Clear_Master is off.
void Thumbnail::stamp(bool fresh, bool cleared) {
    if (fresh && overlay_ == 1) {
        stop("ThumbnailCourse_NEW", "NEW");
        start("ThumbnailCourse_NEW", "State");
        showPane("Clear_Master", true);
        return;
    }
    if (!cleared) {
        stop("ThumbnailCourse_NEW", "State");
    }
    showPane("Clear_Master", false);
}

// 0x431688: root off ("HideStartPG" sound, not ported yet).
void Thumbnail::hiddenState(int step) {
    if (step == 0) {
        showPane("root", false);
    }
}

// 0x4317cc
void Thumbnail::focusState(int step) {
    if (step == 0) {
        start("Thumbnail_IDLE", "Main");
        showPane("root", true);
    }
}

// 0x1f98dc: Thumbnail_PRESS (slot Main); sounds "DecideStartPG" and "CourseIn" (not ported yet).
void Thumbnail::pressState(int step) {
    if (step == 0) {
        start("Thumbnail_PRESS", "Main");
    }
    if (ended("Thumbnail_PRESS")) {
        nerve_.set(&Thumbnail::decidedState);
    }
}

// 0x431870
void Thumbnail::decidedState(int) {}

// ---- Page -----------------------------------------------------------------------------------

Page::Page(std::string name, std::unique_ptr<captain_toad::ui::Instance> layout, Journal& j, Side side,
           const PageInfo& info, captain_toad::ui::Package& pkg)
    : LayoutActor(std::move(name), layout.get(), j), own_(std::move(layout)), side_(side), info_(info), pkg_(pkg) {
    auto part = [&](const char* pane) {
        return layout_->part(pane);
    };
    const std::string n = name_ + ".";
    if (info_.kind == PageKind::Course && side_ == Side::Right) {
        if (captain_toad::ui::Instance* t = part("InfoLevelThumbnail")) {
            thumbnail_ = std::make_unique<Thumbnail>(n + "thumbnail", t, j);
        }
        if (captain_toad::ui::Instance* r = part("InfoRecords")) {
            records_ = std::make_unique<PagePart>(n + "records", r, j, nullptr, true, nullptr, true);
            records_->start("SetText_COMPLETE", "Text");
        }
        if (captain_toad::ui::Instance* c = part("InfoCourseComplete")) {
            complete_ = std::make_unique<PagePart>(n + "complete", c, j, "Hide", false, "IDLE", false);
        }
        if (captain_toad::ui::Instance* a = part("InfoAButton")) {
            icon_ = std::make_unique<IconA>(n + "icon", a, j);
        }
        if (captain_toad::ui::Instance* m = part("InfoPixelToadMedal")) {
            medal_ = std::make_unique<PagePart>(n + "medal", m, j, "Hide", true, "Wait", false);
        }
    } else if (info_.kind == PageKind::Course) {
        if (captain_toad::ui::Instance* p = part("InfoLevelName")) {
            levelName_ = std::make_unique<PagePart>(n + "name", p, j, "Hide", true, "Wait", true);
        }
        if (captain_toad::ui::Instance* p = part("InfoGoalStar")) {
            goal_ = std::make_unique<PagePart>(n + "goal", p, j, nullptr, true, nullptr, true);
        }
        if (captain_toad::ui::Instance* p = part("InfoItems")) {
            gemsPart_ = std::make_unique<PagePart>(n + "gems", p, j, "Hide", true, "Wait", true);
        }
        if (captain_toad::ui::Instance* p = part("InfoChallenge")) {
            challenge_ = std::make_unique<PagePart>(n + "challenge", p, j, "Hide", true, "IDLE", true);
        }
    } else if (info_.kind == PageKind::Illustration && side_ == Side::Right) {
        if (captain_toad::ui::Instance* c = part("InfoChapter")) {
            chapter_ = std::make_unique<PagePart>(n + "chapter", c, j, "Hide", true, "Wait", true);
        }
        crown_ = std::make_unique<PagePart>(n + "crown", nullptr, j, "Hide", true, "Wait", true);
    } else if (side_ == Side::Left) {
        if (captain_toad::ui::Instance* a = part("InfoAButton")) {
            icon_ = std::make_unique<IconA>(n + "icon", a, j);
        }
    }
    setTexts();
}

std::vector<LayoutActor*> Page::parts() {
    std::vector<LayoutActor*> out;
    for (LayoutActor* a : std::initializer_list<LayoutActor*>{
             thumbnail_.get(), records_.get(), complete_.get(), icon_.get(), medal_.get(), levelName_.get(),
             goal_.get(), gemsPart_.get(), challenge_.get(), chapter_.get(), crown_.get()}) {
        if (a) {
            out.push_back(a);
        }
    }
    return out;
}

// Texts and textures from the course (the page constructors; 0x2af848 on the left page).
void Page::setTexts() {
    const captain_toad::CourseEntry& e = *info_.entry;
    const std::string season = "Season" + std::to_string(std::min(e.season, 3));
    if (info_.kind == PageKind::Illustration) {
        if (side_ == Side::Left) {
            const int tex = pkg_.packTexture("BookIllustrations/" + e.stage);
            if (tex >= 0) {
                layout_->setTexture("img_illustration", tex);
            }
            if (captain_toad::ui::Instance* c = layout_->part("ParChapter")) {
                c->start("PageL", "State");
            }
        } else if (captain_toad::ui::Instance* chapter = layout_->part("InfoChapter")) {
            chapter->start("PageR", "State");   // 0x1f2ef4
            // 0x1f2c9c: the title in the region's variant (Chapter_JP / Chapter_NA by 0x2e1ea8;
            // hypothesis as on the course pages: this release is not the Japanese region).
            chapter->writeVisible("Chapter_JP", false);
            chapter->writeVisible("Chapter_NA", true);
            chapter->setText("TxtChapter_NA", pkg_.message("ChapterName", e.stage));
        }
        return;
    }
    if (side_ == Side::Left) {
        if (captain_toad::ui::Instance* info = layout_->part("InfoLevelName")) {
            info->start(season, "Color");
            info->hold("SetPageType_NORMAL", "", 0);
            info->setText("txt_stage", number(info_.courseNumber));
            info->setText("TxtCourseInfo_NA", pkg_.message("StageName", e.stage));
        }
        if (captain_toad::ui::Instance* goal = layout_->part("InfoGoalStar")) {
            goal->start("SetEpisode_EPISODE" + std::to_string(std::min(e.season, 3)), "Color");
            goal->hold("SetGoalIcon_NORMAL", "", 0);
            goal->hold(info_.cleared ? "SetGoalIcon_ON" : "SetGoalIcon_OFF", "OnOff", 0);
        }
        if (captain_toad::ui::Instance* items = layout_->part("InfoItems")) {
            for (int g = 0; g < 3; g++) {
                items->writeVisible("gem0" + std::to_string(g + 1), (info_.gems >> g) & 1);
            }
        }
        if (captain_toad::ui::Instance* challenge = layout_->part("InfoChallenge")) {
            challenge->hold("SetCourseCleared_NO", "", 0);
        }
        layout_->writeVisible("img_bonus", false);
        return;
    }
    if (captain_toad::ui::Instance* t = layout_->part("InfoLevelThumbnail")) {
        const int tex = pkg_.packTexture("Screenshots/" + e.stage + "_L");
        if (tex >= 0) {
            t->setTexture("img_Thumbnail_L", tex);
            t->setTexture("img_Thumbnail_R", tex);
        }
    }
}

// The NEW overlay held at the end of its intro, with its loop (0x2b8e3c + Page_NEW_IDLE).
void Page::holdNewOverlay() {
    if (captain_toad::ui::Instance* l = layout_) {
        const auto& anims = l->def().animations;
        auto it = anims.find("Page_NEW_INTRO");
        if (it != anims.end()) {
            hold("Page_NEW_INTRO", "PageNew", it->second.frames);
        }
    }
    start("Page_NEW_IDLE", "PageNew_Loop");
}

// 0x2aaae4 (course right page): the NEW overlay and the thumbnail's stamp; an unopened course hides
// its records, stamp, medal and thumbnail button, an opened one shows them. The roots of the
// course-complete stamp and the pixel-toad medal follow the save record either way, so a stacked
// unopened page shows neither. (The early returns for a page without a course id, or of a course of
// type 0xd, do not occur for course pages in this release's StageList.)
void Page::refreshRight() {
    if (info_.kind == PageKind::Illustration) {
        refreshChapter();
        return;
    }
    if (!thumbnail_) {
        return;
    }
    // A newly opened course shows the NEW overlay at the end of its intro, unless it shows already
    // (0x2aa998 on PageNewOverlay).
    if (info_.opened && !info_.cleared && layout_->paneVisible("PageNewOverlay") == false) {
        const auto& anims = layout_->def().animations;
        if (auto it = anims.find("Page_NEW_INTRO"); it != anims.end()) {
            hold("Page_NEW_INTRO", "PageNew", it->second.frames);
        }
    }
    thumbnail_->stamp(info_.fresh, info_.cleared);
    if (!info_.opened) {
        records_->hide();
        complete_->hide();
        if (medal_) {
            medal_->hide();
        }
        thumbnail_->hide();
    } else {
        thumbnail_->focus();
        records_->show();
        complete_->show();
        if (medal_) {
            medal_->show();
        }
    }
    complete_->setRoot(info_.complete);
    if (medal_) {
        medal_->setRoot(info_.medal);
    }
    // The records (0x2aa6a8 / 0x2aa6d0 / 0x2aa7b0): hidden ("Master" off) until the course has
    // been cleared, then the "played" texts. The badge for beating the challenge time
    // (img_ChallengeTime) shows when the best time is within it; best times are not kept yet, so
    // it stays off. (Courses of the other record kind, 0x2b49ec, show SetText_MUMMY-ME and the
    // best floor instead; not distinguished yet: hypothesis that no season-1 course uses it.)
    if (!info_.cleared) {
        records_->showPane("Master", false);
    } else {
        records_->start("SetText_PLAYED", "Text");
        records_->showPane("Master", true);
    }
    records_->showPane("img_ChallengeTime", false);
}

// 0x29ac30 (illustration right page): an opened spread shows its chapter title (Wait). The crown
// shows only on an unopened page under a set of conditions (0x29e8a0: the course of type 4 and
// further checks) that season-1 progress never meets; it stays hidden here, with its InfoCrown
// pane off. An illustrated cutscene (type 3) that is opened and not cleared holds the NEW overlay
// at the end of its intro unless it shows already.
void Page::refreshChapter() {
    if (!chapter_) {
        return;
    }
    if (info_.opened) {
        chapter_->show();
    }
    crown_->hide();
    showPane("InfoCrown", false);
    if (info_.cutscene && info_.opened && !info_.cleared && !layout_->paneVisible("PageNewOverlay")) {
        const auto& anims = layout_->def().animations;
        if (auto it = anims.find("Page_NEW_INTRO"); it != anims.end()) {
            hold("Page_NEW_INTRO", "PageNew", it->second.frames);
        }
    }
}

// 0x2af500 (course left page): an opened, uncleared course holds the NEW overlay at the end of its
// intro and loops Page_NEW_IDLE; an opened course shows its name, goal star, gems and challenge,
// an unopened one hides them.
void Page::refreshLeft() {
    if (!levelName_) {
        return;
    }
    if (info_.opened && !info_.cleared) {
        holdNewOverlay();
    }
    // The goal star's "OnOff" pane and the challenge's "Cleared" pane follow bit 1; the
    // challenge's "OnOff" follows bit 5 (the challenge done: not ported, never). The panes have a
    // Japanese variant (…_JP) chosen by the region (0x10a560); this release uses the other one
    // (hypothesis: the European ROM is not the Japanese region).
    goal_->showPane("OnOff", info_.cleared);
    challenge_->showPane("Cleared_NA", info_.cleared);
    challenge_->showPane("OnOff", false);
    if (!info_.opened) {
        levelName_->hide();
        goal_->hide();
        gemsPart_->hide();
        challenge_->hide();
        return;
    }
    levelName_->show();
    goal_->show();
    gemsPart_->show();
    challenge_->show();
}

// Appear (vtable +8): course right 0x1d204c, course left 0x1c3f84, illustration right 0x217378,
// illustration left 0x210a8c. Every page starts hidden.
void Page::appear() {
    appearLayout();
    for (LayoutActor* p : parts()) {
        p->appearPart();
    }
    if (info_.kind == PageKind::Illustration && side_ == Side::Right) {
        // 0x217378: the chapter's "OnOff" pane only on the page of Chapter4_1.
        if (chapter_) {
            chapter_->showPane("OnOff", info_.entry->stage == "Chapter4_1");
        }
        if (!beyond) {
            start("HideArrow", "Arrow00");
        }
        start("SetPageType_CHAPTER", "SetPageType");
        refreshChapter();
    } else if (info_.kind == PageKind::Illustration) {
        start("SetPageType_CHAPTER", "SetPageType");
        if (!beyond) {
            start("SetPageCover_ON", "ShowBookCover");
        }
    } else if (side_ == Side::Right) {
        start("SetPageType_NORMAL", "SetPageType");
        if (!beyond) {
            start("SetPageCover_ON", "ShowBookCover");
        }
        refreshRight();
    } else {
        if (!beyond) {
            start("Page_DUMMY", "Common");
        }
        start("SetPageType_NORMAL", "SetPageType");
        refreshLeft();
    }
    nerve_.set(&Page::hidden);
}

// A page turn's animation (bound through a prepared handle, 0x2afbe4: independent of the slots).
void Page::playTurn(const char* tag, float frame) {
    layout_->start(tag, std::string("#") + tag, frame);
    if (journal_.on()) {
        journal_.action(name_, tag, "", frame);
    }
}

void Page::arrive() {
    nerve_.set(&Page::arriving);
}

// +0x6c (course right 0x1d253c, course left 0x1c4454, illustration right 0x21773c, illustration
// left 0x210d94): the page under this one becomes current, then this page leaves.
void Page::leave() {
    if (beyond) {
        beyond->showCurrent();
    }
    if (side_ == Side::Right && thumbnail_) {
        thumbnail_->hide();   // +0x98 0x1d1ee8
    }
    nerve_.set(&Page::leaving);
}

void Page::showCurrent() {
    if (side_ == Side::Right && info_.kind != PageKind::Illustration) {
        nerve_.set(&Page::current);
    } else {
        nerve_.set(&Page::restState);
    }
}

void Page::rest() {
    if (side_ == Side::Right && info_.kind != PageKind::Illustration) {
        nerve_.set(&Page::restState);
    } else {
        showCurrent();
    }
}

void Page::end() {
    nerve_.set(&Page::endState);
}

void Page::hideThumbnail() {
    if (thumbnail_) {
        thumbnail_->hide();   // 0x1d1894 -> 0x431688
    }
}

// 0x2f8ee8 (course spread): the A icon goes (0x2aaa70 -> 0x412f84).
void Page::unfocus() {
    if (icon_ && icon_->shown()) {
        icon_->hideIcon();
    }
}

// End: course right 0x429520 and illustration right 0x436154 play End; the left pages (0x426748,
// 0x434b58) also hide their root.
void Page::endState(int step) {
    if (step == 0) {
        start("End");
        if (side_ == Side::Left) {
            showPane("root", false);
        }
    }
}

void Page::toStack() {
    nerve_.set(&Page::stack);
}

void Page::hide() {
    nerve_.set(&Page::hidden);
}

bool Page::turning() const {
    return nerve_.is(&Page::leaving) || nerve_.is(&Page::arriving);
}

bool Page::settled() const {
    return nerve_.is(&Page::restState) || nerve_.is(&Page::stack) || nerve_.is(&Page::hidden);
}

bool Page::opening() const {
    return nerve_.is(&Page::newIntro);
}

// +0x9c: course right pages loop the NEW frame and "Loop" (0x1d1f78), course left pages the NEW
// frame (0x1c3af0); illustrated cutscenes: the right page both (0x2172e0), the left page "Loop"
// (0x210a24); other illustrations nothing.
void Page::startNewLoop() {
    if (info_.kind == PageKind::Illustration) {
        if (!info_.cutscene) {
            return;
        }
        if (side_ == Side::Right) {
            start("Page_NEW_IDLE", "PageNew_Loop");
        }
        start("Loop", "Loop");
        return;
    }
    start("Page_NEW_IDLE", "PageNew_Loop");
    if (side_ == Side::Right) {
        start("Loop", "Loop");
    }
}

// 0x17c028: the spread's pages show that their course opened.
void Page::startCourseOpen() {
    afterOpen_ = nerve_.next ? nerve_.next : nerve_.current;
    nerve_.set(&Page::newIntro);
}

void Page::lockForGems() {
    if (thumbnail_) {
        thumbnail_->lock();
    }
}

// 0x17bdec: with this spread open and the book taking input, the course's button is focused and
// its A icon comes in (once).
bool Page::focus() {
    if (side_ != Side::Right || info_.kind == PageKind::Illustration || !nerve_.is(&Page::restState)) {
        return false;
    }
    if (info_.opened) {
        if (thumbnail_) {
            thumbnail_->focus();
        }
        if (icon_ && !icon_->shown()) {
            icon_->show();
            playSound("AIconAppear");   // 0x17bdec
        }
    }
    return true;
}

// 0x17bda4 (course spread): the thumbnail's press and the A icon's.
void Page::choose() {
    if (thumbnail_) {
        thumbnail_->press();
    }
    if (icon_) {
        icon_->press();
    }
}

bool Page::decided() const {
    return thumbnail_ ? thumbnail_->decided() : true;
}

// ---- States -------------------------------------------------------------------------------------

// Hidden: 0x42955c / 0x42679c / 0x436190 (root off); the illustration left page also binds
// Page_DUMMY (0x434bac).
void Page::hidden(int step) {
    if (step == 0) {
        if (info_.kind == PageKind::Illustration && side_ == Side::Left) {
            start("Page_DUMMY");
        }
        showPane("root", false);
    }
}

// Current (course right page 0x4297c0): Page_IDLE; two steps later the content is refreshed
// (+0xa8) and the NEW mark set (+0xac), and the page rests.
void Page::current(int step) {
    if (step == 0) {
        start("Page_IDLE");
        if (thumbnail_) {
            thumbnail_->hide();   // +0xa4 0x1d1894
        }
    }
    if (step > 1) {
        refreshRight();
        if (thumbnail_) {
            thumbnail_->markNew();
        }
        nerve_.set(&Page::restState);
    }
}

// Rest: course right 0x429598 (Page_IDLE, root on), course left 0x4268dc (Page_IDLE), illustration
// right 0x436280 ("Wait", root on), illustration left 0x434cb4 (Page_IDLE).
void Page::restState(int step) {
    if (step != 0) {
        return;
    }
    if (info_.kind == PageKind::Illustration && side_ == Side::Right) {
        start("Wait");
        showPane("root", true);
        return;
    }
    start("Page_IDLE");
    if (side_ == Side::Right) {
        showPane("root", true);
    }
}

// Stack: course right 0x4296b0 (Page_DUMMY, root on, the cover on the last page), course left
// 0x426a58 (Page_DUMMY, the cover on the first), illustration right 0x4363d4 ("HideWait", root on,
// HideArrow on the last page), illustration left 0x434ddc (Page_DUMMY, the cover on the first).
void Page::stack(int step) {
    if (step != 0) {
        return;
    }
    if (info_.kind == PageKind::Illustration && side_ == Side::Right) {
        start("HideWait");
        showPane("root", true);
        if (!beyond) {
            start("HideArrow", "Arrow01");
        }
        return;
    }
    if (info_.kind == PageKind::Illustration) {
        start("Page_DUMMY");
    } else {
        playTurn("Page_DUMMY", 0);
    }
    if (side_ == Side::Right) {
        showPane("root", true);
    }
    if (!beyond) {
        start("SetPageCover_ON", "ShowBookCover");
    }
}

// Leave: course right 0x1d22f4 (Page_NEXT), course left 0x426918 (Page_PREVIOUS from frame 2),
// illustration right 0x4362e0 (Page_NEXT), illustration left 0x434cf0 (Page_PREVIOUS from frame
// 2). When it has ended the page on the other side comes down (`toward`), and this page hides.
void Page::leaving(int step) {
    const char* tag = side_ == Side::Right ? "Page_NEXT" : "Page_PREVIOUS";
    const float frame = side_ == Side::Left ? 2 : 0;
    if (step == 0) {
        showPane("root", true);
        if (info_.kind == PageKind::Illustration && side_ == Side::Left) {
            start(tag, "", frame);
        } else {
            playTurn(tag, frame);
        }
    }
    if (ended(tag)) {
        if (side_ == Side::Right) {
            showPane("root", false);
        }
        if (toward) {
            toward->arrive();
        }
        nerve_.set(&Page::hidden);
    }
}

// Arrive: course right 0x1d219c (Page_PREVIOUS), course left 0x4267d8 (Page_NEXT), illustration
// right 0x4361cc (Page_PREVIOUS), illustration left 0x434c0c (Page_NEXT). When it has ended the
// covered page goes to the stack (`beyond`).
void Page::arriving(int step) {
    const char* tag = side_ == Side::Right ? "Page_PREVIOUS" : "Page_NEXT";
    if (step == 0) {
        showPane("root", true);
        if (info_.kind == PageKind::Illustration && side_ == Side::Left) {
            start(tag);
        } else {
            playTurn(tag, 0);
        }
        if (side_ == Side::Right) {
            refreshRight();   // +0xa8
        }
    }
    if (ended(tag)) {
        if (beyond) {
            beyond->toStack();
        }
        if (side_ == Side::Right && info_.kind != PageKind::Illustration) {
            if (thumbnail_) {
                thumbnail_->markNew();   // 0x2ab294
            }
            nerve_.set(&Page::arrived);
        } else {
            nerve_.set(&Page::restState);
        }
    }
}

// 0x429ac4: Page_IDLE with the root on, then at rest.
void Page::arrived(int step) {
    if (step == 0) {
        start("Page_IDLE");
        showPane("root", true);
    }
    nerve_.set(&Page::restState);
}

// Course-open (course right 0x429a4c, course left 0x426c30): Page_NEW_INTRO in the PageNew group;
// when it has ended the content shows the opened course and the page returns to its state.
void Page::newIntro(int step) {
    if (step == 0) {
        start("Page_NEW_INTRO", "PageNew");
    }
    if (ended("Page_NEW_INTRO")) {
        info_.opened = true;
        info_.fresh = true;
        side_ == Side::Right ? refreshRight() : refreshLeft();
        nerve_.set(afterOpen_ ? afterOpen_ : &Page::restState);
    }
}

}   // namespace book
