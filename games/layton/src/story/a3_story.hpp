#pragma once
// The third adventure as a whole: a stack of "loops" (the game's lt3CLoop
// classes) run by a list of requests, from the logos to the end credits.
//
// How the game is put together
// ----------------------------
// Everything the player sees is one loop at a time: the logos, the title
// menu, an event (a scripted scene), a place (a room to look around), a
// movie, a puzzle, the bag and its menus. Loops sit on a stack; the top one
// runs. Scripts and loops ask for changes by posting requests, which the
// game handles at the start of the next frame, in order:
//
//   push     a new loop on top; the one below waits (and lets go of what it
//            showed) until it is uncovered again;
//   replace  the top loop ends and a new one takes its place;
//   pop      the top loop ends; the one below carries on;
//   overlay  like push, but the loop below is not put to sleep first.
//
// While the top loop is busy finishing (a place running its closing
// script, a loop still loading) the requests wait. The bottom of the stack
// is the game itself (the "GAME" loop): it loads the game's data and its
// resident scripts, and when it comes to the top again it starts over with
// the title.
//
// Most loops are scripts with a little native code around them:
//  * script loops (logo, title, bag, menus, credits...) run the MAIN section
//    of one program; they start it again whenever they are uncovered;
//  * the event loop runs evt/GG/GG_IIIIII's MAIN with the event's talk
//    texts; the place loop runs the room's PLACE_BEGIN / PLACE_LOOP /
//    PLACE_END; the movie loop plays a film with captions; the puzzle loop
//    is adventure3::PuzzleLoop.
// Every loop's scene also has the game's resident scripts (lt3_common,
// lt3_place, lt3_event, lt3_memo, lt3_saveload): the macros all scripts use.
//
// Reconstructed from overlay 0 (requests 020640a8/020640cc/020640f0 and
// 02063d7c, the scheduler 02064580/020649fc), overlay 6 (the loop registry,
// 020f4740; the loop base class 020f3290..020f35d8), overlay 12 (GAME),
// overlay 14 (script loops), 13 (events), 15 (places), 16 (movies). See
// .analysis/adventure3/docs/story.md.
#include "graphics/text.hpp"
#include "puzzles/a3_puzzle_loop.hpp"
#include "story/a3_data.hpp"
#include "story/a3_saves.hpp"
#include "system/game_types.hpp"
#include "system/a3_lscr_scene.hpp"
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace picarat {
class Adventure3Sound;
}

namespace picarat::adventure3 {

class Story;

// The four numbers a request passes to its loop (the scripts' thread
// variables 200..203 of the new loop).
using LoopArgs = std::array<int, 4>;

// One loop on the stack.
class Loop {
public:
    Loop(Story& story, std::string name, LoopArgs args);
    virtual ~Loop();

    const std::string& name() const { return name_; }

    const LoopArgs& args() const { return args_; }

    // Created and pushed (vtable +0x14, then the loop's own setup at +0x38).
    virtual void start() {}

    // Becomes the top loop: right after it starts and whenever it is
    // uncovered again (+0x1c -> +0x40).
    virtual void activate() {}

    // Covered by a new loop (+0x20 -> +0x44): what it created since it was
    // activated goes.
    virtual void deactivate() {}

    // Ends (popped or replaced; +0x18).
    virtual void end() {}

    // One frame while on top.
    virtual void frame(const lscr::SceneInput& input) = 0;
    // Requests wait while the top loop is busy (vtable +8): by default
    // while either screen's brightness is still changing (020eb3e8; the
    // game also waits for some sound and pen states, not reproduced).
    virtual bool busy() const;

    virtual Surface render() const;

    // Commands 1280..1299 (02106xxx: the top loop's vtable +0x58 + 4n):
    // what each loop does with them; `index` 0..19. By default nothing
    // (020f1844 and on).
    virtual lscr::Result command(int index, lscr::Call& call) {
        (void)index;
        (void)call;
        return lscr::Result::Next;
    }

    // Conditions 2210..2217 (02103fe0: the top loop's vtable +0x98 + 4n,
    // after its commands): what each loop answers; by default false.
    virtual bool condition(int index, lscr::Call& call, std::span<lscr::Value> args) {
        (void)index;
        (void)call;
        (void)args;
        return false;
    }

    // A key typed on the computer keyboard (a native convenience; only the
    // name entry takes it).
    virtual void keyboard(const NameInput& key) { (void)key; }

    virtual lscr::Scene* scene() { return scene_.get(); }

    virtual const lscr::Scene* scene() const { return scene_.get(); }

protected:
    // A fresh scene with the game's commands and resident scripts; its
    // brightness continues from the screens as they are now.
    std::unique_ptr<lscr::Scene> makeScene();

    Story& story_;
    std::string name_;
    LoopArgs args_;
    std::unique_ptr<lscr::Scene> scene_;
};

class Story {
public:
    // `sound`: the game's sound (none: silent).
    // `saves`: the folder of the save slots (none: kept in memory only).
    // `boot`: start the game (GAME); without it the stack starts empty and
    // the caller requests the loops (the app's movie player).
    Story(Assets& assets, const GameText& text, Adventure3Sound* sound = nullptr,
          std::filesystem::path saves = {}, bool boot = true);
    ~Story();

    // A host convenience: GAME starts the title without MENU_LOGO on top
    // (lt3_menu_logo only shows the logos and plays their jingle; the
    // title's own MAIN sets its screens up). The app sets it; research tools
    // keep the power-on timeline.
    bool startAtTitle = false;

    void frame(const lscr::SceneInput& input);
    Surface render() const;

    // --- Requests (020640a8 push, 020640cc overlay, 020640f0 replace,
    // 02063d7c pop) ----------------------------------------------------------
    enum class Request { Pop, Push, Overlay, Replace };
    void request(Request kind, const std::string& name = {}, LoopArgs args = {});

    // Whether requests are waiting (condition 2200, 02063d3c).
    bool requestsPending() const { return !requests_.empty(); }

    Loop* top() { return stack_.empty() ? nullptr : stack_.back().get(); }

    // A key typed on the computer keyboard, for the top loop.
    void keyboard(const NameInput& key) {
        if (Loop* t = top()) {
            t->keyboard(key);
        }
    }

    // The loops on the stack, bottom first (research and tests).
    std::vector<std::string> stackNames() const;

    // Called with each loop that becomes the top (research traces).
    std::function<void(Loop&)> onActivate;

    Assets& assets;
    const GameText& text;
    Adventure3Sound* sound;
    lscr::GameState state;
    StoryData data;
    // The event being played (020ea198: +0x5f6c..), the puzzle the story
    // points at (+0x5f88; a puzzle number, -1 none) and the save slot in use
    // (+0x580c, -1 none).
    const EventInfo* currentEvent = nullptr;
    int currentPuzzle = -1;
    int saveSlot = -1;
    // What the game remembers about each puzzle (by number; the save
    // record's 224 puzzle words, +0x340).
    std::map<int, PuzzleRecord> puzzles;

    // Items just collected, waiting to be announced (at most 16; 020e0c7c):
    // {id, argument, value, kind}.
    struct ItemNotice {
        int id = -1, argument = 0, value = 0, mask = 0;
    };

    std::deque<ItemNotice> itemNotices;
    // 020e0c7c: an item is collected - its flag or counter, the "new"
    // flags of the three modes, and a notice; a set collects its members.
    void collect(const ItemInfo& item, int argument = 0, int depth = 0);
    // 020e61b8: a puzzle's state changes (1551; the puzzle loop's verdict):
    // finding it counts it found, solving it counts it solved, adds its
    // picarats (by the failures before) and brings its item and the
    // "puzzle added" notice; completing a group brings its completion item.
    void setPuzzleState(int number, int state);
    // The game's puzzle lists by slot (020e6810): found (state != 0),
    // marked, available and not solved; and those made available since the
    // list was last cleared (+0x5d38).
    std::vector<int> foundSlots() const;
    std::vector<int> markedSlots() const;
    std::vector<int> availableSlots() const;
    std::vector<int> newlyAvailable;
    // Frames played (1680; the game keeps a tick count from the save's
    // start).
    std::uint64_t playFrames = 0;
    // The two names the player gives (the save record's names, +0x10 and
    // +0x28): theirs, and the one the story asks for later.
    std::array<std::string, 2> names;

    // The memo pad's ink, by canvas (4760..4772).
    std::map<std::string, Surface> memos;

    // Research runs: when set and true for a loop that is not reconstructed
    // yet, a request for it is left out instead of stopping the game.
    std::function<bool(const std::string& loop)> skipLoop;
    bool knows(const std::string& loop) const;

    // Research runs: when set and true for a puzzle, the puzzle loop records
    // it found and solved without playing it.
    std::function<bool(int number)> autoSolve;

    // The save slots; saving (1692, 020ddcc8) fills the slot's header and
    // keeps the game; loading (1691, 020ddc00) needs a slot with a name and
    // continues its game (play time from what was saved).
    SaveSlots saves;
    bool saveGame(int slot);
    bool loadGame(int slot);
    // The app's checkpoint (a host convenience; SaveSlots::kCheckpoint):
    // with `checkpoints` set, every room writes the game's record when it
    // is ready (PlaceLoop); `continueCheckpoint` makes GAME load it and
    // open its place instead of the title, as the title's Continue does
    // (lt3_menu_title LOAD: 1691, 1200, 1231) but without the recap.
    bool checkpoints = false, continueCheckpoint = false;
    void saveCheckpoint();
    bool loadCheckpoint();
    SaveRecord record() const; // the game as a save record (020ddcc8)
    // A name the game's code gives a game variable, flag, phase or event
    // (a Shift-JIS string in the overlay; the scripts spell them the same).
    lscr::Value codeName(std::uint32_t address, int overlay = 6) const;
    // A new game's data (020dda70): the saved state cleared, ten hint
    // coins, no event, no puzzle, no slot.
    void resetGame();
    // Applies rows of a table as script commands on the game state (phase
    // actions 5812, program actions 5921; 020da9ec).
    void apply(const std::vector<TableCommand>& commands);
    // Whether rows of conditions all hold (5813, 5920).
    bool holds(const std::vector<TableCommand>& conditions);
    // Enters a story phase (1703, 020e8454): 現在のフェーズ and its actions.
    void enterPhase(const Phase& phase);
    // The event being played (020ea198 / 020ea158): its puzzle becomes
    // the story's current puzzle.
    void setEvent(const EventInfo* event);
    // 020e83d0: while the current phase's conditions hold, the next phase
    // (in the table's order) begins.
    void advancePhases();
    // Sets the current place (1700, 020e28b8): the place, its map, guide and
    // sound set.
    void setPlace(const PlaceInfo& place);

    // The master brightness of both screens as the last frame left it
    // (scenes start from it; -4096 black .. 4096 white).
    std::array<int, 2> brightness{-lscr::fx::kOne, -lscr::fx::kOne};
    // The stage and the guide (the stage manager is the game's, not a
    // loop's: what a loop leaves on the screens stays for the next). The
    // guide's two messages are made again, empty, for each loop.
    lscr::Scene::Stage stage;
    lscr::Scene::Guide guide;
    // 4560: the guide {number}: the upper screen's guide map and its two
    // messages (020f2bf8 -> 020c2398 with screen 1, 020c2d74).
    void setGuide(lscr::Scene& scene, int id);

    // The screens' background filters, kept by the graphics system across
    // loops (4510..4531; lt3_place resets them).
    std::array<lscr::Scene::BackgroundFilter, 2> backgroundFilter;

    // The files every loop's scene loads first (the GAME loop's resident
    // programs, 0211f740).
    static const std::vector<std::string>& residentScripts();

private:
    struct Pending {
        Request kind;
        std::string name;
        LoopArgs args;
    };

    std::unique_ptr<Loop> create(const std::string& name, const LoopArgs& args);
    void handleRequests();
    std::vector<std::unique_ptr<Loop>> stack_;
    std::deque<Pending> requests_;
    bool activated_ = false; // the top loop has been activated
};

// Registers the story's commands and conditions on a loop's scene (loop
// requests 12xx, game data 15xx..19xx, saves 169x, layers 202x...). See
// a3_story_commands.cpp.
void installStoryCommands(lscr::Scene& scene, Story& story, Loop& loop);
// The part of them that needs no scene (game data), for table rows.
void installStoryData(lscr::Machine& machine, Story& story);

} // namespace picarat::adventure3
