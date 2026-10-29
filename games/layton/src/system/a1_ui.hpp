#pragma once
#include <initializer_list>
// The first adventure's touch screen state and buttons, the input side of
// its scenes (the drawing goes through SceneKit, system/scene_kit.hpp).
//
// The first adventure has its own touch and button code (ARM9 0200ebc4 and
// the button class 02050e44..020512d0); it shares no code with the second
// adventure's (overlay 2, 0206e2c0 and 0208003c) and behaves differently:
//
//  * a press counts from the first frame the stylus touches (unless the
//    sample jumped 64 pixels or more from the previous one);
//  * a button is "inside" while the stylus is down and both the press
//    position and the current position lie in its rectangle (inclusive
//    edges); a click is a release no later than 4 frames after the last
//    frame inside - there is no delay after the release;
//  * a toggle button flips its state on each click.
#include "system/scene_kit.hpp"

namespace picarat::adventure1 {

// ARM9 0200ebc4 (per frame) on the state block at 020d27e4-ish: current
// position (+8/+0xa), press position (+0x18/+0x1c), previous position
// (+0x20/+0x24), "pressed this frame" (+0x35) and "released this frame"
// (+0x34). Positions are -16 when there is none.
class TouchState {
public:
    // One frame: the host's stylus sample.
    void update(bool down, int x, int y);

    // 0200eeb4: whether the stylus is down and the current sample is less
    // than 64 pixels from the sample this function saw last. Every call
    // remembers the current sample, so within one frame only the first call
    // can reject a jump (the original calls it from several places).
    bool steady();

    bool down = false;
    int x = -16, y = -16;                 // current sample (+8/+0xa)
    int pressX = -16, pressY = -16;       // where the press started (+0x18/+0x1c)
    int previousX = -16, previousY = -16; // last frame's sample while down (+0x20/+0x24)
    bool pressed = false;                 // a press started this frame (+0x35)
    bool released = false;                // the stylus was lifted this frame (+0x34)
    unsigned frame = 0;                   // the game's frame counter (020512cc)

private:
    int seenX_ = -16, seenY_ = -16; // 0200eeb4's remembered sample (020d2824)
};

// A button (ARM9 02050e44): a rectangle, usually with a sprite.
class Button {
public:
    // 02050f1c: a sprite button at (x, y) whose rectangle is the size of
    // the sprite's current frame. `animation` names the idle animation
    // (empty: animation 1). `shadow` adds a second copy of the sprite 2
    // pixels down and right in the animation named by the ROM (02051028).
    void setSprite(SceneKit& kit, int x, int y, const std::string& sprite,
                   const std::string& animation = {}, bool shadow = false);
    // 0205102c: a plain rectangle without a sprite.
    void setRect(int x, int y, int width, int height);
    // 02050ef8: remember the current animation as the one shown while
    // pressed (the original swaps between two animations).
    void setPressedAnimation(SceneKit& kit, const std::string& animation);

    // 02051138: input and drawing, once per frame.
    void update(SceneKit& kit, TouchState& touch);
    // 020512d0: whether the button was clicked this frame; plays its sound
    // (`sound`) when `withSound`.
    bool clicked(SceneKit& kit, const TouchState& touch, bool withSound = true);

    // 0205104c: the press began on the button and the stylus is on it now
    // (no steadiness test).
    bool held(const TouchState& touch) const {
        auto inside = [&](int px, int py) {
            return x <= px && px <= x + width && y <= py && py <= y + height;
        };
        return inside(touch.pressX, touch.pressY) && inside(touch.x, touch.y);
    }

    // 020510a8: drawing only (a toggle button only while on).
    void draw(SceneKit& kit);

    // 02050ee8: forgets a pending click.
    void cancel() {
        active_ = false;
        stamp_ = kNever;
    }

    int x = 0, y = 0, width = 0, height = 0;
    int pressDx = 0, pressDy = 0;        // sprite offset while pressed (+0x10/+0x14)
    bool visibleIdle = false;            // +0x1b: draw while not pressed
    bool toggle = false, on = false;     // +0x20/+0x21
    bool drawOnly = false;               // +0x22: only drawn, never pressed
    int sound = 0;                       // +0x23: effect played on a click
    SceneKit::SpriteObject sprite;       // +0x28
    SceneKit::SpriteObject shadowSprite; // +0x2c

private:
    // The original clears the stamp to 0 while its frame counter is far
    // past it; here the counter starts at 0, so "never" is far away instead.
    static constexpr unsigned kNever = 0x80000000u;
    bool hasSprite_ = false, hasShadow_ = false;
    bool active_ = false, armed_ = false; // +0x18 / +0x19
    unsigned stamp_ = kNever;             // +0x24: frame of the last press inside
    bool swap_ = false;                   // +0x1e
    unsigned pressedAnimation_ = 0, idleAnimation_ = 0;
};

// The other button kind (ARM9 02051390..02051824), used by a few engines:
// a sprite placed at its "pos" variable that shows "on" while the stylus,
// pressed on it, stays on it and "off" otherwise. Releasing on it plays the
// button's sound at once; the click is reported six updates later (like the
// second adventure's buttons, system/interaction.hpp, which it resembles).
class SpriteButton {
public:
    // 02051390: the sprite in "off", at its "pos" variable; `sound` is
    // played on a click (-1: none).
    void load(SceneKit& kit, const std::string& sprite, int sound);
    // 02051390 without a position variable: at (x, y).
    void load(SceneKit& kit, const std::string& sprite, int sound, int x, int y);
    // 0205153c(button, 1): input (020515c8), then drawing (0205155c).
    void update(SceneKit& kit, const TouchState& touch);
    // 0205155c: drawing only.
    void draw(SceneKit& kit);
    // 02051824: the click, once its six updates have passed.
    bool clicked();

    SceneKit::SpriteObject sprite; // +0x14
    int x = 0, y = 0;              // +0/+4
    int sound = -1;                // +0x10

private:
    enum class State { Idle, Held, Clicked }; // +0x18: 0, 1, 2
    State state_ = State::Idle;
    bool inside_ = false;  // +0x1c
    unsigned counter_ = 0; // +0x1d: updates since the click
    void enter(SceneKit& kit, State state);
    bool hit(SceneKit& kit, const TouchState& touch) const;
};

// The first adventure's fades: each frame, the chosen screens' brightness
// moves by `step` from where it is toward 0 (in) or -16 (out), the last
// drawn frame staying on screen, for 16 / step + 1 frames: ARM9 0200fb90
// (lower in), 0200fce8 (both out), 0200fe78 (upper out), 0200ff00 (lower
// out) always run all of them (the last at the target); 0200f7b4 (both in)
// stops at the target. Every frame counts as a game frame. The lower screen
// is the main engine (0x0400006c).
enum class Screens { Lower, Upper, Both };
// `frames`: VBlank waits per step (0200f7b4 / 0200fce8's third argument).
void fadeIn(SceneKit& kit, Screens screens, int step = 1, int frames = 1);
void fadeOut(SceneKit& kit, Screens screens, int step = 1, int frames = 1);

// The text layers (ARM9 0200d5ec): 8-bit layers whose palette (0200d4ec)
// is black (0xff, the default), white (0xfe), red (0xfc, "#R"), green (0xfb,
// "#G") and a pale cyan (0xfa). Glyphs come from the 1-bit fonts with the
// program's width tables (packed with the fonts): fontevent for centred
// titles (0200dac8), fontq for text boxes (0200df2c).
std::uint16_t textColour(int index);

// ARM9 0200df2c: draws `text` from (x, y) on a screen's text layer with
// fontq: characters before `skip` only advance, at most `count` characters
// are read; a newline steps 12 pixels; "#X" changes the colour (G, R, W, X),
// "@c" clears the lower text layer from y down and starts again at (x, y);
// other "@" codes take no room. The colour returns to black afterwards.
// (The original also waits 3 frames at a final "@p" and plays a typing
// sound every fifth character while revealing; neither is done here yet.)
void drawTextBox(SceneKit& kit, SceneKit::Screen screen, int x, int y, std::string_view text,
                 std::size_t skip = 0, std::size_t count = std::string_view::npos);
// ARM9 0200d960: `text` from (x, y) with fontevent, left aligned: a newline
// (or "@l") steps the font's line feed + 4 (16 pixels), "#X" changes the
// colour as in 0200df2c; other "@" codes take no room. (Carriage returns
// are skipped: hypothesis, the font has no glyph for them.)
void drawEventText(SceneKit& kit, SceneKit::Screen screen, int x, int y, std::string_view text);
// ARM9 0200dac8 (one line, used by 0200dc8c, and by 0200dd50 for the upper
// screen, for each line):
// `line` centred on `centreX` at row `y` with fontevent: the width is the
// sum of the characters' advances plus 1 each (0200d270, table 020d2710).
void drawCentredLine(SceneKit& kit, SceneKit::Screen screen, int y, std::string_view line,
                     int centreX = 0x80);

// ARM9 0200de38: a block of lines (split at newlines) centred on row
// `centreY` (lines step by the font's line feed + 5 + `extra`), each line
// centred on `centreX` (0200dac8), in palette colour `colour`; `fontq`
// picks the question font instead of fontevent. Drawn on `layer`. A
// negative `centreX` left-aligns the lines at -centreX (0200de00).
void drawCentredText(SceneKit& kit, Surface& layer, int centreY, std::string_view text,
                     bool fontq = false, int extra = 0, int centreX = 0x80, int colour = 0xff);

inline void drawCentredText(SceneKit& kit, SceneKit::Screen screen, int centreY,
                            std::string_view text, bool fontq = false, int extra = 0,
                            int centreX = 0x80, int colour = 0xff) {
    drawCentredText(kit, kit.textLayer(screen), centreY, text, fontq, extra, centreX, colour);
}

// The program's sprintf (020b31cc / 0200c410) on a format from the game's
// texts ("Puzzle %.3d %s", "Error Code: %d"): %d / %i take the next number,
// %s the next string, with their flags, width and precision; %% is a
// percent sign. A conversion without a fitting argument is left out.
struct TextArg {
    TextArg(int v) : number(v), isNumber(true) {}

    TextArg(std::string v) : text(std::move(v)) {}

    int number = 0;
    std::string text;
    bool isNumber = false;
};

std::string formatText(std::string_view format, std::initializer_list<TextArg> args);

// A picture's name as the scripts give it ("room_1_bg.bgx") to the pack's
// name: the loaders (02009d80, 0200a4e0) replace the last three characters
// by "arc" whatever they are (event 45 names "ebg_302.gbx").
inline std::string pictureName(std::string name) {
    if (name.size() > 4 && name[name.size() - 4] == '.') {
        name.resize(name.size() - 4);
    }
    return name;
}

// Whether 0200df2c, typing `text` from `skip` to `count`, waits for the
// typing sound: when the last character read is drawn (not a newline or a
// code) and its index is a multiple of 5 (measured: one frame).
bool typingWaits(std::string_view text, std::size_t skip, std::size_t count);

} // namespace picarat::adventure1
