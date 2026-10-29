#pragma once
// The third adventure's scene: the services its scripts drive.
//
// A scene (the puzzle screens, menus, events) is a set of script threads and
// the things they create by name on the two screens:
//
//  * objects - pictures, animated sprites, numbers drawn with digit sprites,
//    buttons - each on the screen of the resource it shows, at a position
//    relative to that screen, in a layer (higher layers further back);
//  * messages - text boxes laid out with one of the game's fonts;
//  * per screen: a master brightness, a backdrop colour and a picture.
//
// Object properties that change over time (position, offset, alpha, scale,
// rotation, brightness) do so through tweens: a start, a target and a curve
// over a number of frames (see Tween). Sprites play the named sequences of
// their animation; the timing comes from the animation file.
//
// Scripts reach all of this through the commands installScene() registers;
// the scene advances one frame per frame() and render() composes the two
// screens. Host-specific facts (the puzzle being played, its progress) come
// in through Scene::Host.
//
// Reconstructed from overlays 0 and 6 (sprite base 0206xxxx, tween update
// 02067444/02067658/02042738, curves 0203b930, draw list 020b0d40, messages
// 0206ad94...) and the command handlers of overlay 9. See
// .analysis/adventure3/docs/script-machine.md.
#include "graphics/a3_assets.hpp"
#include "graphics/a3_message.hpp"
#include "graphics/a3_text.hpp"
#include "graphics/text.hpp"
#include "puzzles/written_answer.hpp"
#include "system/a3_lscr.hpp"
#include <array>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace picarat {
class Adventure3Sound;
}

namespace picarat::lscr {

// Fixed point 20.12, the game's number format for positions, alpha,
// brightness and time (4096 = 1).
namespace fx {
constexpr int kOne = 4096;
// 0203b..: products round to nearest (+0x800 >> 12).
int mul(int a, int b);
// 02005c4c (the hardware divider): a * 4096 / b, rounded.
int div(int a, int b);
// Script floats to fixed point: v * 4096 truncated toward zero (02024098,
// 02023f2c); back as v / 4096 (02023fa0, 02023af0).
int fromFloat(float v);
float toFloat(int v);
// The sine table of the game's maths library (4096 steps per turn, values
// in fixed point): sin and cos of index i.
int sine(int i);
int cosine(int i);
} // namespace fx

// A value moving from `start` to `target` over `total` time units (frames
// * 4096), by curve (0203b930, 02067444):
//   1 linear, 2 decelerating 1-(1-f)^2, 3 accelerating f^2 (f counts down
//   from 1), 4 slow-fast-slow, 5 there and back on a half sine, 6 there and
//   back on a parabola, 7 endless back and forth on a cosine.
// Curves 1..4 end at the target; 5 and 6 come back to the start; 7 never
// ends. A locked tween cannot be replaced until it ends.
template <int N>
struct Tween {
    using V = std::array<int, N>;
    int curve = 0;
    bool locked = false;
    int remaining = 0, total = 0;
    V value{}, target{}, start{};

    bool active() const { return curve == 7 || remaining > 0; }

    // Sets the value at once (020656d0).
    void set(const V& v);
    // Starts moving to `to` over `duration` (time units); a duration of 0,
    // curve 0, or a target equal to the value set it at once (02065774).
    // False when locked.
    bool to(int curveKind, const V& to, int duration, bool lock = false);
    // Advances by `elapsed` time units.
    void step(int elapsed);
};

enum class Screen { Lower = 0, Upper = 1 };

class Scene;

// A scene object. Positions are fixed point, relative to the object's screen.
// Messages (text boxes, 4610..4651) share the draw list but are found by
// name separately (020f2cd4), as in the game.
struct Object {
    enum class Kind { Picture, Sprite, Number, Button, Touch, Message, Canvas };
    Kind kind = Kind::Sprite;
    std::string name;
    int id = 0;
    Screen screen = Screen::Lower;
    int layer = 0;
    int level = 0;     // the resource scope it was created in (3010)
    bool dead = false; // deleted during the frame, freed after it

    // The game's four object switches (02051d20..02051d68), each with a
    // stack that scopes save and restore (02051d80 / 02051e68):
    // shown (4105; hidden objects neither draw nor move), takes input
    // (4106, 3023), moves (4107), and a fourth (4108).
    struct Switch {
        bool on = true;
        std::uint16_t saved = 0; // earlier states, newest in bit 0
    };

    std::array<Switch, 4> switches{};

    bool shown() const { return switches[0].on; }

    bool takesInput() const { return switches[1].on; }

    bool moves() const { return switches[0].on && switches[2].on; }

    // Mirroring (+0x164): bit 0 across, bit 1 up and down - before the turn
    // (4113 sets bit 0; the fifth field of a named position).
    int mirror = 0;
    Tween<1> alpha; // 0..4096
    Tween<2> position, offset;
    // A shake (4126 / 4127; +0x188): each frame the object is drawn at a
    // random place within +-shake of where it is (020667d4).
    Tween<2> shake;
    std::array<int, 2> jitter{};
    int trail = 0; // 4114: an after-image count (+0x332; not drawn - see story.md)
    // Messages only: drawn inside this rectangle (4640; w 0: anywhere).
    std::array<int, 4> clip{};
    Tween<2> scale;
    // Rotation in fixed-point radians (0x3244 = pi), normalised to
    // (-pi, pi]. With `steps` > 0 it turns in steps of 2 pi / steps, each
    // turn taking `turnTime` (4244; 020423ac); `step` is the current one.
    Tween<1> rotation;
    int steps = 0, step = 0, turnTime = 0;

    // A grid the position snaps to (4212; 02042014): origin, the highest
    // cell, the cell size (0: none) and the snap time; `cell` is the
    // current cell (4210).
    struct Grid {
        std::array<int, 2> origin{}, last{};
        int size = 0, time = 0;
    } grid;

    std::array<int, 2> cell{};
    // The anchor point it is linked to (4125): its position is then
    // relative to the point (020ad884).
    std::string link;
    std::array<int, 2> flags{}; // 4160/4161 (flag 1: sound of a turn step)
    int kindTag = 0;            // 4116/4117
    std::map<int, Value> user;  // 4118/4119 (numbers show value 0)
    // The picture or animation shown.
    const adventure3::Picture* picture = nullptr;
    const adventure3::Animation* animation = nullptr;
    // Sprite playback (0206616c..02066a34): the sequence, the time into it
    // (frames * 4096), speed (4096 = one frame per frame), how often it
    // wrapped, and a queued sequence to switch to when it ends or wraps.
    const adventure3::Animation::Sequence* sequence = nullptr;
    int time = 0, speed = fx::kOne, loops = 0;
    bool paused = false, once = false, finished = false;
    bool starVariant = false; // 4135: prefer "<name>*" sequences
    int playFlags = 0;        // the flags the sequence was started with (+0x170)
    const adventure3::Animation::Sequence* queued = nullptr;
    bool queuedOnce = false;
    // Numbers (4020): digits of `value` by `format`, each the sequence
    // "<prefix><digit>" of the animation, `spacing` apart.
    std::string format, prefix;
    int spacing = 0;
    // Script objects (buttons 4018) own a thread that runs during their
    // update (020adc20); its variables 200.. are the object's values
    // (4118/4119).
    Thread* thread = nullptr;
    // Touchable script objects and their sections:
    //   Button (4018, 020ae5e4): N released, T touched, C clicked;
    //   Switch (4016, 020add78): one section run when touched;
    //   Piece (4021, 020afc04): N released, T picked up (moved), C turned.
    // The touch state: 0 idle, 1 pressed / carried, 2 dragged or turned,
    // 3 turned, 4 clicked (until the C section ends).
    enum class Style { Button, Switch, Piece, Script, Slider } style = Style::Button;
    // Falling objects (4023): moved by the physics step while it is on
    // (4005); their values 2 speed, 3 gravity, 4 bounce, 5 landings.
    bool dynamic = false;
    std::string releasedSection, touchedSection, clickedSection;
    int touch = 0;
    std::array<int, 2> grab{};
    int grabAngle = 0;

    // Touch areas (4121): x, y, w, h, id relative to the position.
    struct Area {
        int x, y, w, h, id;
    };

    std::vector<Area> areas;
    // Messages: the text and its reveal (graphics/a3_message.hpp);
    // `capacity` as the script created it (4610). A message with a
    // behaviour (the story's talk window) is run and drawn by it.
    adventure3::MessageText text;
    int capacity = 0;
    std::shared_ptr<struct MessageBehaviour> behaviour;
    // A character speaking (+0x168 bit 0): its sequences' "*" variants
    // (the mouth moving) are shown while it is set.
    bool talking = false;
    // Handwriting canvases (4712, 47xx; ov6 020ce6f4): a writing box read by
    // the shared recogniser, and the ink drawn so far (a screen-sized layer).
    std::shared_ptr<WrittenAnswer> writing;
    Rect area;
    int penWidth = 3, writingMode = -1;
    // 4731 / 4732: the pen set to colour 0 - it rubs ink out - and back
    // (0206c5f0 / 0206c6a0 swap the colour with the one kept at +0x3d).
    bool erasing = false;
    Surface ink;
    // Circling canvases (4711, ov6 020cdedc): what the ink's bounding box
    // (`inked`, screen pixels, right/bottom exclusive) holds of the anchor
    // points decides; values (4750/4780): 0 most points allowed, 1 error
    // sprite, 2 marker sprite, 3 points circled, 4 the point.
    bool circling = false, inking = false;
    // The ink colour (4730: entry 0xf0 + n of the screen's palette, see
    // inkColour()); 0 rubs out.
    std::uint16_t inkColour = 0x8000;
    // Memo pads (4713; ov6 lt3CPaintMemo, 020cc160..020cc580): started by
    // 4750 they run the memo script's sections MEMO_BEGIN, MEMO_MAIN and
    // MEMO_END in turn on their own thread - the next one once the last
    // has ended - and take the pen while MEMO_MAIN runs. Stage 0 idle,
    // 1..3 the section running.
    bool memo = false;
    int memoStage = 0;
    std::array<Value, 5> canvasValues{};
    std::array<int, 4> inked{};
    std::array<int, 2> pen{};
    std::vector<std::array<int, 4>> pendingInk; // segments not drawn yet
};

// What a message object does beyond showing text (the talk window): run
// each frame instead of the plain reveal, and drawn instead of the text.
struct MessageBehaviour {
    virtual ~MessageBehaviour() = default;
    virtual void update(class Scene& scene, Object& message) = 0;
    virtual void draw(const class Scene& scene, Surface& screen, Screen which,
                      const Object& message) const = 0;
    // The message has nothing more to show (condition 4310).
    virtual bool done(const Object& message) const = 0;
};

// The touch screen and buttons in one frame (the game's input object).
struct SceneInput {
    bool held = false, pressed = false, released = false; // the pen
    int x = 0, y = 0;                                     // lower screen
    // The buttons by the game's key numbers (020607fc: the DS keypad's
    // order): bit 0 A, 1 B, 2 SELECT, 3 START, 4..7 right, left, up, down,
    // 8 R, 9 L, 10 X, 11 Y.
    unsigned keysHeld = 0, keysPressed = 0;
};

// 02065ec0: an object's touch box - all its touch areas together, else its
// sequence's box - relative to its place.
std::array<int, 4> touchBox(const Object& o);
// An object's sequence by name or index, or null.
const adventure3::Animation::Sequence* sequenceOf(const Object& o, const Value& which);
// 02066114: shows a sequence; flags bit 0 paused, bit 1 once (hold at the
// end), bit 2 keep playing if it is already shown.
void play(Object& o, const adventure3::Animation::Sequence* s, int flags);

class Scene;

// A puzzle engine's additions to a scene (the special puzzle screens are
// the common one with these hooks overridden): script commands 1280..1293
// (scene vtable 0x58 + 4k, forwarded to 0x104 + 4k; the common engine's are
// empty, 020f1824) and a step every frame (vtable 0xc0).
struct SceneEngine {
    virtual ~SceneEngine() = default;
    virtual void command(Scene& scene, int hook, Call& call) = 0;

    // Before the objects take the pen and move (vtable 0xc0).
    virtual void frame(Scene&) {}

    // Right after the objects took the pen, before the scripts run (the end
    // of 020f3788, vtable 0xe4 of the minigame loops).
    virtual void afterInput(Scene&) {}

    // After the common update (vtable 0xe4).
    virtual void update(Scene&) {}

    // Drawing over a screen's objects (vtable 0xf8).
    virtual void draw(const Scene&, Surface&, Screen) const {}
};

class Scene {
    std::unique_ptr<Machine> machine_;

public:
    // What the scene needs from the game around it.
    using Input = SceneInput;

    // A movable piece as the puzzle screen remembers it when it is left
    // (0211ebb4) and puts back when it returns (0211ea34): objects whose
    // kind tag is 3, 6 or 7, or touch objects with tag 0.
    struct Piece {
        std::string name, link;
        int kindTag = 0, value = 0, rotation = 0, x = 0, y = 0;
    };

    struct Host {
        // The current puzzle's progress (1910, 1912) and catalogue row (1620).
        std::function<int()> puzzleState;
        std::function<int()> puzzleFailures;
        // The hints opened for the current puzzle are set (1916) / one more
        // (1917).
        std::function<void(int)> setPuzzleHints;
        std::function<int()> puzzleHints;
        // Puzzle strings (1540): 1 picture a, 2 picture b, 3 statement,
        // 4 correct, 5 wrong, 6..9 hints.
        std::function<std::string(int)> puzzleText;
        // Where the pieces are remembered (1294 / 1295); none: not kept.
        std::vector<Piece>* pieces = nullptr;
        // The game's sound (48xx); none: silent.
        Adventure3Sound* sound = nullptr;
        // Where memo canvases keep their ink between scenes (4760..4772);
        // none: not kept.
        std::map<std::string, Surface>* memos = nullptr;
    };

    // `state` holds the game's flags and variables; without it the scene
    // keeps its own.
    Scene(adventure3::Assets& assets, const GameText& text, Host host, GameState* state = nullptr);
    ~Scene();

    Machine& machine;

    // Loads a script file (the game's path, "evt/prg/lt3_nazo_start.cbin").
    void load(const std::string& path);
    // Starts the scene's own thread on `section` (at `label`, if given);
    // thread variables from 200.
    Thread& start(const std::string& section, std::span<const Value> variables = {},
                  const std::string& label = {});

    // Starts the scene's thread over on `section`, keeping its variables
    // (020f19d0 -> 020519d4); a new thread if there is none.
    Thread& restart(const std::string& section, const std::string& label = {});

    Thread* mainThread() { return main_.get(); }

    const Thread* mainThread() const { return main_.get(); }

    // One frame: input goes to the objects, the scene's thread runs, then
    // each object runs its thread and moves by one frame.
    void frame(const Input& input = SceneInput{});

    const Input& input() const { return input_; }

    // Both screens, upper first (256x384).
    Surface render() const;
    bool running() const;

    // --- Services, for the commands ----------------------------------------
    Object* find(const std::string& name);
    Object* findById(int id);
    Object* owner(const Thread& thread);
    Object* create(Object::Kind kind, const std::string& name, const std::string& resource,
                   bool needsResource = true);
    void remove(const std::string& name);
    void setLayer(Object& o, int layer);
    std::vector<Object*> drawOrder() const;
    // An object's position (fixed point): the linked point's place, then
    // position and offset (02065638); and rounded to pixels (020656b4).
    std::array<int, 2> absolute(const Object& o) const;
    std::array<int, 2> pixel(const Object& o) const;
    // The turn drawn: its own plus that of the object carrying the point it
    // is linked to (020b2bf4 / 020659d4).
    int totalRotation(const Object& o, int depth = 0) const;
    // A load was requested this frame (3510/3610; condition 3000).
    bool loading = false;
    SceneEngine* engine = nullptr;

    // Whether an object took the pen this frame (020f3788: bit 0 of the
    // scene's flags).
    bool penTaken() const { return penTaken_; }

    // Where the pen at (x, y) touches `o`: its touch area's id, 3 on its
    // shown pixels, -1 nowhere (research tools).
    int touchAt(const Object& o, int x, int y) const { return hit(o, x, y); }

    // The pen place the objects see: the frame before's, a press's own
    // (input record +0xec).
    std::array<int, 2> pen() const { return lastPen_; }

    // Where the pen came down, and its smoothed place (see frame()).
    std::array<int, 2> pressPen() const { return pressPen_; }

    std::array<int, 2> smoothPen() const { return smoothPen_; }

    // Input for the objects off (020f3788: a solved puzzle stops play).
    bool inputBlocked = false;
    // Walls around an object: the id-4 touch areas of every other shown
    // object, mirrored and turned with it, in pixels (020b3444).
    std::vector<std::array<int, 4>> wallsAround(const Object& o) const;
    // The touch areas with `id` of every shown object (but `except`),
    // mirrored and turned with it, in pixels (020b3444, 020bd048).
    std::vector<std::array<int, 4>> areasWith(int id, const Object* except = nullptr) const;
    // An object's touch box mirrored and turned with it (02065f08).
    std::array<int, 4> turnedTouchBox(const Object& o) const;
    // Whether an object's own walls placed at `at` (pixels) hit any of
    // `walls` (020b0048).
    bool hits(const Object& o, std::array<int, 2> at,
              const std::vector<std::array<int, 4>>& walls) const;
    // The physics step (020b1ff4, first half) and its switch (4005).
    bool physics = false;
    void physicsStep();
    // Turns to the step nearest `angle` (020424d0) / sets a step (020422c8).
    void turnToward(Object& o, int angle, bool lock = false);
    void setStep(Object& o, int step);
    int stepFor(const Object& o, int angle) const;
    // Snaps the position to the grid cell nearest `position` (02042148).
    void snap(Object& o, std::array<int, 2> position);
    // Moves a dragged object to `position` (fixed point); on a grid it snaps
    // to the cell there when that changes (02065994).
    void dragTo(Object& o, std::array<int, 2> position);

    // Anchor points (34xx; ov6 020d3ab0, 020d5114, 020d579c): named places,
    // free or attached to an object (then they move and turn with it).
    struct Point {
        std::array<int, 2> local{}; // fixed point
        std::string attachedTo;     // object name, empty: free
        int serial = 0;             // creation order (the game keeps a list)
    };

    std::map<std::string, Point> points;
    Point& point(const std::string& name); // finds or creates
    // The points in creation order.
    std::vector<std::pair<std::string, const Point*>> pointList() const;
    std::optional<std::array<int, 2>> pointPosition(const std::string& name) const;
    // The point nearest `at` (fixed point; Manhattan distance below 512
    // pixels, 0x200000), skipping points that would make `object` carry
    // itself (020d3e68 / 020d419c), of the kinds in `mask` (bit 0 free, bit
    // 1 attached), with `freeOnly` only points nothing is linked to; "" if
    // none.
    std::string nearestPoint(std::array<int, 2> at, unsigned mask = ~0u,
                             const std::string& object = {}, bool freeOnly = false);
    // Links an object to a point, keeping where it is (020ad884 / 020ad7b4).
    void linkTo(Object& o, const std::string& point);
    // Whether putting `object` on `point` would make it carry itself
    // (020d3bb8).
    bool carries(const std::string& point, const std::string& object, int depth = 0) const;

    // Scene variables (1130/1131; 020f1a7c / 020f1ab8): the variables
    // 200..215 of the scene's own thread, null outside 0..15 or before it
    // started.
    Value* sceneVariable(int index) {
        return main_ && index >= 0 && index < 16 ? &main_->variables[size_t(index)] : nullptr;
    }

    std::vector<Object*> creationOrder();

    Object* message(const std::string& name);
    Object* canvas(const std::string& name);
    Object* createCanvas(const std::string& name, bool circling = false);
    // A circling canvas: wiped (020cb7f8) / the pen (020cb5a8, 020cb650).
    void wipeCircle(Object& canvas);
    void circleInput(Object& canvas, bool& available);
    void drawCircleSegment(Object& canvas, const std::array<int, 4>& segment);
    void circleEnded(Object& canvas);
    // Sets a canvas up for writing with recognition mode `mode` (4750).
    void setWritingMode(Object& canvas, int mode);
    // Starts a memo pad (4750; 020cc264): its script loaded, a thread with
    // `arguments` as variables 200.., on MEMO_BEGIN.
    void startMemo(Object& pad, std::span<const Value> arguments);
    // A memo pad's pen (0206c9d8 while MEMO_MAIN runs).
    void memoInput(Object& pad, bool& available);
    // Shows a canvas's reading on the sprite of the same name.
    void showReading(Object& canvas);
    Object* createMessage(const std::string& name, Screen screen, int capacity, int font);
    void removeMessage(const std::string& name);

    struct Resource {
        Screen screen = Screen::Lower;
        const adventure3::Picture* picture = nullptr;
        const adventure3::Animation* animation = nullptr;
    };

    void loadPicture(const std::string& name, int bank, const std::string& path);
    void loadAnimation(const std::string& name, int bank, const std::string& path);
    const Resource* resource(const std::string& name) const;
    void freeResource(const std::string& name);
    void pushScope();
    void popScope();
    // 3000 (020f1b3c -> 020d947c): what the current level created goes,
    // the level stays.
    void clearLevel();
    // 3020 / 3021 (020d916c / 020d9180): every object's switches saved /
    // restored, without a new level.
    void saveSwitches();
    void restoreSwitches();
    // 3500 / 3600 (020f220c / 020f2368): every picture / animation freed.
    void freeAll(bool pictures);

    int level() const { return level_; }

    // Starts a script object's thread (4018); variables from 200.
    Thread& objectThread(Object& o, std::span<const Value> variables);
    // Runs a section on a script object's thread, replacing its top frame
    // (020adbdc).
    void callOn(Object& o, const std::string& section);
    // 3023: input on or off for every object (020d91b4).
    void setAllInput(bool on);
    // The message palette (index -> RGB555; 4642's indices): 1 the text,
    // 3 <CR> red - measured on the ROM's pictures; others hypotheses.
    std::vector<std::uint16_t> messagePalette;
    // 2022 / 2021: hardware layer priorities per screen (02053c28; not yet
    // used by the renderer).
    std::array<std::array<int, 4>, 2> layerPriority{};

    // The stage: the lower screen's background - a map picture and its
    // animated parts (rc/lt3_map/m%04d.lbin: 5220 picture, 5230 animation,
    // 5232 {x, y, sequence}; 020c2398). -1: none. It and the lower screen
    // picture (4501 0) share the background: the one set last shows
    // (event 2000 replaces its letter picture with the bus stage).
    struct Stage {
        int id = -1;
        const adventure3::Picture* picture = nullptr;
        std::vector<Object> parts;
        bool shown = false;
        // 4540 / 4541: the stage's offset in pixels, fixed point (stage
        // object +0x84; the sense on screen is a hypothesis).
        Tween<2> scroll;
    } stage;

    void setStage(int map);
    // A screen's picture (4501 / 4502; 020c2964): it takes the screen's
    // background over - the stage (or guide) there is forgotten and the
    // screen's filter and offset start afresh (020c2130 -> 0204ba28,
    // 020c2e54). Null clears it.
    void setScreenPicture(int screen, const adventure3::Picture* picture);
    // 020c2130 for one screen: no stage or guide, plain filter, no offset.
    void resetBackground(int screen);

    // The guide: the upper screen's background - the guide map
    // map/g%02d.cimg (the stage manager's second stage, 020c2398 with
    // screen 1). Like the stage it shares the screen with 4501 1: the one
    // set last shows. Its two messages (place and goal) are made by the
    // story (4560).
    struct Guide {
        int id = -1;
        const adventure3::Picture* picture = nullptr;
        bool shown = false;
    } guide;

    // 4553: shows or hides the stage parts playing `sequence` (020c2cf0:
    // the part's flag bit 0 hides it).
    void showStagePart(const std::string& sequence, bool shown);

    // Each screen's background filter (4510..4531; the graphics system's
    // per-screen record at +0x64, 0204bc48 / 0204bd58, applied by 02048ea4
    // to the first 240 colours of the screen's background picture - the
    // characters and other sprites are not filtered). Values: a colour
    // matrix (out = colour x matrix, rows of three, 1.0 = 4096) and a level
    // -1..1 that then pulls every colour towards black or white.
    struct BackgroundFilter {
        static constexpr int kLevel = 9;
        Tween<10> record;

        BackgroundFilter() { record.set(identity()); }

        static Tween<10>::V identity() {
            return {fx::kOne, 0, 0, 0, fx::kOne, 0, 0, 0, fx::kOne, 0};
        }

        bool plain() const { return record.value == identity(); }

        std::uint16_t apply(std::uint16_t colour) const;
    };

    std::array<BackgroundFilter, 2> backgroundFilter;
    // 3620..3632: the same filter on an animation resource's palette (the
    // resource's record, 0204d980 / 0204d988) - events darken or tint a
    // character this way. Keyed by the resource's animation.
    std::map<const adventure3::Animation*, BackgroundFilter> tints;

    // The event's characters in order (4012; 4090 / 4092).
    std::vector<std::string> cast;

    std::array<Tween<1>, 2> brightness; // -4096 black .. 4096 white
    // The blend level of each screen's six layers (BG0..3, objects, 5),
    // 0..4096 (2023 / 2024; 02053ad4, 02053b30). Objects are drawn at the
    // level of layer 4 (hypothesis: the game's sprites are the objects
    // layer).
    std::array<std::array<Tween<1>, 6>, 2> layerAlpha;
    std::array<std::uint16_t, 2> backdrop{}; // RGB555
    std::array<const adventure3::Picture*, 2> screenPicture{};

    std::map<std::string, std::string> strings; // 32xx string buffers
    // Text tables (3310): id -> two texts.
    // Each id keeps all its rows in file order; 3321's index picks one
    // (0210cad8: the id's first row plus the index).
    std::map<std::string, std::map<int, std::vector<std::array<std::string, 2>>>> textTables;
    void loadTextTable(const std::string& name, int file, int part);

    adventure3::Assets& assets;
    const GameText& text;
    Host host;
    // The layer new objects get (the object constructor, 02064e7c).
    int defaultLayer = 3;

private:
    std::list<Object> objects_;
    std::vector<Object*> order_; // draw list: back (high layers) to front
    std::map<std::string, Resource> resources_;
    std::vector<std::vector<std::string>> scopes_; // 3010/3011
    int level_ = 0;
    bool updating_ = false;
    void erase(Object* o);
    Input input_;
    bool penTaken_ = false;
    std::array<int, 2> pressPen_{}, smoothPen_{}, smoothHeld_{};
    std::array<int, 2> lastPen_{};
    void dispatchInput();
    std::unique_ptr<handwriting::Recognizer> recognizer_;
    std::map<std::uint16_t, std::uint8_t> characterCodes_;
    std::set<std::string> loadedScripts_;
    bool buttonInput(Object& o, bool available);
    void memoStep(Object& pad);
    void grabFor(Object& o, bool turning);
    void dragged(Object& o);
    void turned(Object& o);
    void slid(Object& o);
    int hit(const Object& o, int x, int y) const;
    std::unique_ptr<Thread> main_;
    std::vector<std::unique_ptr<Thread>> threads_;
    int nextId_ = 1;
};

// Registers the scene's commands and conditions on its machine (objects
// 40xx-42xx, messages 46xx, screens 20xx/45xx, resources 3xxx, puzzle data
// 1540/1620/19xx, sound 48xx as a documented omission). See a3_lscr_scene.cpp.
void installScene(Scene& scene);

} // namespace picarat::lscr
