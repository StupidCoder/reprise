// See FrontEnd.hpp.
#include "session/FrontEnd.hpp"

#include "gpu/Device.hpp"

#include "game/Camera.hpp"
#include "importer/UiExport.hpp"
#include "importer/SoundExport.hpp"
#include "session/book/BookScene.hpp"
#include "session/book/BookSound.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace captain_toad::session {

namespace {

std::u16string number(int n) {
    std::u16string s;
    for (char c : std::to_string(n)) {
        s += char16_t(c);
    }
    return s;
}

// The page class a StageList type makes (0x1b5268 with the type queries 0x2b89c8, 0x2b8448,
// 0x2b81e0, 0x2b85f0, 0x2b8404 and the type table 0x4f6cdc); other types make no page.
bool pageKind(const std::string& type, book::PageKind& kind, bool& cutscene) {
    cutscene = false;
    if (type == "通常") {
        kind = book::PageKind::Course;
    } else if (type == "挿絵" || type == "挿絵デモ") {
        kind = book::PageKind::Illustration;
        cutscene = type == "挿絵デモ";
    } else if (type == "特殊(ボーナス)") {
        kind = book::PageKind::Bonus;
    } else if (type.rfind("特殊(", 0) == 0) {
        kind = book::PageKind::Special;
    } else {
        return false;
    }
    return true;
}

// 0x2df3a0: a season's first course (the first entry of the season that is not an illustration,
// a bonus, a minigame or trick art: types 4, 1, 2 and 12; the season's prologue).
const captain_toad::CourseEntry* seasonStart(const std::vector<captain_toad::CourseEntry>& courses, int season) {
    for (const auto& e : courses) {
        if (e.season == season && e.type != "挿絵" && e.type != "ボーナス" && e.type != "ミニゲーム" &&
            e.type != "トリックアート") {
            return &e;
        }
    }
    return nullptr;
}

}   // namespace

ScreenRects ScreenRects::fit(int x, int y, int w, int h, bool withBottom) {
    ScreenRects r{};
    const double cw = 400, ch = withBottom ? 480 : 240;
    const double s = std::min(w / cw, h / ch);
    const int tw = int(400 * s), th = int(240 * s);
    const int ox = x + (w - int(cw * s)) / 2, oy = y + (h - int(ch * s)) / 2;
    r.top[0] = ox;
    r.top[1] = oy + (withBottom ? int(240 * s) : 0);
    r.top[2] = tw;
    r.top[3] = th;
    r.bottom[0] = ox + int(40 * s);
    r.bottom[1] = oy;
    r.bottom[2] = int(320 * s);
    r.bottom[3] = int(240 * s);
    return r;
}

FrontEnd::FrontEnd(const std::filesystem::path& dataDirectory, Progress& progress, bool afterOpening)
    : dataDirectory_(dataDirectory), progress_(progress) {
    courses_ = captain_toad::courseList(dataDirectory);
    book_ = std::make_unique<captain_toad::ui::Package>(captain_toad::buildUi(dataDirectory, "CourseSelection"));
    wipes_ = std::make_unique<captain_toad::ui::Package>(captain_toad::buildUi(dataDirectory, "Wipes"));
    if (!std::getenv("DIORAMA_NO_SOUND")) {
        sound_ = std::make_unique<book::Sound>(captain_toad::buildFrontEndSound(dataDirectory));
        if (const char* out = std::getenv("DIORAMA_SOUND_OUT")) {
            soundOut_ = fopen(out, "wb");
        }
    }

    // The scene from the course list and the save state.
    book::BookSetup setup;
    setup.sound = sound_.get();
    setup.season = progress_.season;
    setup.lives = progress_.lives;
    for (const auto& [stage, bits] : progress_.gems) {
        setup.gems += (bits & 1) + ((bits >> 1) & 1) + ((bits >> 2) & 1);
    }
    const captain_toad::CourseEntry* last = nullptr;
    // The last played course by name: the first entry (a stage can be listed again later, e.g. a
    // season's opening as a special prologue).
    for (const auto& e : courses_) {
        if (!last && e.stage == progress_.lastPlayed) {
            last = &e;
        }
    }
    for (const auto& e : courses_) {
        if (e.season != progress_.season) {
            continue;
        }
        book::PageInfo info;
        if (!pageKind(e.type, info.kind, info.cutscene)) {
            continue;
        }
        const bool open = progress_.open(courses_, e);
        // A page opened by a clear but not shown yet opens with the course-open sequence;
        // illustrations open silently (0x20d5e0). Progress files from before "revealed" count every
        // open page as shown.
        if (open && !progress_.hasRevealed) {
            progress_.revealed.insert(e.stage);
        }
        if (open && info.kind == book::PageKind::Illustration && !info.cutscene) {
            progress_.revealed.insert(e.stage);
        }
        const bool revealed = progress_.revealed.count(e.stage) != 0;
        if (open && !revealed) {
            setup.pendingOpen.push_back(e.courseId);
        }
        info.entry = &e;
        info.courseNumber = courseNumber(e);
        info.opened = open && revealed;
        info.cleared = progress_.cleared.count(e.stage) != 0;
        info.fresh = info.opened && !info.cleared && !progress_.played.count(e.stage);
        auto g = progress_.gems.find(e.stage);
        info.gems = g == progress_.gems.end() ? 0 : g->second;
        info.gemLocked = e.collectLockNum > 0 && !info.cleared;
        if (info.gemLocked) {
            setup.gemLocked.push_back(e.courseId);
        }
        if (last == &e) {
            setup.lastPlayedSpread = int(setup.spreads.size());
        }
        spreadEntries_.push_back(&e);
        setup.spreads.push_back(info);
    }
    progress_.hasRevealed = true;
    setup.returnedFromClear = progress_.returnedFromClear;
    // 0x2facbc: after a course other than the first course of seasons 1-3, the book shows itself
    // open at once; otherwise it appears and opens.
    bool fromSeasonStart = false;
    for (int s = 1; s <= 3; s++) {
        fromSeasonStart = fromSeasonStart || (last && last == seasonStart(courses_, s));
    }
    setup.animatedOpen = !(progress_.returnedFromStage && last && !fromSeasonStart);
    progress_.returnedFromClear = false;
    progress_.returnedFromStage = false;

    const char* journal = std::getenv("DIORAMA_BOOK_JOURNAL");
    scene_ = std::make_unique<book::BookScene>(*book_, std::move(setup), journal);
    scene_->onReveal = [this](int courseId) {
        for (const auto& e : courses_) {
            if (e.courseId == courseId) {
                progress_.revealed.insert(e.stage);
            }
        }
    };
    if (afterOpening) {
        // The opening wipe (front-cover-a call 831): the cover's demo turn, the curtain. The
        // book's scene starts when the wipe ends.
        const std::string season = "Season" + std::to_string(std::min(progress_.season, 3));
        cover_ = std::make_unique<captain_toad::ui::Instance>(*wipes_, "WipeOpening_TOP");
        cover_->startAction(season);
        cover_->startAction("AppearOpening");
        if (captain_toad::ui::Instance* book = cover_->part("ParBook")) {
            book->startAction(season);
            book->startAction("DemoRotate");
        }
        scene_->wipeDone = [this] {
            return !cover_ || cover_->ended("End");
        };
        state_ = State::Cover;
        coverPhase_ = Wipe::Appear;
    } else {
        scene_->start();
    }
}

FrontEnd::~FrontEnd() {
    if (soundOut_) {
        fclose(soundOut_);
    }
}

int FrontEnd::courseNumber(const captain_toad::CourseEntry& e) const {
    int n = 0;
    for (const auto& c : courses_) {
        if (c.season == e.season && c.type == "通常") {
            ++n;
        }
        if (&c == &e) {
            break;
        }
    }
    return n;
}

void FrontEnd::touch(bool down, float x, float y) {
    touchDown_ = down;
    touchPos_[0] = x;
    touchPos_[1] = y;
}

void FrontEnd::advance(double seconds, const Controls& controls) {
    clock_ += seconds;
    const double step = 1.0 / 30;
    int n = 0;
    while (clock_ >= step - 1e-9 && n < 4) {
        clock_ -= step;
        tick(controls);
        ++n;
    }
    if (clock_ > step) {
        clock_ = 0;
    }
}

void FrontEnd::tick(const Controls& c) {
    // The wipe's layout pass comes before its state update (front-cover-a: AppearOpening's end,
    // 51 passes, is seen at call 933, 51 ticks after the appear).
    if (cover_) {
        cover_->calc();
    }
    using namespace captain_toad::RecoveredCamera;
    book::BookInput in;
    in.left = (c.buttons & (kDLeft | kL)) || c.stick[0] < -0.5f;
    in.right = (c.buttons & (kDRight | kR)) || c.stick[0] > 0.5f;
    in.a = c.buttons & kA;
    in.b = c.buttons & kB;
    in.touch = touchDown_;
    in.touchX = touchPos_[0];
    in.touchY = touchPos_[1];

    switch (state_) {
    case State::Cover:
        coverTick();
        break;
    case State::Book:
        if (scene_->backRequested() && !fadeTop_) {
            // Back to the title: the scene change's fade (wipe manager message 0x3d -> 0x338620:
            // WipeFadeBlack "Appear" at its own pace, 0x2f8a5c with -1); the title follows. The
            // port returns to its launcher once the screens are black.
            fadeTop_ = std::make_unique<captain_toad::ui::Instance>(*wipes_, "WipeFadeBlack_TOP");
            fadeBottom_ = std::make_unique<captain_toad::ui::Instance>(*wipes_, "WipeFadeBlack_BOTTOM");
            fadeTop_->startAction("Appear");
            fadeBottom_->startAction("Appear");
            state_ = State::Fade;
        }
        if (loaderStep_ >= 0 && ++loaderStep_ == 2) {
            // Two ticks after the book's ChosenDone the loader shows the title card (WipeStart,
            // appear 0x3df4cc; card-msg-a: ChosenDone and the loader at call 802, the card at
            // 806). Its bottom screen is WipeBottom_BOTTOM.
            const auto& e = *spreadEntries_[size_t(scene_->chosenSpread())];
            cardTop_ = std::make_unique<captain_toad::ui::Instance>(*wipes_, "WipeStart_TOP");
            cardBottom_ = std::make_unique<captain_toad::ui::Instance>(*wipes_, "WipeBottom_BOTTOM");
            cardTop_->startAction("Season" + std::to_string(std::min(e.season, 3)));
            cardTop_->startAction("Normal");
            cardTop_->startAction("Appear");
            cardTop_->setText("TxtStage", number(courseNumber(e)));
            cardTop_->setText("TxtCourseName_NA", book_->message("StageName", e.stage));
            cardTop_->setText("TxtMessage", u"");
            cardBottom_->startAction("Appear");
            if (sound_) {
                sound_->play("WipeStart", startVoice(e));   // the card's start voice (call 806)
            }
            card_ = Wipe::Appear;
            state_ = State::Card;
        }
        break;
    case State::Card:
        cardTick();
        break;
    case State::Fade:
        if (fadeTop_->ended("Appear")) {
            quit_ = true;
        }
        break;
    case State::Done:
        break;
    }
    if (state_ == State::Cover || state_ == State::Book) {
        scene_->tick(in);
        progress_.page = scene_->currentSpread();
        if (state_ == State::Book && scene_->chosen() && loaderStep_ < 0) {
            // ChosenDone: the scene's loader starts (0x2053f8) with "all players decided"
            // (ProductSequence PgAllPlayerDecided, SeSyDummy; call 802).
            loaderStep_ = 0;
            if (sound_) {
                sound_->play("ProductSequence", "PgAllPlayerDecided");
            }
        }
    }
    if (cardTop_) {
        cardTop_->calc();
        cardBottom_->calc();
    }
    if (fadeTop_) {
        fadeTop_->calc();
        fadeBottom_->calc();
    }
    if (cover_ && state_ == State::Book && cover_->ended("End")) {
        cover_.reset();   // 0x4171f8: the wipe dies when End has played
    }
    if (sound_) {
        sound_->tick();
        for (int i = 0; i < 2; i++) {   // the two 60 Hz frames of the tick
            sound_->frame();
            if (soundOut_) {
                // Research render: DSP frames in step with the frames (DIORAMA_SOUND_OUT).
                soundClock_ += captain_toad::audio::kOutputRate / 60.0 / captain_toad::audio::kFrameSamples;
                std::vector<int16_t> buf(captain_toad::audio::kFrameSamples * 2);
                while (soundClock_ >= 1) {
                    soundClock_ -= 1;
                    sound_->render(buf.data(), 1);
                    fwrite(buf.data(), 2, buf.size(), soundOut_);
                }
            }
        }
    }
}

// The audio thread pulls whole DSP frames.
void FrontEnd::fillAudio(int16_t* stereo, int frames) {
    if (!sound_) {
        std::fill(stereo, stereo + size_t(frames) * 2, int16_t(0));
        return;
    }
    for (int i = 0; i < frames; i++) {
        if (audioAt_ >= audioPending_.size()) {
            audioPending_.assign(captain_toad::audio::kFrameSamples * 2, 0);
            sound_->render(audioPending_.data(), 1);
            audioAt_ = 0;
        }
        stereo[i * 2] = audioPending_[audioAt_];
        stereo[i * 2 + 1] = audioPending_[audioAt_ + 1];
        audioAt_ += 2;
    }
}

// The title card's start voice (0x3df4cc): by the stage's name lists in the table at 0x489444 and
// the course's player (StageList Player; the companion's courses use her variants).
std::string FrontEnd::startVoice(const captain_toad::CourseEntry& e) {
    static const char* const ghost[] = {"GhostPlayerMaze1Stage",      "GhostPlayerMaze2Stage",
                                        "GhostPlayerPatanPanelStage", "GhostPlayerChikuwaFallStage",
                                        "GhostPlayerBossFireStage",   "GhostPlayerMaze3Stage"};
    static const char* const boss[] = {"BossFireStage",    "BossFire2Stage", "BossFire3Stage",    "BossBird1Stage",
                                       "EnemyBattleStage", "BossBird2Stage", "TrialBossFireStage"};
    static const char* const brigade[] = {"ToadBrigadeFreshRuinsStage", "ToadBrigadeTouchBlindNeedleBallStage",
                                          "ToadBrigadeTouchRotateFortressStage", "ToadBrigadeClimbTowerStage"};
    auto in = [&](const auto& list) {
        return std::any_of(std::begin(list), std::end(list), [&](const char* s) { return e.stage == s; });
    };
    if (in(ghost)) {
        return "PgStartVoiceForManemyStage";
    }
    const bool companion = e.player == "キノピコ";   // StageList +0x14 == 1 (0x3e4c8c)
    if (in(boss)) {
        return companion ? "PgStartVoiceForKinopicoBossStage" : "PgStartVoiceForKinopioBossStage";
    }
    if (!companion && in(brigade)) {
        return "PgStartVoiceForKinopioBrigadeStage";
    }
    return companion ? "PgStartVoiceForKinopicoNormalStage" : "PgStartVoiceForKinopioNormalStage";
}

// The opening wipe (WipeOpening actor: appear 0x162154, Appear 0x4172a8, Wait 0x41726c, End
// 0x4171f8) with the book cover in it (CourseBookParts: DemoRotate 0x41e540 -> Wait 0x41e330). The
// wipe manager counts it closed after 60 Wait steps (0x2bc724, step > 0x3b); the title scene then
// changes to the book: the title is unloaded and the book loaded (front-nerves-a: closed at call
// 1053, the book scene starts and the wipe gets End at 1166). The port loads at once; the
// original's loading time (kBookLoadTicks, measured) is kept as a pause, as for the other loading
// pauses.
void FrontEnd::coverTick() {
    // A state set by a state runs from the next tick (Nerve.hpp): the actions of Wait come one tick
    // after the end of the appear was seen (front-cover-a: end seen at call 933, Wait at 935).
    switch (coverPhase_) {
    case Wipe::Appear:
        if (cover_->ended("AppearOpening")) {   // 0x4172a8: isActionEnd of the last action
            coverPhase_ = Wipe::Wait;
            wipeStep_ = -1;
        }
        break;
    case Wipe::Wait:
        if (++wipeStep_ == 0) {
            cover_->startAction("Wait");   // 0x41726c
        }
        if (wipeStep_ > kOpeningWaitSteps) {
            coverPhase_ = Wipe::Loading;
            wipeStep_ = 0;
        }
        break;
    case Wipe::Loading:
        if (++wipeStep_ >= kBookLoadTicks) {
            cover_->startAction("End");   // 0x4171f8
            coverPhase_ = Wipe::End;
            scene_->start();
            state_ = State::Book;
        }
        break;
    default:
        break;
    }
    // The cover in the wipe: DemoRotate, then Wait from the next tick (0x41e540 -> 0x41e330).
    if (captain_toad::ui::Instance* book = cover_->part("ParBook")) {
        if (coverBook_ == 1) {
            book->startAction("Wait");
            coverBook_ = 2;
        }
        if (coverBook_ == 0 && book->ended("DemoRotate")) {
            coverBook_ = 1;
        }
    }
}

// The title card (WipeStart actor: appear 0x3df4cc, Appear 0x4118b0 -> Wait 0x4118ec). The wipe
// manager counts it closed after 150 Wait steps (0x2bc75c: step > 0x95; > 0xd1 when the save's
// +0x4f flag is set, an amiibo feature not ported); the course then starts (the stage was loaded
// meanwhile, front-input-a: End1 at call 1144). The card goes to the course's screen, which ends
// it (CourseUi).
void FrontEnd::cardTick() {
    switch (card_) {
    case Wipe::Appear:
        if (cardTop_->ended("Appear")) {
            card_ = Wipe::Wait;
            wipeStep_ = -1;
        }
        break;
    case Wipe::Wait:
        if (++wipeStep_ == 0) {
            cardTop_->startAction("Wait");
        }
        if (wipeStep_ > kCardWaitSteps) {
            chosen_ = spreadEntries_[size_t(scene_->chosenSpread())];
            card_ = Wipe::End;
            state_ = State::Done;
        }
        break;
    default:
        break;
    }
}

std::unique_ptr<captain_toad::ui::Package> FrontEnd::takeTitleCard(std::unique_ptr<captain_toad::ui::Instance>& top) {
    top = std::move(cardTop_);
    cardBottom_.reset();
    cover_.reset();
    return std::move(wipes_);
}

void FrontEnd::render(int x, int y, int w, int h) {
    // Research: DIORAMA_DUMP_TICK=N dumps the layouts' panes (DIORAMA_UI_DEBUG) at render N / 2.
    static const char* dump = std::getenv("DIORAMA_DUMP_TICK");
    const bool dumping = dump && renders_++ == std::atoi(dump) * 2;
    if (dumping) {
        setenv("DIORAMA_UI_DEBUG", "1", 1);
    }
    struct Restore {
        bool on;
        ~Restore() {
            if (on) {
                unsetenv("DIORAMA_UI_DEBUG");
            }
        }
    } restore{dumping};
    const bool bottom = !cardTop_ && state_ != State::Cover;
    const ScreenRects r = ScreenRects::fit(x, y, w, h, true);
    captain_toad::gpu::Device& device = captain_toad::gpu::device();
    captain_toad::gpu::Clear clear;
    clear.color = clear.depth = true;
    device.clear(clear);
    auto viewport = [&](const auto& rect) {
        device.setViewport({rect[0], rect[1], rect[2], rect[3]});
    };
    viewport(r.top);
    if (cardTop_) {
        cardTop_->drawScreen(0);
    } else {
        if (state_ != State::Cover) {
            scene_->drawTop(book_->screen("CourseBook") ? book_->screen("CourseBook")->topFov : 30);
        }
        if (cover_) {
            cover_->drawScreen(0);
        }
    }
    viewport(r.bottom);
    if (bottom) {
        scene_->drawBottom();
    } else if (cardBottom_) {
        cardBottom_->drawScreen(0);
    }
    if (fadeTop_) {
        viewport(r.top);
        fadeTop_->drawScreen(0);
        viewport(r.bottom);
        fadeBottom_->drawScreen(0);
    }
}

}   // namespace captain_toad::session
