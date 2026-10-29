#pragma once
// The fifth adventure as a whole: a stack of loops run by a list of
// requests, from the logos to the end credits.
//
// How the game is put together
// ----------------------------
// Everything the player sees is one loop at a time (the logos, the title,
// the name keyboard, an event, a place, a puzzle, a menu). Loops sit on a
// stack; the top one runs. Scripts ask for loops by number (2104; 10000 an
// event, 10001 a place, 10002 a puzzle) and end the top one (2102); the
// requests are carried out at the start of the next frame, in order:
//
//   push     the loop below is suspended (what it showed while on top goes)
//            and the new loop is made and started on top of it;
//   pop      the top loop is suspended and ends; the one below carries on;
//   restart  the top loop is suspended and resumed again.
//
// After the requests the loop now on top resumes, then begins: script
// loops run their script's "Init" on resuming and "Main" on beginning.
// Once begun, a loop ends itself when no request is waiting and neither of
// its two script threads runs (0x2ad24c).
//
// The bottom of the stack is the game loop (lt5CLoopGame): it loads the
// resident scripts and fonts, asks for the logos first, then for the title,
// and ends itself after that (an empty stack starts a new one).
//
// Reconstructed from the loop factory 0x2c592c (loop numbers: 1000 game,
// 1200 event, 1300 place, 2000 logos, 2010 title, 2030 name keyboard, 2150
// staff roll, 3000 puzzle, 6300 horse ride, 6400 ruins ...), the loop base
// class (0x2adf3c start, 0x2add74 end, 0x2add1c resume, 0x2ad308 suspend,
// 0x2adfa4 begin, 0x2ad24c end of frame), lt5CLoopGame (0x182788,
// 0x1829d8), the script loop (0x183148, 0x2ad12c, 0x2ad1f4) and the event
// loop (0x2d4c44, 0x1a72e4, 0x3393ac). See
// .analysis/adventure56/docs/story.md.
#include "import/a56_formats.hpp"
#include "system/a5_game.hpp"
#include "system/a5_saves.hpp"
#include "system/game_types.hpp"
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace picarat::adventure5 {

class Story;

// The loop numbers of the factory 0x2c592c.
enum LoopId : int {
    GameLoopId = 1000,
    EventLoopId = 1200,
    PlaceLoopId = 1300,
    LogoLoopId = 2000,
    TitleLoopId = 2010,
    NameLoopId = 2030,
    StaffRollLoopId = 2150,
    PuzzleLoopId = 3000,
};

class Loop;

// An event a loop plays (the event object at +0x18 of the event loop and of
// the ruins' event runner): 0x339478 / 0x3394b4 open it by number / name
// (current with its puzzle, 0x184bcc; its branch; its script
// evt/GG/GG_NNNNNN), 0x3394f0 enters it (current again, 稼働中のイベント,
// its sound set and texts "lt5_evt"), 0x3393d4 runs it (the branch's
// opening effects, then "Main" at the branch's label on a thread of the
// loop), 0x29e378 leaves it (稼働中のイベント cleared) and 0x29e3f4 closes
// it (the branch's closing effects).
class EventPlayback {
public:
    explicit EventPlayback(Game& game) : game_(game) {}

    // A negative branch picks one by the events' rules (0x33cbac).
    bool open(const EventRecord* event, int branch);
    void enter();
    // keep: a running thread is kept (+0x15c bit 0 clear, 0x3393d4).
    void run(Loop& loop, int thread, bool keep);
    void leave();
    void close();
    // While it plays: the event stays current (the event loop's update).
    void keepCurrent();

    const EventRecord* event() const { return event_; }

private:
    void makeCurrent();

    Game& game_;
    const EventRecord* event_ = nullptr;
    const Branch* branch_ = nullptr;
};

// A loop (lt5CLoop): its number, eight values and two script threads.
class Loop {
public:
    Loop(Story& story, int id, std::vector<xq::Value> values);
    virtual ~Loop();

    // vtable +0x28 (pushed): a scope of the loop's own (0x2adf3c).
    virtual void start();
    // +0x2c (popped): everything the loop made goes (0x2add74).
    virtual void end();
    // +0x30 (the frame driver 0x37a0e0, once in the loop's life, right
    // after the start): a script loop's "Init" (0x2ad130). It sets +0x18
    // bit 4 first (0x2ad2c8) and runs in the loop's own scope, as the
    // first resume waits for it.
    virtual void init() { initStarted = true; }
    // The manager (0x257230) resumes a loop only once its Init has started
    // and no longer runs (+0x18 check 0x347a94: 0x351cf0, +0x15c bit 1).
    virtual bool resumeWaits() const { return !initStarted; }
    // +0x34 (on top again): a second scope for what it shows there
    // (0x2add1c).
    virtual void resume();
    // +0x38 (covered): the on-top scope goes (0x2ad308).
    virtual void suspend();
    // +0x3c (on top and begun): sets "begun" (0x33698c).
    virtual void begin();
    // +0x20: whether begin must wait (a script loop's Init still runs,
    // 0x347a74); the frame driver 0x37a0e0 begins a resumed loop (0x351ca4)
    // only when it says no.
    virtual bool beginWaits() const { return false; }
    // One frame of the loop: its own update, then its scripts.
    virtual void update();
    // +0x54 (after the frame): once begun, the loop ends itself when no
    // request waits and its threads are idle (0x2ad24c).
    virtual void afterFrame();
    // A key typed on the computer's keyboard (only the name keyboard takes
    // them).
    virtual void keyboard(const NameInput& key) { (void)key; }
    // 2110 (vtable +0x4c): a command to the loop; false when it has none.
    virtual bool command(int number, xq::Call& c, std::size_t first);

    bool threadsRunning() const;
    // A block of the newest script on a thread of the loop (0x32b780).
    bool runBlock(int thread, const std::string& block, const std::string& label = {}) {
        return startBlock(thread, block, label);
    }

    int id = 0;
    std::array<xq::Value, 8> values{};
    std::array<std::unique_ptr<xq::Thread>, 2> threads;
    bool begun = false;
    bool initStarted = false; // +0x18 bit 4
    bool resumed = false;     // +0x18 bit 2
    int resumes = 0;
    std::uint64_t resumeSerial = 0; // which resume of the story's this is (Story::checkpoint)

protected:
    // Starts a block of the newest script on a thread (0x32b780); true
    // while it runs.
    bool startBlock(int thread, const std::string& block, const std::string& label = {});

    Story& story_;
    Game& game_;
    int level_ = -1;    // the scope depth below the loop's own (+0x20)
    int topLevel_ = -1; // the depth below its on-top scope (+0x22)
};

// lt5CLoopMenu (creator 0x379988): a loop run by a script (value 6 on: the
// script names, loaded as "seq/<name>" at the start, 0x183148). "Init"
// (vtable +0x30, 0x2ad130) runs once in the loop's life, before the first
// resume (see Loop::init): the frame driver 0x37a0e0 calls it while +0x18
// bit 4 is clear and Init sets that bit first (0x2ad2c8); nothing clears
// it, so a resume (+0x34, after an event or a restart) does not run it
// again, and what Init makes (prog_place's cameras) lives in the loop's
// own scope. Beginning starts "Main" afresh (0x2ad1f8); every resume
// clears "begun" (0x3369cc), so Main starts again.
class ScriptLoop : public Loop {
public:
    ScriptLoop(Story& story, int id, std::vector<xq::Value> values, std::string script)
        : Loop(story, id, std::move(values)), script_(std::move(script)) {}

    void start() override {
        Loop::start();
        if (!script_.empty()) {
            game_.loadScript("seq/" + script_);
        }
    }

    void init() override {
        Loop::init();
        initRunning_ = startBlock(0, "Init");
    }

    bool resumeWaits() const override { return Loop::resumeWaits() || initRunning_; }

    void begin() override {
        // 0x2ad1f8: a running thread 0 is kept only when +0x15c bit 0 is
        // clear; the constructor 0x2558ac takes that bit from its creator,
        // and every script loop's creator sets it (menus and the place pass
        // 1, the others get 0xff; only the event loop, not one of these,
        // passes 0). So Main starts afresh on every begin, which ends a Main
        // left waiting when the loop was covered (prog_place's
        // Place_ToEvent waits for good after pushing an event).
        Loop::begin();
        startBlock(0, "Main");
    }

    bool beginWaits() const override { return initRunning_; }

    void afterFrame() override {
        if (initRunning_ && !threads[0]->running()) {
            initRunning_ = false;
        }
        Loop::afterFrame();
    }

protected:
    std::string script_;
    bool initRunning_ = false; // +0x15c bit 1
};

class Story final : public LoopHost {
public:
    // Research: a line per loop request carried out (pop / restart / push).
    std::function<void(const std::string&)> debugLog;

    // `game`: the runtime (fonts, scripts, scene). The stack starts with the
    // game loop.
    explicit Story(Game& game);
    ~Story() override;

    // One frame of the main loop: the requests, the top loop's resume and
    // begin, its frame, the scene's updates.
    void frame(const Input& input);

    // LoopHost.
    bool push(int id, const std::vector<xq::Value>& values) override;
    void pop() override;
    void restart() override;

    bool pending() const override { return !requests_.empty(); }

    bool requestEvent(const xq::Value& event, int branch) override;
    xq::Value loopValue(int i) const override;
    bool loopCommand(int number, xq::Call& c, std::size_t first) override;
    // A typed key for the top loop (Loop::keyboard).
    void keyboard(const NameInput& key);

    Loop* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }

    // The loop of that number nearest the top (null: none on the stack).
    Loop* findLoop(int id) const {
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            if ((*it)->id == id) {
                return it->get();
            }
        }
        return nullptr;
    }

    // The loop numbers from the bottom of the stack up ("1000 1300 1200").
    std::string stackText() const;

    Game& game;
    // Research: loop numbers the port has no loop for (the request is
    // refused).
    std::function<void(int id)> unknownLoop;
    // Research: another loop in place of a pushed one (the console's
    // poked title request, its values too; tools only).
    std::function<int(int id, std::vector<xq::Value>& values)> remapLoop;
    // The app's boot: the title without the logos.
    bool startAtTitle = false;
    // The app's checkpoint (a host convenience; the other games have the
    // same): once a place is ready - the place loop on top and begun, taking
    // touches, no request waiting, two frames in a row - the game's save
    // record (saveRecord) goes to this file, once each time the place loop
    // comes back on top (entering a place, back from an event or a puzzle).
    // Empty: none written.
    std::filesystem::path checkpointFile;
    // Continue from such a record: the game loop restores it and asks for
    // the place loop at the saved place instead of the title - the title's
    // continue (10100, then 10001) without its recap event.
    std::optional<SaveRecord> continueFrom;

private:
    struct Request {
        int kind = 0; // 0 pop, 1 restart, 3 push (0x29630c's kinds)
        int id = 0;
        std::vector<xq::Value> values;
    };

    void carryOut();
    std::unique_ptr<Loop> make(int id, std::vector<xq::Value> values);

    // Writes the checkpoint once the place is ready (checkpointFile).
    void checkpoint();

    std::vector<std::unique_ptr<Loop>> stack_;
    std::deque<Request> requests_;
    std::uint64_t resumeSerial_ = 0, checkpointSerial_ = 0;
    int readyFrames_ = 0;
};

// A data list's operand as a value.
xq::Value valueOf(const import::adventure56::DataList& list, const import::adventure56::Operand& o);
// A data list file's commands with their operands (lists of res/sub).
struct DataCommand {
    int opcode = 0;
    std::vector<xq::Value> operands;

    int number(std::size_t i) const { return i < operands.size() ? operands[i].toInt() : 0; }
    float real(std::size_t i) const { return i < operands.size() ? operands[i].toFloat() : 0.0f; }
    std::string text(std::size_t i) const {
        return i < operands.size() && operands[i].isText() ? operands[i].text : std::string();
    }
};
std::vector<DataCommand> readDataList(Game& game, const std::string& path);

} // namespace picarat::adventure5
