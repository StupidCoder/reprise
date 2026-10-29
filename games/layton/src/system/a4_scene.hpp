#pragma once
// The fourth adventure's scene: what the game's script services act on.
//
// How the game shows things
// -------------------------
// Scripts load named resources onto a screen - pictures (46xx) and
// animation packs (47xx) - and create named objects (50xx..54xx) that show
// one of them: a picture, or an animation playing a sequence ("touch",
// "hint_off0") or laying out one image per character of a text (puzzle
// numbers). Objects have a position and an offset that tween, rotation,
// scale and alpha, eight values and a script thread of their own: buttons
// run blocks of the loaded scripts when they are touched (normal, select,
// click...). Each screen draws its objects in the order of their priority
// (0..13, later ones on top; equal priorities in the order they were
// placed). Message boxes (60xx) lay out text with the game's font; each
// screen has a master brightness that fades.
//
// Screen 0 is the lower (touch) screen, screen 1 the upper one.
//
// Reconstructed from the engine library (ov6: gnCChara 020ab2a4..020ad880,
// the animation player 02094d68..02095340, gnCScreen 0209912c..0209a4c8,
// gnCMessage 020ade78..020b0f64) and the game's managers in ov7 (objects
// +0x9440: 020dc020 create, 020dbc88 draw order, 020dd994 update; lt4CChara
// 020db8e4). See .analysis/adventure4/docs/scene.md.
#include "graphics/a4_assets.hpp"
#include "graphics/graphics.hpp"
#include "puzzles/handwriting.hpp"
#include "system/a4_gss.hpp"
#include "system/a4_tween.hpp"
#include "video/movie.hpp"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace picarat::adventure4 {

// The name hash objects and resources are found by (CRC-32, ITCM 01ff86a0).
std::uint32_t nameHash(std::string_view name);

// The text colours (fnt/color.lp, 16 entries).
const std::vector<std::uint16_t>& textColours(Assets& assets);

// A game text as plain Windows-1252: {xy} escapes replaced (table ov6
// 020b4790) and <..> marks left out.
std::string plainText(Assets& assets, std::string_view text);

// A picture loaded onto a screen by name (4602).
struct ImageResource {
    std::string name;
    int screen = 0;
    const Picture* picture = nullptr;
    int level = 0; // the scope level it was loaded at (+0x10, 4021)
};

// A screen's colour treatment (gnCScreen +0x80, a 4x4 matrix in 4.12):
// the colour columns of red, green and blue and a constant colour,
// c' = M c + k (1.0 = full intensity).
struct ColourMatrix {
    std::array<std::int32_t, 9> m{4096, 0, 0, 0, 4096, 0, 0, 0, 4096}; // column by column
    std::array<std::int32_t, 3> k{};

    bool identity() const { return *this == ColourMatrix{}; }

    bool operator==(const ColourMatrix&) const = default;

    // ITCM 01ff9300: gains on Y, I and Q (the game's YIQ matrices, built
    // at start by ov6 020b4dxx into 01ff86e0 / 01ff871c), then a blend of
    // `s` toward the colour `target` (lt4_def 2101 records).
    static ColourMatrix of(std::array<std::int32_t, 3> gains, std::array<std::int32_t, 3> target,
                           std::int32_t s);
};

// An animation pack loaded onto a screen by name (4702).
struct AnimationResource {
    std::string name;
    int screen = 0;
    const Animation* animation = nullptr;
    int level = 0;
    // A colour treatment of everything drawn from the pack (4730..4732,
    // ov6 02093704 / 02093dbc), changing linearly over frames.
    ColourMatrix colour, colourFrom, colourTo;
    std::int32_t colourTime = 0, colourFrames = 0;
};

// Plays an animation pack's sequences (ov6 02094d68.., an object's +0x270).
class AnimationPlayer {
public:
    // Play flags (+0x1c).
    enum : unsigned {
        Paused = 2,   // time stands still
        Restart = 4,  // restart even when the sequence is already playing
        Hold = 8,     // stop at the end instead of looping
        NoChain = 16, // do not continue with the sequence's next one
    };

    // 02094de8: shows this pack (nothing playing yet).
    void attach(const AnimationResource* resource);

    const AnimationResource* resource() const { return resource_; }

    // 02094f18: plays a sequence (by pointer; null fails).
    bool play(const Animation::Sequence* sequence, unsigned flags);
    // 02095088: shows `text` as one image per character (images named
    // after the character), `step` pixels apart, `line` per new line.
    bool showText(const std::string& text, int step, int line, unsigned flags);
    // 02095340: advances by `elapsed` (20.12 frames).
    void update(std::int32_t elapsed);

    const Animation::Sequence* sequence() const { return sequence_; }

    // The step showing at the current time (02091e44).
    const Animation::Step* step() const;

    std::int32_t time() const { return time_; }

    unsigned loops() const { return loops_; }

    bool finished() const; // a held sequence at its end

    // 02094e94 (5130): played through at least once, or flag bit 0.
    bool done() const { return loops_ >= 1 || (flags_ & 1u); }

    bool textMode() const { return !glyphs_.empty() || textShown_; }

    struct Glyph {
        const Animation::Image* image = nullptr;
        int x = 0, y = 0;
    };

    const std::vector<Glyph>& glyphs() const { return glyphs_; }

    std::int32_t speed = kOne; // +0xc

private:
    const AnimationResource* resource_ = nullptr;
    const Animation::Sequence* sequence_ = nullptr; // +0x14
    const Animation::Sequence* next_ = nullptr;     // +0x18
    unsigned nextFlags_ = 0;
    std::int32_t time_ = 0; // +8
    unsigned loops_ = 0;    // +0x10 low 24 bits
    unsigned flags_ = 0;    // +0x1c
    bool starred_ = false;  // +0x10 bit 25: the sequence name holds '*'
    bool textShown_ = false;
    std::vector<Glyph> glyphs_; // +0x20.., count +0xc0
};

class Scene;

// The four on/off switches every scene item has (ov6 020a94c0..020a96bc,
// words +8, +0xc, +0x10, +0x14). Each is a stack: 4014 pushes all of them
// (the current state stays), 4015 pops them back.
class Switches {
public:
    enum Kind {
        Master, // +8: everything (5011, 4010)
        Touch,  // +0xc: answers touches (5012, 4011)
        Update, // +0x10: tweens, animation and thread run (4012)
        Draw,   // +0x14: drawn (5014, 4013)
    };

    bool on(Kind k) const { return !(off_[k] & 1u); }

    // The combined states the game asks for (gnCChara vtable +0x20..+0x28).
    bool active(Kind k) const { return on(Master) && on(k); }

    void set(Kind k, bool on) { off_[k] = (off_[k] & ~1u) | (on ? 0u : 1u); }

    // 020a9550: every word shifts up keeping its current bit.
    void push() {
        for (auto& w : off_) {
            w = (w << 1) | (w & 1u);
        }
    }

    // 020a95d0..020a9684: the saved bit comes back; returns it per switch.
    std::array<bool, 4> popped() {
        std::array<bool, 4> saved{};
        for (std::size_t k = 0; k < 4; ++k) {
            saved[k] = !(off_[k] & 2u);
            off_[k] = ((off_[k] >> 1) & ~1u) | (off_[k] & 1u);
        }
        return saved;
    }

private:
    std::array<std::uint32_t, 4> off_{}; // bit 0: off now; higher bits: pushed
};

// A scripted object (gnCChara with lt4CChara's script thread).
class Chara {
public:
    // +0x264: 0 none (not set up: no updates, no touches), 1 a screen only
    // (5121), 2 a picture, 3 an animation, 4 empty (5124); kinds 1 and 4
    // are touched through their touch areas (020ab7dc).
    enum class Visual { None, Screen, Image, Animation, Empty };

    Chara(Scene& scene, std::string name, int kind, const gss::Program* program,
          std::span<const gss::Value> values);

    const std::string name;
    const std::uint32_t hash;
    int kind; // the second operand of 5002 (+0x3c4); the slide loop recolours gates

    // What the object shows.
    Visual visual = Visual::None;
    const ImageResource* image = nullptr;
    AnimationPlayer animation;
    int screen = -1;   // +0x3bc
    int priority = 13; // +0x3c0 (020db328 starts it at 13); 14 and up: not drawn
    Switches switches;
    // lt4CChara 020db38c/020db3e4 (vtable +0x10..+0x1c): setting a switch;
    // an object that could not be touched before and can now restarts its
    // normal block.
    void setSwitch(Switches::Kind k, bool on);
    // lt4CChara 020db43c: shown, active, a button (touchKind) and not in
    // the middle of a click.
    bool touchable() const;

    TweenPoint position; // +0x18
    TweenPoint offset;   // +0x50
    TweenValue alpha;    // +0x190 (0..4096)
    TweenPoint scale;    // +0xd8 (4096 = 1)
    TweenValue rotation; // +0xa4
    // +0x150: how far the object shakes (5160..5162); +0x188 this frame's
    // shake, the amount times a random -1..1 per axis (020acf00).
    TweenPoint shake;
    std::array<std::int32_t, 2> shaking{};

    // +0x338..+0x344 (5181, 020aca3c): a box (sx, sy, w, h) of the film on
    // the screen's texture (a film opened with 9100's flag) drawn at (x, y)
    // from the object, after its own picture (ov6 020a1a24).
    struct FilmPart {
        bool on = false;
        int x = 0, y = 0, sx = 0, sy = 0, w = 0, h = 0;
    } filmPart;

    // Values 0.. (the thread's registers; 5108/5109).
    gss::Thread thread;
    const gss::Program* program = nullptr; // +0x354
    std::array<std::string, 6> callbacks;  // block names per slot (+0x3b0)
    int state = -1;                        // +0x34c (-1: none yet)
    // +0x348: what kind of button the object is (5111: 0, 5112: 1, 5113: 2,
    // 5114: 3); -1 none.
    int touchKind = -1;

    // The place the object sits on (5171; +0x350 its name hash, 0 none) and
    // the place's transform it is drawn through: matrix +0x118 (a, b, c, d
    // as x' = a x + c y, y' = b x + d y) and translation +0x110.
    std::uint32_t placeHash = 0;
    std::array<std::int32_t, 4> parentMatrix{kOne, 0, 0, kOne};
    std::array<std::int32_t, 2> parentTranslation{};
    // +0x128: where the object is in the screen (020acfa4, each update):
    // parent transform of position + offset + shake.
    std::array<std::int32_t, 2> world{};
    void updateWorld();
    // +0x130: how the object's pixels map to the screen around `world`:
    // the place's matrix times rotation and scale (020ad220, 020ad37c), as
    // (a, b, c, d) with x' = a x + c y, y' = b x + d y.
    std::array<std::int32_t, 4> matrix() const;

    // Turning in steps (rotation +0xa4, ov6 020838d0..02083a50): `count`
    // steps a turn, each `angle` apart; turns take `frames` (5390).
    struct Steps {
        int count = 0, step = 0;
        std::int32_t frames = 0, angle = 0;
    } steps;

    void setSteps(int count, std::int32_t frames);
    // 020836e4 (5392): straight to a step.
    bool setStep(int step);
    // 02083948: the step nearest an angle.
    int stepAt(std::int32_t angle) const;
    // 02083a50 (5393): turns to the step nearest an angle.
    bool turnTo(std::int32_t angle, int curve, bool lock);

    // ov6 020abac4: the drawing position in pixels (rounded).
    std::array<int, 2> pixelPosition() const;
    // ov6 020ac338: the bounds (x1, y1, x2, y2) of what it shows: the touch
    // areas when there are any, else the playing sequence's box (020ac5f8),
    // else nothing.
    Animation::Box bounds() const;
    std::vector<std::array<int, 5>> touchAreas; // +0x1b8: x, y, w, h, id (5152)
    // ov6 020aca8c: what a touch at a screen pixel hits: the last touch
    // area holding it (its id), else 0 when it lands on a drawn pixel of
    // the playing animation; -1 for a miss or an object that cannot be
    // touched.
    int hit(int x, int y) const;
    // lt4CChara update 020db8e4 (thread, state) + gnCChara 020accf4.
    void update(std::int32_t elapsed);
    // Starts the block for a state (020db5ac); falls back to state 0.
    bool enterState(int s);

    bool deleted = false;
    int level = 0; // the scope level it was made at
    Scene& scene;
};

// A message box (gnCMessage, lt4CMessage; manager at game +0x94e4): text
// laid out with the game's font from a position.
struct Message {
    std::string name;
    int screen = 0;
    int width = 256;     // 6002's third operand (role not checked)
    int font = 0;        // the game's font slot (6002's fourth operand, 020d23d8)
    TweenPoint position; // +0x48
    TweenPoint offset;   // +0x80 (6212/6215)
    Switches switches;
    int setting11c = -1; // +0x11c (6133; *role not identified*)
    // Boxes (x1, y1, x2, y2) of the screen's text layer cleared since the
    // text was drawn (4290); the text shows there again once redrawn (6230,
    // or new text).
    std::vector<std::array<int, 4>> erased;
    bool fresh = false; // text set this frame, not drawn into the layer yet
    // +0xd4 (6140): the box (x1, y1, x2, y2) the text shows in; none when
    // x2 <= x1.
    std::array<int, 4> clip{};
    int letterSpacing = 0; // +0xfc (6141)
    int lineSpacing = 0;   // +0xfd
    int rubyOffset = 0;    // 6141's third value (negative: no ruby room)
    int textColour = 1;    // +0x44 (6112)
    int edgeColour = 1;    // +0x45 (6113)
    unsigned flags = 0;    // +0x2c: bit 0 (6122), bit 1 (6123)
    std::string text;      // set by 6191
    int level = 0;
    // 6131 (020b03b0): while the text appears, a sound (+0x108) every so
    // many frames (+0x10c).
    int typeSound = -1, typeSoundFrames = 0;
    std::uint32_t hash = 0;

    // The laid-out text (ov6 020ae868, records +0x30): glyphs where they
    // are drawn, and the marks that pace their appearing.
    struct Record {
        enum Type : std::uint8_t {
            Glyph = 2, // costs one step of the reveal
            Wait = 4,  // <W n> n frames (30 for <W>), <WT> until a tap (-1)
            Pause = 5, // <L s.f>: until s.f seconds after the text began
            Open = 6,  // <T>: a stretch a tap shows at once (value: length)
            Close = 7, // </T>
        };

        Type type = Glyph;
        const FontGlyph* glyph = nullptr;
        int x = 0, y = 0, colour = 0;
        int value = 0;
    };

    std::vector<Record> records;
    // How the text appears (020b03d0): `speed` glyphs a frame (20.12, 6142)
    // up to the next mark; a tap (6193) moves on from a wait or finishes a
    // <T> stretch.
    std::int32_t speed = kOne;  // +0xf0
    std::int32_t cursor = 0;    // +0xf4: glyphs shown, 20.12
    std::size_t revealed = 0;   // +0xe8: records passed
    std::int32_t clock = 0;     // +0xf8: time since the text began
    int waitFrames = 0;         // +0x100
    int pauseUntil = 0;         // +0x104 (frames)
    std::size_t bracketEnd = 0; // +0xec
    int soundCounter = 0;       // +0x110
    bool instant = false;       // +0x28 bit 5: all at once (6191, 6192)
    bool waitAtEnd = false;     // +0x28 bit 4: a tap ends the text (6191)
    bool waiting = false;       // +0x28 bit 2: at a wait for a tap (6102)
    bool skip = false;          // +0x28 bit 1: the tap (6193)
    bool inBracket = false;     // +0x28 bit 3
    bool skipping = false;      // +0x29 bit 3: a tap finished a stretch

    // 020ae44c (6100): all shown and not waiting.
    bool done() const { return !waiting && revealed >= records.size(); }

    // 020b03d0: one frame of the text appearing; the typing sound to play
    // this frame, or -1.
    int update(std::int32_t elapsed);
    // 020afe20 (6190): no text.
    void clear();
};

// A place (ov7 020d6ef4; places manager +0x9624, named list +0x9660):
// a named point objects can sit on (5171) and the game finds by distance
// (8900).
struct Place {
    std::string name;
    std::uint32_t hash = 0;
    std::uint32_t parentHash = 0;                         // +0x1c (8002's fourth operand)
    std::array<std::int32_t, 2> local{};                  // +0x20
    std::array<std::int32_t, 4> matrix{kOne, 0, 0, kOne}; // +0x28
    std::array<std::int32_t, 2> position{};               // +0x38: where it is
    Switches switches;
    int level = 0;
};

// A drawing canvas (gnCPaint ov6 020b1870.., lt4CPaint ov7; manager
// +0x9584): the pen draws into its screen's drawing layer (BG slot 2,
// 020d2710) within an area. Circling puzzles, the memo and written answers
// use it.
struct Paint {
    std::string name;
    std::uint32_t hash = 0;
    int screen = 0;
    int level = 0;
    Switches switches;
    // +0x28 (7112): 1 a dot, 2 a line, 3 a box, 4 the pen, 5 a fill, 8 the
    // pen kept for undoing, 9 replayed strokes; 0x10 and up also keeps the
    // strokes for reading handwriting.
    int mode = 0;
    std::array<int, 4> area{};  // +0x30: x1, y1, x2, y2 (7113)
    std::array<int, 4> drawn{}; // +0x38: what the pen touched (7130)
    int colour = 0;             // +0x40 (7121)
    std::uint16_t ink = 0;      // +0x44: the colour's RGB
    std::int32_t scale = kOne;  // +0x48 (7115)
    std::int32_t width = 0;     // +0x4c: the pen (7114)
    // +0x50, +0x54, +0x58: where the pen started, is and was (area
    // coordinates).
    std::array<int, 2> start{}, current{}, previous{};
    bool pressed = false;      // +0x114 bit 0
    bool dirty = false;        // +0x114 bit 1: draw this frame
    bool clearOnPress = false; // +0x114 bit 2
    // Strokes for reading handwriting (mode 0x10..; +0x5c).
    std::vector<std::vector<std::array<int, 2>>> strokes;
    std::string answer; // +0x120: what was read (7141)

    // 020b285c: whether a screen point is in the area.
    bool hit(int x, int y) const;

    bool touchable() const { return switches.active(Switches::Touch); } // 020d838c

    void press(int x, int y); // 020b2144
    void move(int x, int y);  // 020b2250
    void release();           // 020b2380
};

// A screen: brightness and its background picture (gnCScreen).
struct Screen {
    TweenValue brightness; // +0x6ac: -4096 (black) .. 4096 (white)
    // +0x6e4: how strongly the objects (the OBJ layer) show over the
    // screen's background, 0..4096 (020997f4: blend EVA = value·16 >> 12,
    // EVB = 16 - EVA); loops set it back to 4096 when they resume.
    TweenValue blend;
    // The colour treatment and its change (0209a2cc: linear over frames).
    ColourMatrix colour, colourFrom, colourTo;
    std::int32_t colourTime = 0, colourFrames = 0;
    void setColour(const ColourMatrix& c);
    void moveColour(const ColourMatrix& c, std::int32_t frames);

    bool colourMoving() const { return colourTime < colourFrames; }

    // The screen's shake (4241 set, 4242 move: game +0x5ba0 screens,
    // +0x10f4 per screen, 0209f0cc / 0209f0fc; *hypothesis*: like an
    // object's shake, the whole picture moves by the amount times a random
    // -1..1 per axis each frame).
    TweenPoint shake;
    std::array<int, 2> shaking{};
    const Picture* background = nullptr; // 4200
    std::uint16_t backdrop = 0;          // 4201
    // The drawing layer paints draw into (BG slot 2 as a bitmap): RGB555
    // with bit 15 for drawn pixels.
    std::vector<std::uint16_t> drawing = std::vector<std::uint16_t>(256 * 192, 0);
};

// The scene: resources, objects, screens and the touch input the services
// use. One per loop (the game keeps them in its game object).
class Scene {
public:
    explicit Scene(Assets& assets);
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Assets& assets;
    std::array<Screen, 2> screens;
    // The screen shown on top (+0x5ba0 +0x2fbc, 4131; the hardware's
    // display swap); the other one takes the pen (0209ee14).
    int upper = 1;

    int touchScreen() const { return 1 - upper; }

    // Scripts loaded for the scene, newest first in block lookups.
    const gss::Program& loadScript(const std::string& path);

    const gss::Program* newestScript() const {
        return scripts_.empty() ? nullptr : scripts_.back().get();
    }

    // The film process (ov10 lt4CMovieBGProcess; game +0xaacc..+0xaace).
    // 9100 asks for a film; the frame step opens it (020ccdac); the
    // scripts wait while it opens (state 0), start it (9102: ready 1 ->
    // 2), watch it play (3) and stop it (9101: -> 6, then it closes).
    // The frames come at the film's own rate; it shows full screen on the
    // main engine (screen 0) over the background.
    struct Film {
        int request = -1; // +0xaacc: the film asked for, -1 none
        // +0xaacd: 9100's second operand (process +0x37c bit 3): the film
        // loops (ov10 020efa74) and plays as a texture the objects draw
        // parts of (5181; the videos page's thumbnails) instead of over the
        // screen (*hypothesis* for the second part: only that page uses it).
        bool flag = false;
        int state = -1; // the process's +0xc; -1: no process (+0xaace < 0)
        int number = -1;
        std::shared_ptr<MovieFrames> frames;
        std::uint64_t clock = 0;
        // The frame due (+0x390, 9120): a timer at the film's rate counts it
        // up (020ef724); the port counts the game's 60 frames a second.
        int frame = -1;
        int shown = 0; // +0x394 (9121): frames shown so far
        // +0x384 (9122): the due frame's time in 60ths of a second,
        // frame * 60 / rate in floats (020ef760..020ef77c); the movie
        // script times the subtitles by it.
        int time = -1;
    } film;

    // 020ccdac and the process: one frame.
    void updateFilm();

    // The room on the lower screen (game +0x9786..; 4300, ov7 020cf940):
    // its picture (map/mNNNNv.ci) as the screen's background, its pack
    // (map/mNNNNv.ca) loaded as "lt4_map" outside the loops' levels, and a
    // map script (res/map/mNNNN.cq). Once its files are in (020cf080 in the
    // frame step) the pack's first sequences (at most 16) play as the
    // room's parts and the script's MapMain runs through once; the parts
    // can be switched off and faded (4320..4323).
    struct Room {
        int shown = -1;     // +0x9786: the room (lt4_def 2131 id), -1 none
        bool ready = false; // +0x9788
        std::string script; // the map script, empty for none

        struct Part {
            AnimationPlayer player;
            bool hidden = false, held = false; // +0xf4 bits 0 and 1
            TweenValue alpha;                  // +0xcc
        };

        std::vector<std::unique_ptr<Part>> parts; // +0x978c, 0xf8 each
    } room;

    // 4300: shows a room (lt4_def 2131 {id, name, map, variant, script?,
    // ...}); false when there is none.
    bool showRoom(int id, int map, char variant, bool withScript);
    // 020cefe8: screen 0 loses its room (also when 4200/4201 change it).
    void resetRoom();
    // The map area on the upper screen (4400, ov7 020cfe54; +0xa728).
    int areaShown = -1;
    // 020cfd64: screen 1 loses its area map.
    void resetArea();
    bool removeMessage(std::string_view name);
    // 020cf080: sets the room up once its files are in.
    void updateRoom();
    Room::Part* roomPart(std::string_view sequence);

    // 4600 / 4700 / 5000 / 6000 / 7000 (020d0d64, 020d147c, 020d1860,
    // 020d2508, 020d28a4): every picture, animation, object, message or
    // paint goes, whatever its level.
    void removeAllImages();
    void removeAllAnimations();
    void removeAllObjects();
    void removeAllMessages();
    void removeAllPaints();

    // Resources by name.
    bool loadImage(const std::string& name, int screen, const std::string& path);
    bool loadAnimation(const std::string& name, int screen, const std::string& path);
    const ImageResource* image(std::string_view name) const;
    const AnimationResource* animation(std::string_view name) const;
    AnimationResource* animation(std::string_view name);
    bool unloadImage(std::string_view name);
    bool unloadAnimation(std::string_view name);

    // Objects by name (ov7 020dc020 create, 020d1914 find).
    Chara* create(const std::string& name, int kind, std::span<const gss::Value> values);
    Chara* find(std::string_view name) const;
    Chara* find(std::uint32_t hash) const;
    // 020dd108 (a new press): the topmost object of a screen's draw order a
    // touch hits, with the area it hit.
    Chara* objectAt(int screen, int x, int y, int& area) const;
    bool remove(std::string_view name);
    // 020dbc88: moves an object into a screen's draw order (priority -1
    // keeps its priority).
    void place(Chara& object, int screen, int priority);

    // Every live object in the manager's order (name hash), or one
    // screen's.
    std::vector<Chara*> objects(int screen = -1) const;
    // 020dcfd8 / 020dd054: the index-th object of a kind / on a place.
    Chara* objectOfKind(int kind, int index) const;
    Chara* objectOnPlace(std::uint32_t placeHash, int index) const;

    // Places (020d2aac create, 020d2cb0 find, 020d782c nearest).
    Place* createPlace(const std::string& name, std::array<std::int32_t, 2> at,
                       const std::string& parent);
    Place* findPlace(std::string_view name);
    Place* findPlace(std::uint32_t hash);
    const Place* nearestPlace(std::array<std::int32_t, 2> at, std::int32_t radius) const;
    // 020d7c18 / 020d7a78: the shown places inside a box (from `at`, `size`
    // across and down): how many, and the n-th (the last found when there
    // are fewer).
    int placesInside(std::array<std::int32_t, 2> at, std::array<std::int32_t, 2> size) const;
    const Place* placeInside(std::array<std::int32_t, 2> at, std::array<std::int32_t, 2> size,
                             int n) const;

    // Paints (020d2710 create, 020d2958 find).
    Paint* createPaint(const std::string& name, int screen);
    Paint* paint(std::string_view name);
    Paint* paint(std::uint32_t hash);

    const std::vector<Paint*>& paints() const { return paintOrder_; }

    // 020e0ab8: the topmost touchable paint of a screen a new press is in.
    Paint* paintAt(int screen, int x, int y);
    // 020b2948, after the messages: each paint draws what the pen did.
    void updatePaints();
    // 020b23c8 / 020b242c: a paint's drawing and drawn box cleared.
    void clearPaint(Paint& p);
    // lt4CPaint 020dfd1c, when the pen lifts in a handwriting mode: the
    // strokes read by the recogniser (ov9 020ef1e8 → overlay 2, the
    // recogniser shared with the earlier games); the answer is the first
    // candidate's first character through dcm/dcm_en/conv_list.gu, "?"
    // when nothing was read.
    void readHandwriting(Paint& p);
    // The game's language index (game +0x18), for the recogniser.
    int language = 5;

    // Scope levels (game +0xaadc): everything made records the current
    // level; 4020 opens a level, 4021 (020ce774, 020cd654) frees what was
    // made at the current one - places, messages, objects, texts,
    // animations, pictures and scripts - and closes it.
    int level = 0;
    void freeLevel();

    // 4014/4015: every object's, message's and place's switches.
    void pushSwitches();
    void popSwitches();
    std::vector<Message*> messages(int screen = -1);

    // Message boxes by name (manager at game +0x94e4).
    Message* createMessage(const std::string& name, int screen, int width, int font);
    Message* message(std::string_view name);
    // 4290: a box of a screen's text layer is cleared (the texts drawn so
    // far lose it until they are drawn again).
    void eraseTexts(int screen, std::array<int, 4> box);
    // 020afef0 (6191): lays a text out in the message (020ae868), shown
    // at once or as it appears, waiting for a tap at its end or not.
    bool setText(Message& m, const std::string& text, bool instant, bool waitAtEnd);

    // Text lists (txt/@/NN/NN_NNNNNN.cb) loaded under a name (4802): each
    // id holds strings, each string two fields (voice, text).
    bool loadTexts(const std::string& name, int group, int number);
    bool removeTexts(std::string_view name); // 020d0470
    const std::string* text(std::string_view name, int id, int index, int field) const;
    // ov6 020a47a0: how many strings id holds (0 none); 020a4924: the id
    // at a position of the list (ids in order; -1 none).
    int textCount(std::string_view name, int id) const;
    int textIdAt(std::string_view name, int index) const;

    // A frame of the scene's own updates (ov7 020dd994, objects).
    void updateObjects(std::int32_t elapsed);
    void updateScreens(std::int32_t elapsed);
    void updateMessages(std::int32_t elapsed);

    // Draws both screens: rows 0..191 the upper screen (1), 192..383 the
    // lower (0).
    void render(Surface& out) const;

    // What a play loop draws itself through the 3D engine (game +0x5ba0;
    // ov6 020a2e08: the first step of an animation's sequence at a position,
    // tinted by the polygon colour 0209fa4c, alpha 0209fa30). Drawn under
    // the objects of the animation's screen (the loop's draw hook runs before
    // the objects are drawn, 020e2664); the loop fills the list anew every
    // frame.
    struct LoopSprite {
        const AnimationResource* animation = nullptr;
        const Animation::Sequence* sequence = nullptr;
        std::array<std::int32_t, 2> position{}; // 20.12 pixels
        std::uint16_t colour = 0x7fff;          // RGB555, modulates the texels
        unsigned alpha = 31;
        // Mirroring about the position (bit 0 x, bit 1 y; 020a2be8's sign
        // table: the step's offset and the cells flip with it).
        unsigned mirror = 0;
        // A player's current step (020a2cd0 draws an animation player);
        // null: the sequence's first step.
        const Animation::Step* step = nullptr;
    };

    std::vector<LoopSprite> loopSprites;

    // Effects the scene itself plays (the messages' typing sound), set by
    // the services when there is sound.
    std::function<void(int)> playEffect;

    // Lines and square dots a play loop draws through the 3D engine (0209fb20
    // a line of a width between two points, 020a0600 a dot), untextured in
    // one colour, on the lower screen under its objects (the loop draws
    // before the objects, 020e2664).
    struct LoopShape {
        bool dot = false;
        // 20.12 pixels: a line's ends, or a dot's corner and size.
        std::array<std::int32_t, 2> a{}, b{};
        std::int32_t width = 0; // a line's (20.12)
        std::uint16_t colour = 0x7fff;
    };

    std::vector<LoopShape> loopShapes;

    // A picture a play loop draws through the 3D engine as a grid of
    // textured quads (020a0c4c begins, 0209f6b0 quad strips with 0209fac0
    // texture coordinates and 0209fa64 corners), translucent; under the
    // loop's sprites of the picture's screen. Corners in 20.12 pixels,
    // texture coordinates in 20.12 texels, `columns` corners a row.
    struct LoopMesh {
        const ImageResource* image = nullptr;
        int columns = 0;
        std::vector<std::array<std::int32_t, 4>> corners; // x, y, u, v
        unsigned alpha = 31;
        std::uint16_t colour = 0x7fff;
    };

    std::vector<LoopMesh> loopMeshes;

    gss::Machine machine;

private:
    void drawObject(Surface& out, int top, const Chara& c) const;
    void drawRoomPart(Surface& out, int top, const Room::Part& part) const;
    void drawLoopSprite(Surface& out, int top, const LoopSprite& sprite) const;
    void drawLoopShape(Surface& out, int top, const LoopShape& shape) const;
    void drawLoopMesh(Surface& out, int top, const LoopMesh& mesh) const;
    std::vector<std::unique_ptr<gss::Program>> scripts_;
    std::vector<int> scriptLevels_;
    // Freed scripts stay alive for threads still inside them.
    std::vector<std::unique_ptr<gss::Program>> freedScripts_;
    std::vector<std::vector<std::uint8_t>> scriptFiles_;
    std::map<std::uint32_t, ImageResource> images_;
    std::map<std::uint32_t, AnimationResource> animations_;
    std::map<std::uint32_t, std::unique_ptr<Chara>> objects_;
    std::array<std::vector<Chara*>, 2> drawOrder_;
    std::map<std::uint32_t, std::unique_ptr<Message>> messages_;
    std::map<std::uint32_t, std::unique_ptr<Place>> places_;
    std::vector<std::unique_ptr<Chara>> removed_; // replaced before their purge
    std::map<std::uint32_t, std::unique_ptr<Paint>> paints_;
    std::unique_ptr<handwriting::Recognizer> recognizer_;
    std::vector<std::array<std::uint16_t, 2>> characterMap_; // conv_list.gu {from, to}
    std::vector<Paint*> paintOrder_; // made order, for touches (topmost last)

    struct TextList {
        std::map<int, std::vector<std::array<std::string, 2>>> entries;
        int level = 0;
    };

    std::map<std::uint32_t, TextList> texts_;
    void drawMessage(Surface& out, int top, const Message& m) const;
};

} // namespace picarat::adventure4
