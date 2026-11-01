// =============================================================================================
// FrontEnd.hpp - the course book: choosing a course on both screens
// =============================================================================================
//
// After the opening's title logo the game shows its course book: the book on the top screen in
// perspective (UI screen "CourseBook", CourseBook_TOP, field of view 30) and the touch screen
// ("CourseSelect": CourseBook_BOTTOM, "ButtonBack"). The book itself is the recovered scene in
// session/book (BookScene and its actors); this class connects it to the game: it builds the
// scene from the course list and the player's progress, feeds it input and draws it, and plays
// the wipes around it (the opening wipe before the first book, the course's title card after a
// course has been chosen).
//
// The wipes are recovered actors (FrontEnd.cpp, .analysis/docs/frontend.md): each appears, waits
// until the wipe manager counts it closed, and ends when the next scene has started. The only
// measured value is the original's loading time between the title and the book, kept as a pause.
// =============================================================================================
#pragma once
#include "importer/Stages.hpp"
#include "render/UiLayout.hpp"
#include "session/Progress.hpp"
#include "session/Session.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <cstdio>
#include <vector>

namespace book {
class BookScene;
class Sound;
}   // namespace book

namespace captain_toad::session {

// Where the two screens go in a rectangle (pixels, origin bottom left): the 400 x 240 top screen
// above the 320 x 240 touch screen, centred, scaled uniformly (the console's arrangement).
struct ScreenRects {
    int top[4];   // x, y, w, h
    int bottom[4];
    static ScreenRects fit(int x, int y, int w, int h, bool withBottom);
};

class FrontEnd {
public:
    // `afterOpening`: the book follows the opening's title logo, through the opening wipe (the
    // cover on black, the curtain sliding in) as in the original's first start.
    FrontEnd(const std::filesystem::path& dataDirectory, const std::filesystem::path& cacheDirectory,
             Progress& progress, bool afterOpening = false);
    ~FrontEnd();

    // Advances the 30 Hz scene by `seconds` of real time.
    void advance(double seconds, const Controls& controls);
    void render(int x, int y, int w, int h);

    // Set once a course has been chosen and its title card shown: the stage to start.
    const captain_toad::CourseEntry* chosen() const { return chosen_; }
    // The course number shown on the title card ("1" for the first course of a season).
    int courseNumber(const captain_toad::CourseEntry& e) const;
    bool quitRequested() const { return quit_; }

    // Touch input in touch-screen pixels (0..320, 0..240, y down).
    void touch(bool down, float x, float y);

    // Once chosen(): hands the title card (WipeStart_TOP, at Wait) and its package to the course,
    // which ends it.
    std::unique_ptr<captain_toad::ui::Package> takeTitleCard(std::unique_ptr<captain_toad::ui::Instance>& top);

    // Audio: true when the front end's sound package loaded; fillAudio() renders interleaved stereo
    // 16-bit frames at the DSP rate (captain_toad::audio::kOutputRate), from the audio thread.
    bool hasSound() const { return sound_ != nullptr; }
    void fillAudio(int16_t* stereo, int frames);

private:
    void tick(const Controls& c);
    static std::string startVoice(const captain_toad::CourseEntry& e);
    void coverTick();
    void cardTick();

    // Wipe manager thresholds (Wait steps before a wipe counts as closed).
    static constexpr int kOpeningWaitSteps = 0x3b;   // 0x2bc724
    static constexpr int kCardWaitSteps = 0x95;      // 0x2bc75c
    // Measured, not game data: the title scene's unload and the book's load (front-nerves-a: the
    // wipe is seen closed at call 1053, the book scene starts at 1166). 55 ticks put the book's
    // first tick on the traced call (book-msg-input-b: every book action on its traced tick).
    static constexpr int kBookLoadTicks = 55;
    enum class Wipe { Appear, Wait, Loading, End };
    Wipe coverPhase_ = Wipe::End, card_ = Wipe::End;
    int wipeStep_ = 0;
    int coverBook_ = 0;
    int loaderStep_ =
        -1;   // ticks since the book's ChosenDone   // the cover in the wipe: 0 DemoRotate, 1 ended, 2 Wait

    std::filesystem::path dataDirectory_;
    Progress& progress_;
    std::vector<captain_toad::CourseEntry> courses_;
    std::vector<const captain_toad::CourseEntry*> spreadEntries_;
    std::unique_ptr<captain_toad::ui::Package> book_, wipes_;
    std::unique_ptr<book::BookScene> scene_;
    std::unique_ptr<captain_toad::ui::Instance> cardTop_, cardBottom_, cover_, fadeTop_, fadeBottom_;
    enum class State { Cover, Book, Card, Fade, Done } state_ = State::Book;
    int renders_ = 0;
    std::unique_ptr<book::Sound> sound_;
    std::vector<int16_t> audioPending_;
    size_t audioAt_ = 0;
    FILE* soundOut_ = nullptr;   // research: DIORAMA_SOUND_OUT
    double soundClock_ = 0;
    double clock_ = 0;
    bool touchDown_ = false;
    float touchPos_[2] = {0, 0};
    const captain_toad::CourseEntry* chosen_ = nullptr;
    bool quit_ = false;
};

}   // namespace captain_toad::session
