#pragma once
// The third adventure's loops (see a3_story.hpp for how they are
// stacked). Each is the native part of one of the game's lt3CLoop classes;
// what happens inside is the game's own scripts.
#include "story/a3_story.hpp"
#include <optional>

namespace picarat {
class MovieFrames;
}

namespace picarat::adventure3 {

// GAME (overlay 12, lt3CLoopGame): the bottom of the stack. Starting, it
// resets the game's data and asks for the title with the logos on top
// (0211f740); when it is uncovered with nothing else to do it stops the
// music, ends and - as its arguments ask - starts over (0211fc00).
class GameLoop : public Loop {
public:
    GameLoop(Story& story, LoopArgs args) : Loop(story, "GAME", args) {}

    void start() override;
    void frame(const lscr::SceneInput& input) override;
};

// A script loop (overlay 14, lt3CLoopScript): logos, title, bag, menus,
// credits... Each time it becomes the top loop it loads its program and runs
// its MAIN section from the start (0211db80, 0211dccc); when MAIN has ended
// it asks to be popped unless other requests are already waiting
// (0211dc84).
class ScriptLoop : public Loop {
public:
    ScriptLoop(Story& story, std::string name, LoopArgs args, std::string file)
        : Loop(story, std::move(name), args), file_(std::move(file)) {}

    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;

private:
    enum class State { Idle, Loading, Running, Finished };
    std::string file_;
    State state_ = State::Idle;
};

// An event (overlay 13, lt3CLoopEvent): evt/GG/GG_IIIIII's MAIN with the
// event's talk lines. Starting, it becomes the current event and chooses
// its program's variant - the request's second argument, or (when it is
// negative) the first whose conditions hold (020dab28 / 020dab40); when
// it is first run the variant's actions apply and MAIN starts at the
// variant's label; when MAIN ends the event pops itself (0211dde4) and,
// ending, applies the variant's closing actions (0211dd00). Each time it
// is uncovered it selects its sound set (unless イベントサウンドセット無効
// is set) and creates its talk window "lt3_msg_talk" (0211dd28).
class EventLoop : public Loop {
public:
    EventLoop(Story& story, LoopArgs args) : Loop(story, "EVENT", args) {}

    void start() override;
    void activate() override;
    void deactivate() override;
    void end() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;

private:
    const EventInfo* event_ = nullptr;
    const ProgramVariant* variant_ = nullptr;
    bool loading_ = false;
    bool popped_ = false;
};

// A place (overlay 15, lt3CLoopPlace): a room to look around. Created, it
// becomes the current place (0211db8c); whenever it is uncovered it starts
// afresh (0211dbb0) - unless a story recap is due (あらすじ起動: then the
// current phase's recap event comes first) - with the room's programs
// (rc/lt3_plc p%04d and rc/obj p%04d) and its examine texts
// ("lt3_msg_tobj", txt/?/30 part <place>). It runs PLACE_BEGIN, PLACE_LOOP
// until that ends or a request is made, then PLACE_END (0211ddbc); requests
// wait until PLACE_END is over (0211db34).
class PlaceLoop : public Loop {
public:
    PlaceLoop(Story& story, LoopArgs args) : Loop(story, "PLACE", args) {}

    void start() override;
    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;

    // What the room offers to touch while PLACE_LOOP waits for the pen
    // (TouchArea; the touch-area help).
    std::vector<TouchArea> touchAreas() const;

private:
    int state_ = 0; // 1 loading, 2 PLACE_BEGIN, 3 PLACE_LOOP, 4 PLACE_END, 5 over
};

// A movie (overlay 16, lt3CLoopMovie) {movie, fade in, fade out}: mov/
// m%03d.mods on the upper screen, its captions (txt/?/50 part <movie>:
// {frame, -, text}) on the lower one. Its script (evt/mov/%02d with
// lt3_movie) opens the film's sound and starts the picture by setting the
// thread variable 204 to 2; the loop then plays at the film's rate and,
// when it is over or skipped (a tap, when variable 206 allows it), fades out
// and pops itself (0211e374, 0211e73c, 0211e6b0, 0211e234, 0211e618).
class MovieLoop : public Loop {
public:
    MovieLoop(Story& story, LoopArgs args);
    ~MovieLoop() override;

    void start() override;
    void end() override;
    void frame(const lscr::SceneInput& input) override;
    Surface render() const override;
    // The host's skip (HostInput::skip): the film ends as START ends a
    // skippable one, whether or not the script lets START do so.
    void skip();

private:
    std::unique_ptr<picarat::MovieFrames> movie_;

    struct Caption {
        int frame = 0;
        std::string text;
    };

    std::vector<Caption> captions_;
    std::size_t nextCaption_ = 0;
    unsigned shown_ = 0;      // the frame on screen
    std::uint64_t clock_ = 0; // film frames x 60, in 8.24 (the alarm)
    bool playing_ = false;
};

// A puzzle (overlay 35, lt3CLoopNazo) {puzzle, ...}: the puzzle loop of the
// standalone puzzles - title card, engine, verdict, retries (0211e6c8) - on
// the story's game state and records, with the story's commands in its
// scenes. It becomes the current puzzle; found and solved go through the
// story (020ea4a4 -> 020e61b8: counters, picarats, items); when it is over
// it pops itself. (The fourth argument, which skips the title card and the
// verdict, is not used by the scripts and not reproduced.)
class NazoLoop : public Loop {
public:
    NazoLoop(Story& story, LoopArgs args);
    ~NazoLoop() override;

    void start() override;
    void frame(const lscr::SceneInput& input) override;
    Surface render() const override;
    bool busy() const override;
    lscr::Scene* scene() override;
    const lscr::Scene* scene() const override;

private:
    std::unique_ptr<PuzzleLoop> puzzle_;
    bool popped_ = false;
};

// The program a script loop runs, from the game's loop registry (overlay 6's
// static initialiser 020f4740: pairs of loop name and program file); none if
// `name` is not a script loop.
std::optional<std::string> scriptLoopFile(Assets& assets, const std::string& name);

// The story's other loops (events, places, movies, puzzles...); null for a
// name that is none of them.
std::unique_ptr<Loop> makeStoryLoop(Story& story, const std::string& name, const LoopArgs& args);

} // namespace picarat::adventure3
