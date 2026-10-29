#pragma once
// The fifth adventure's runtime: the definitions table (lt5_def), the game
// state (flags and variables), the script machine with its loaded scripts,
// the scene, input, and the script services. Loops (the puzzle session and
// its play loops, menus) run on top of it (puzzles/a5_puzzle.hpp).
//
// The definitions (res/ge/lt5_def.xs, an XSCR list) name every flag (2001
// {id, name}), every variable (2011 {byte offset, bytes, name}; negative
// sizes are signed) and every puzzle (2301 {id, name, title,
// type, type name, picarats for 0/1/2+ wrong answers, place, ...}; the row
// order is the puzzle number); variable sizes are byte counts. Scripts address flags and variables
// by name or number (16200/16201 flags, 16300/16301 variables; an optional operand picks a slot of
// an array variable).
#include "graphics/a56_assets.hpp"
#include "graphics/ctr3d/raster.hpp"
#include "puzzles/a56_handwriting.hpp"
#include "sound/a56_sound.hpp"
#include "sound/a56_stream_players.hpp"
#include "system/a56_scene.hpp"
#include "system/a56_xq.hpp"
#include "system/a5_progress.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace picarat::moflex {
struct Tables;
}

namespace picarat::adventure5 {

struct PuzzleRecord {
    int id = 0;     // script res/naz/seq/n<id>.xq, texts txt/@/50/50_<id>.xs
    int number = 0; // shown number (row in the catalogue)
    std::string name, title, typeName;
    int type = 0;
    int picarats[3] = {};
    // The place it is found in, the story step that leaves it behind
    // (passed by when the story enters that step) and the item solving it
    // gives (2301 fields 8..10).
    std::string place, lostStep, unlockItem;
};

// Names in the game's definitions and scripts are Shift-JIS; the ones the
// native code refers to, as byte strings.
namespace names {
inline constexpr const char* verdict = "\x83\x69\x83\x5d\x94\xbb\x92\xe8"; // ナゾ判定
// ＜ナゾボタン＞判定無効: the judge button is off.
inline constexpr const char* judgeOff = "\x81\x83\x83\x69\x83\x5d\x83\x7b\x83\x5e\x83\x93\x81\x84"
                                        "\x94\xbb\x92\xe8\x96\xb3\x8c\xf8";
// ＜ナゾ＞操作無効: the puzzle takes no input.
inline constexpr const char* inputOff =
    "\x81\x83\x83\x69\x83\x5d\x81\x84\x91\x80\x8d\xec\x96\xb3\x8c\xf8";
} // namespace names

struct Definitions {
    std::map<std::string, int> flagIds;

    // Items of the collections (2021 {id, name, kind, mask, text, set
    // items...}; record 0x17d658: +4 id, +6 mask, +0xc kind, +0xe.. the
    // set's items).
    struct Item {
        int id = 0, kind = 0, mask = 0;
        std::string name, text;
        std::vector<std::string> set;
    };

    std::map<int, Item> items;
    std::map<std::string, int> itemByName;
    // Characters (2111 {id, name, ...}; the id names their files chr/c%3d;
    // 0x275f34 looks a name up, the id is +4 & 0x3ff).
    std::map<std::string, int> characterIds;

    struct Variable {
        int offset = 0, size = 1; // bytes 1..4 (negative: signed)
    };

    std::map<std::string, Variable> variables;

    // The initial state (2201 0 "初期状態" with its 2203 entries: 1512 sets
    // a flag, 1521 a variable to a value, 1702 the place): a new game's
    // values (the hint coins: 10).
    struct InitialEntry {
        int code = 0;
        std::string name;
        int value = 0;
    };

    std::vector<InitialEntry> initialState;
    std::map<int, Variable> variablesByOffset;
    std::vector<PuzzleRecord> puzzles;

    // Sound scenes (2101 {id, music, ambience, effect set}; 11000).
    struct SoundScene {
        int music = 0, ambience = 0, effects = 0;
    };

    std::map<int, SoundScene> soundScenes;

    const PuzzleRecord* puzzle(int id) const;
    const PuzzleRecord* puzzleByName(const std::string& name) const;
};

// The edition: the fifth or the sixth adventure (the same runtime; the
// definitions and puzzle parameter files are named after the game).
enum class Edition { Fifth = 5, Sixth = 6 };

// "res/@/lt5_def" / "res/@/lt6_def".
std::string definitionsPath(Edition edition);
// "res/lt5_naz.xb" / "res/lt6_naz.xb".
std::string puzzleParametersPath(Edition edition);

// The catalogue and the variable and flag names (res/@/lt5_def).
Definitions loadDefinitions(const adventure56::Pack& pack, Edition edition = Edition::Fifth);
// A catalogue text (escapes such as {:a}) as UTF-8 for the host.
std::string displayText(const std::string& text);

// Touch, buttons and tilt for one frame (the lower screen's pixels).
struct Input {
    bool touching = false;
    int x = 0, y = 0;
    // Held buttons in the game's own order (the input record's +0xa4 bits,
    // 0x2d33d8): the pad's left, right, up and down first.
    std::uint32_t buttons = 0;

    enum : std::uint32_t { kLeft = 1, kRight = 2, kUp = 4, kDown = 8 };

    // The motion sensor's readings the game keeps at +0x540 / +0x548
    // (0 when the console lies flat; type 16 slides its blocks by them).
    float tiltX = 0, tiltZ = 0;

    // The accelerometer in G (x, y, z; the sixth adventure keeps it at game
    // +0x548 while its motion sensor runs, the raw reading x 1/512). Lying
    // still and flat the console reads (0, -1, 0) (reference, puzzle 126).
    std::array<float, 3> acceleration{0, -1, 0};

    // The circle pad (game +0x478 / +0x47c; 1130 / 1131): the ruins'
    // script steps when one passes 0.5 either way (x right, y up, so about
    // -1..1; the scaling of the console's readings is not recovered). The
    // hosts leave them 0: the pad's buttons step as well.
    float circleX = 0, circleY = 0;
};

// A special puzzle type's play loop (the session loop class picked by the
// puzzle's type, 0x18320c; its methods: commands from the scripts' 2110,
// vtable +0x4c; input after the touch, +0x50; an update before the loop's
// scripts, +0x54). The classes live in src/puzzles/a5_special*.
class SpecialPuzzle {
public:
    virtual ~SpecialPuzzle() = default;
    // A 2110 command: its number and operands from `first` on; false when
    // the class does not take it (the service then fails).
    virtual bool command(int number, xq::Call& c, std::size_t first) = 0;

    virtual void input() {}

    virtual void update() {}

    // Research: the puzzle's model state as text (grids, pieces), for the
    // runner's A5_DESCRIBE output; empty when the class has none.
    virtual std::string describe() const { return {}; }
};

// What the scripts ask of the loops (the game's loop manager: requests at
// game +0x1d3c, carried out at the start of the next frame). The story
// (story/a5_story.hpp) implements it; a lone puzzle session has none.
class LoopHost {
public:
    virtual ~LoopHost() = default;
    // 2104 / 0x2c592c: a loop by number on top, with up to six values.
    virtual bool push(int id, const std::vector<xq::Value>& values) = 0;
    // 2102 / 0x2d4c98: the top loop ends.
    virtual void pop() = 0;
    // 2103 / 0x2d4c04: the top loop starts again (suspend + resume).
    virtual void restart() = 0;
    // 2100 / 0x2cb770: requests are waiting.
    virtual bool pending() const = 0;
    // 10000 (0x2b2ba4 ...): an event by number or name is played next.
    virtual bool requestEvent(const xq::Value& event, int branch) = 0;
    // The top loop's value `i` (0x2adf80).
    virtual xq::Value loopValue(int i) const = 0;
    // 2110 (0x12cb68 -> the top loop's vtable +0x4c): a command to the
    // loop; false when it takes none.
    virtual bool loopCommand(int number, xq::Call& c, std::size_t first) = 0;
};

class Game final : public adventure56::MessageHost {
public:
    explicit Game(const std::string& packRoot, Edition edition = Edition::Fifth);
    ~Game() override;

    const Edition edition;

    adventure56::Pack pack;
    Definitions defs;
    xq::Machine machine;
    adventure56::Scene scene;

    // ---- state
    bool flag(int id) const;
    // A flag's number by its name (lt5_def 2001), -1 when there is none.
    int flagId(const std::string& name) const;
    void setFlag(int id, bool on);
    std::int32_t variable(const Definitions::Variable& v, int slot = 0) const;
    void setVariable(const Definitions::Variable& v, std::int32_t value, int slot = 0);
    std::int32_t variable(const std::string& name, int slot = 0) const;
    void setVariable(const std::string& name, std::int32_t value, int slot = 0);
    // A new game's state from the definitions (Definitions::initialState).
    void applyInitialState();

    // The flags set and the variable block, whole (the saves).
    const std::set<int>& flagSet() const { return flags_; }

    void setFlagSet(std::set<int> flags) { flags_ = std::move(flags); }

    const std::vector<std::uint8_t>& variableData() const { return variableBytes_; }

    // (A shorter block from an older save leaves the rest as it is.)
    void setVariableData(const std::vector<std::uint8_t>& bytes) {
        std::copy_n(bytes.begin(), std::min(bytes.size(), variableBytes_.size()),
                    variableBytes_.begin());
    }
    // The save slots (a5_saves.hpp; in memory unless the host gives a
    // folder) and the last save's or load's failure (game +0x197e0 bit 0).
    std::shared_ptr<class SaveSlots> saves;
    bool saveFailed = false;
    SaveSlots& saveSlots(); // (in memory when the host gave none)

    // ---- scripts: loaded files, newest first in block lookups
    const xq::Program* loadScript(const std::string& path); // "seq/prog_nazo.xq"

    const xq::Program* newestScript() const { return newest_; }

    // ---- frame
    Input input, previousInput;
    std::uint64_t frame = 0;

    // The sixth adventure's motion sensor (20500..20503, a6_services.cpp):
    // running since which frame; the reading kept when it stopped.
    struct MotionSensor {
        bool on = false;
        std::uint64_t since = 0;
        std::array<float, 3> held{};
    } motionSensor;

    void beginFrame(const Input& in);
    void endFrame(); // scene update
    void render(ctr3d::Target& top, ctr3d::Target& bottom) const;

    // The running loop's two script threads (0: its main block, 1: the
    // per-frame block that 2220 starts).
    xq::Thread* loopThreads[2] = {nullptr, nullptr};

    // Touch: a new press this frame / released this frame.
    bool pressed() const { return input.touching && !previousInput.touching; }

    bool released() const { return !input.touching && previousInput.touching; }

    // The touch as the scripts read it (the input object at +0x4b4 of the
    // game: +0x5c this frame's movement, +0x64 where the press began, +0x6c
    // the position, kept after release; +0x4c new press / held / released).
    int touchX = 0, touchY = 0, pressX = 0, pressY = 0, moveX = 0, moveY = 0;
    int heldFrames = 0;  // frames the touch has been held since the press
    bool tapped = false; // released this frame as a tap (see beginFrame)
    // The console's wireless address as the game reads it from the shared
    // page (0x1ff81060, copied to 0x411d9c by 0x394908 unless all zero); the
    // secret door's passwords depend on it (1300). A host without one
    // leaves it zero, as the original leaves its copy.
    std::array<std::uint8_t, 6> consoleAddress{};
    // The cameras' per-frame shake offsets (0x221944; see the definition).
    void cameraJitter();
    // The held touch's repeat pulse (0x3a61f0 flag 0x400: at the press, then
    // after 20 display frames every 5) and its countdown.
    bool repeatPulse = false;
    int repeatCountdown = 0;
    bool objectTouchLocked = false; // 2109: objects get no touches

    // The pad-driven touch cursor (1221 / 1220; game +0x480 settings,
    // +0x4b4 source 1). A press inside the bounds while it is on makes the
    // touch source 1 (0x3a6a88: +0x80 bits 0xc000, seen as bit 8 a frame
    // later); its point then moves by the drag times the speed, kept inside
    // the rectangle (console: a still hold leaves it, a 120-pixel drag
    // moves it 180 at speed 1.5 up to x 379). *Hypothesis*: the original's
    // capped step (0x2d0fe0 in 0x3a6740..) never limits a hand's drag.
    struct PadCursor {
        bool on = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the rectangle
        int bx = 0, by = 0, bw = 0, bh = 0; // where a press counts
        float speed = 1;
        // 1222 (0x339bbc): the point follows the press absolutely, x =
        // (x0 - bx) + touch x x width / box width (and so for y), instead
        // of moving by the touch's steps (1221).
        bool absolute = false;
        float x = 0, y = 0;         // source 1's point
        int pressX = 0, pressY = 0; // ... where its press began
    } padCursor;

    int touchSourceId = 0; // the touch's source: 0 the screen, 1 the cursor

    // A touch query's source matches (operand, default 0) and objects take
    // touches.
    bool touchFrom(int source) const { return !objectTouchLocked && source == touchSourceId; }

    // Object handlers: hit-tests the touch, switches handler states and
    // runs the handler blocks (0x37aaa4, 0x2cd1d0).
    void updateObjects();
    void readInput(const Input& in);
    void absolutePad(); // 1222's mapping of the touch
    void processTouch();
    void startHandler(adventure56::Object& o, int state);
    // The engine's own object calls, shared by the services and the special
    // puzzles: create (5002: 0x2d1c34; the values become registers 0..15),
    // an animation set (5600: 0x2cd9b0), play an animation by name or hash
    // (5622: flags 4 restart, 8 once), handler blocks (5031..5034 modes
    // 0..3: 0x2ca394 / 0x28f450 ...).
    adventure56::Object& createObject(const std::string& name, int screen, int priority, int kind,
                                      std::span<const xq::Value> values, int level = -1);
    bool setAnimationSet(adventure56::Object& o, std::uint32_t resource);
    bool playAnimation(adventure56::Object& o, const xq::Value& name, std::uint32_t flags);
    void setHandlers(adventure56::Object& o, int mode, std::span<const std::string> names);
    // Effects by number (11024 / 11025 / 11021).
    // 3010 / 3011 (0x32b16c / 0x2ca8ec): every object of a screen (all
    // screens for one past 4) shown or hidden, or its touch switched.
    void setEveryObject(bool touch, bool on, unsigned screen);
    bool playEffect(int number);
    // A world point seen through a screen's camera (0x2cbb48): fractions
    // across and down the screen, and the depth.
    std::array<float, 3> projectToScreen(int screen, float x, float y, float z) const;
    void stopEffect(int number, int frames);
    // 0x2bb540 (player 1): the effect still sounds.
    bool effectPlaying(int number);
    bool effectsSounding(int number); // -1: any player
    std::string effectName(int number) const;
    // The special puzzle type's loop (null for the standard loop).
    std::unique_ptr<SpecialPuzzle> special;
    bool showPicture(xq::Call& c, adventure56::Object* o); // 5430 / 6430
    // 5044: a block called on the object's handler thread with arguments
    // (from its innermost running block, 0x2b8a04).
    bool startObjectBlock(adventure56::Object& o, const std::string& block,
                          std::span<const xq::Value> arguments);
    // 0x290f5c + 0x2877b8 (the robot game's piece blocks): the update
    // switch on (vtable +0x14), the block called from the handler thread's
    // innermost running block with arguments (started when nothing runs),
    // and the object updated at once with no time passing. *Port*: only
    // the handler thread runs then (a step of no time moves nothing else);
    // a call from the thread's own run is left to that run.
    bool callObjectBlock(adventure56::Object& o, const std::string& block,
                         std::span<const xq::Value> arguments);

    // 11053: stream players' volumes moving over frames (internal player
    // -> from, to, frames, elapsed), stepped at the end of each frame.
    struct VolumeMove {
        float from = 1, to = 1;
        int frames = 0, elapsed = 0;
    };

    std::map<int, VolumeMove> volumeMoves;
    std::array<float, 6> streamVolume{1, 1, 1, 1, 1, 1};
    // The movies opened (3802), ticked at the end of each frame.
    std::vector<std::weak_ptr<adventure56::MoviePlayer>> movies;

    // Whether the object's handler thread runs a block (+0x17c: the
    // handler's root frame has a child; 5040).
    bool handlerRunning(const adventure56::Object& o) const {
        const auto it = handlerThreads_.find({o.is3D, o.hash});
        return it != handlerThreads_.end() && it->second->running();
    }

    // A canvas with a pen draws along the touch that picked it.
    void penTo(adventure56::Object& o, float x, float y, bool start);
    // 0x391f74: the pen lifts from a canvas; in mode 16 the strokes so far
    // are recognised (0x3a283c) into pen.recognised.
    void penUp(adventure56::Object& o);
    // 3041 (0x123520 -> 0x1a7ea8): the handwriting recogniser for a box of
    // width x height and a character category (1 digits, 2 capitals, 3
    // small letters, 4 letters, 5 hiragana, 6 katakana).
    bool setupRecogniser(int width, int height, int category, int limit);
    // An object's values 0..15 (5045/5046, 6045/6046). While the object has
    // a handler thread, the thread's registers are those values: a value
    // another script stores reaches the running handler, and what the
    // handler computes is what others read.
    xq::Value objectValue(const adventure56::Object& o, std::size_t i) const;
    void setObjectValue(adventure56::Object& o, std::size_t i, const xq::Value& v);

    // Research: services the reconstruction lacks (opcode -> count).
    std::map<int, int> missing;
    std::function<void(const std::string&)> log;
    bool traceHandlers = false;

    // The current puzzle (session) and its texts.
    const PuzzleRecord* puzzle = nullptr;

    // ---- the story (system/a5_progress.hpp)
    StoryTables story;
    LoopHost* loops = nullptr;
    // The event being played (game +0x12fd0: its name's hash) and the
    // puzzle the story is at (+0x12cb0, by name).
    const EventRecord* currentEvent = nullptr;
    std::string currentPuzzleName;
    // Puzzles passed by, as puzzle indexes (game +0x8d82, count +0x8e38).
    std::vector<int> passedBy;
    // The named conditions' truth (game +0x12c34; refreshConditions).
    std::vector<bool> conditionBits;
    // The guide (+0x12c48, per place in definition order): the place id a
    // place is reached from on the way to the goal (-1) and its distance
    // (0xffff: none).
    std::vector<int> guideFrom, guideDistance;
    int soundOutputMode = 1; // 11015
    // A 3D object's shake (6270 / 6272: the tween at +0x35c, by object
    // hash). *Not drawn*: the jitter it causes is not read yet.
    std::map<std::uint32_t, adventure56::Tween<3>> shakes;
    std::map<std::uint32_t, adventure56::Tween<3>> shakes2D; // 5280 / 5282: 2D objects (+0x404)
    // A puzzle's saved state (0 new, 1 seen, 2 solved; 0x2d1fcc).
    void setPuzzleState(int id, int state);
    // 1591 / 1804 (0x2abd4c ...): an item of the collections (lt5_def 2021,
    // by name or id) is received.
    void giveItem(const std::string& name);
    void giveItem(int id, int argument = 0);

    // An item notice (game +0x12bb0, 8 bytes): the item, 0, its kind
    // (record +0xc) and mask (+6); at most 16 (+0x12c30).
    struct ItemNotice {
        int item = 0, argument = 0, kind = 0, mask = 0; // argument: e.g. a puzzle index
    };

    std::vector<ItemNotice> itemNotices;
    // The room each screen shows (12000 / 12005, game +0x197e2; -1 none).
    std::array<int, 5> screenRooms{-1, -1, -1, -1, -1};
    // The area map's slots (12100 / 12102 / 12103): shown and prepared
    // areas (-1: none) and the shown slot (1 or 2; *hypothesis*: 1 at the
    // start).
    int gmpShown = -1, gmpPrepared = -1, gmpSlot = 1;

    // The shown area's spots and routes (res/lt5_gmp.xb/g<area>.xs).
    struct AreaPaths {
        int area = -2;

        struct Spot {
            std::string place;
            int x = 0, y = 0, extra = 0;
        };

        struct Route {
            std::string from, to;
            std::vector<std::array<int, 2>> points;
        };

        std::vector<Spot> spots;
        std::vector<Route> routes;
    };

    const AreaPaths& areaPaths();
    // 0x2c18b8: a screen's room goes (its "MAP_%04d" objects and model).
    void removeRoom(int screen);
    // 0x29e22c: area map slots (bit 0 slot 1, bit 1 slot 2) are released.
    void releaseAreaMap(unsigned slots);
    // The cutscene clock (2610 / 2611, game +0x10b8).
    float dramaClock = 0;
    // Four names (16101; 0 the player's).
    std::array<std::string, 4> names;

    // ---- scopes (2190 / 2191 and the loops' own levels): the scene's,
    // and the scripts loaded inside one go with it.
    void openScope(bool snapshot);
    void closeScope();
    // Closes scopes down to `depth` (0x2648c8).
    void closeScopesTo(int depth);

    // Saved puzzle progress by id (a halfword per puzzle at 0x89a4: bits
    // 0..1 the state 0 new, 1 seen, 2 solved (16520), bits 2..4 the
    // picarat reductions from wrong answers (16522)).
    struct PuzzleProgress {
        int state = 0, reductions = 0;
        int hints = 0;         // bits 5..7: hints opened (0..4; 0x185428)
        bool passedBy = false; // bit 8: left behind by the story
    };

    std::map<int, PuzzleProgress> progress;

    // The ruins' record (progress +0x8fb4, 0x43c bytes; the ruins loop works
    // on a copy, 0x1c6c10, and writes it back, 0x1c7c90 / 0x1c798c): per
    // floor (index floor - 1) the rooms seen (+0x394, a bit each) and the
    // floor's object states, and where Layton and Randall stood last
    // (+0x3b4..+0x3cc, 0x2c8378). The object lists (0x1868xx) are filled as
    // their commands are ported.
    struct RuinRecord {
        struct Stand {
            int row = 0, column = 0;
            float angle = 0; // the model's y rotation
        };

        int floor = 0; // +0x3b4: 0 none stored
        Stand layton, randall;
        // +0x3bc..+0x3d8: where the floor was entered (106), for a retry
        // (105 / 134): the floor, Layton's cell and turn, Randall's goal
        // and turn, the flags.
        int retryFloor = 0;
        Stand retryLayton, retryRandall;
        std::uint32_t retryFlags = 0;
        // +0x354: up to 16 names noted by the events (122, 0x186ccc).
        std::array<std::uint32_t, 16> notedNames{};
        std::uint32_t flags = 0; // +0x3cc: bit 0 Randall does not follow, bit 1 (+0x630)
        std::array<std::uint32_t, 8> roomsSeen{};   // +0x394: a bit per room (0x186bf0)
        std::array<std::uint32_t, 8> roomsMapped{}; // +0x41c (128, 0x186c24)
        // Per floor: the floor events noted (+0x2ee: {number, value}
        // pairs, a floor's span between the bytes +0x2e5 / +0x2e6; dug
        // treasures, 0x1d3960) and the raised blocks' heights (+0x240:
        // {row, column, height}, spans +0x237 / +0x238; kind 1006 cells,
        // 0x186b58). The spans' sizes are not modelled (hypothesis: never
        // full).
        std::array<std::map<int, int>, 8> eventNotes;
        std::array<std::map<std::pair<int, int>, int>, 8> raisedHeights;

        // Per floor, by the object's event number: switches and levers
        // (0x2929d0, +0x1ca: pressed), plates (0x186aec, +0xf9: 0 broken),
        // blocks (0x186984: bit 0 sunk, row, column).
        struct BlockNote {
            bool sunk = false;
            int row = 0, column = 0;
        };

        std::array<std::map<int, int>, 8> switchNotes, plateNotes;
        std::array<std::map<int, BlockNote>, 8> blockNotes;
    } ruins;

    // The rabbit's record (progress +0x8f78..+0x8fb3; story/a5_rabbit.cpp):
    // per trick slot (below 32) a bit learned (+0x8f78) and mastered
    // (+0x8f7c; the state 0x280ba0 reads 2 mastered, 1 learned, else 0),
    // a "new" mark (+0x8f80, 0x280b60 / 0x33f7bc) and a byte (+0x8f84,
    // 0x33f978: the variant of the trick's third text); 128 flags
    // (+0x8fa4, 0x18562c / 0x280adc).
    struct RabbitRecord {
        std::uint32_t learned = 0, mastered = 0, fresh = 0;
        std::array<std::uint8_t, 32> variants{};
        std::array<std::uint8_t, 16> flags{};
    } rabbit;

    // Play time: seconds = (frame - playStart) / 60 (0x12fe0; a loaded
    // save moves the start back by its saved seconds, 0x283xxx).
    std::int64_t playStart = 0;
    // The master volume (11016 / 11017; the settings record's byte 1).
    int masterVolume = 10;
    // The game's language code (2930; 4 in the German version).
    int language = 4;
    // 2400: display frames per game frame (1: 60 per second, 2: 30).
    int displayFramesPerFrame = 1;
    // Sound (null: silent). The effect set 11024's numbers address
    // ("SI_<set>_<n>", else the system set "SY_000_<n>").
    adventure56::SoundPlayer* sound = nullptr;
    int effectSet = 0;
    // The stream players as the scripts see them (advanced per frame,
    // with or without sound), what each internal player has prepared for
    // the audio to start, and the streams' INFO by name.
    adventure56::StreamPlayers streams;
    std::string preparedStream[adventure56::StreamPlayers::kPlayers];
    const adventure56::StreamInfo* streamInfo(const std::string& name);
    adventure56::SoundArchive* soundArchive(); // snd/product.bcsar, null without
    // 6850 / <M>: a character's motions by number on its three layers.
    void setCharacterMotion(adventure56::Object& o, std::array<int, 3> numbers);
    // The message engine's events (graphics/a56_message.hpp; the game's
    // vtable +0xa8, 0x33336c).
    xq::Value messageEvent(int code, std::span<const xq::Value> args) override;
    // A stream on a script player (0 stops it) / a sound scene (2101).
    bool playStream(int player, int number, int fade, bool autoStart);
    bool playNamedStream(int player, const std::string& name, int fade, bool autoStart);
    // 0x2bc724: the current event's voice line (EVT_GG_NNNNNN_LLLL).
    bool playEventVoice(int line, int fade, bool autoStart);
    bool selectSoundScene(int id, int fade = 60, bool autoStart = true);

    // The sound effects as the scripts see them: each started effect on
    // its effect player (0 for numbers below 200, 1 from 200: 0x350954)
    // with the samples it still sounds, from a silent run of its sequence
    // (tracks and release tails; the original keeps the handle as long).
    struct ActiveEffect {
        int player = 0;
        std::string name;
        double remaining = 0; // samples at the 3DS sound rate
    };

    std::vector<ActiveEffect> activeEffects;
    double effectDuration(const std::string& name);
    // The game's code and data segment (pack code/code.bin, loaded at
    // 0x00100000): constant tables the native code reads (the name
    // keyboard, the handwriting classes).
    static constexpr std::uint32_t kCodeBase = 0x00100000;
    const std::vector<std::uint8_t>& code();
    std::uint32_t codeWord(std::uint32_t address);
    std::string codeText(std::uint32_t address, std::size_t limit = 64);
    // The current puzzle's six parameter values (16540).
    std::vector<xq::Value> puzzleValues;

private:
    void registerLibraryServices();
    void registerGameServices();
    void registerHandlerServices();
    void registerStoryServices();
    void registerMessageServices();
    void registerStageServices();
    void registerSixthServices(); // src/system/a6_services.cpp
    void register3DServices();
    // Handler threads by (3D list, object hash).
    std::map<std::pair<bool, std::uint32_t>, std::unique_ptr<xq::Thread>> handlerThreads_;
    std::map<std::string, std::unique_ptr<adventure56::StreamInfo>> streamInfo_;
    Input pendingInput_;
    std::unique_ptr<adventure56::SoundArchive> soundArchive_;  // stream files, durations
    std::unique_ptr<adventure56::hwr::Recognizer> recogniser_; // game +0xbc0
    int recogniserLimit_ = 0;                                  // +0xbc8
    std::vector<std::uint8_t> codeSegment_;                    // constant tables
    bool soundArchiveTried_ = false;
    std::map<std::string, double> effectDurations_;
    std::uint32_t pressedObject_ = 0;
    std::vector<std::unique_ptr<xq::Program>> scripts_;
    // The scripts blocks are looked up in, oldest first, with the scope
    // depth each was loaded in.
    std::vector<std::pair<xq::Program*, int>> liveScripts_;
    AreaPaths areaPaths_;
    const xq::Program* newest_ = nullptr;
    std::shared_ptr<moflex::Tables> movieTables_; // 3802: the decoder's tables from the code
    std::vector<std::uint8_t> variableBytes_;
    std::set<int> flags_;
};

} // namespace picarat::adventure5
