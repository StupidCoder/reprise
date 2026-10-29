#pragma once
// Touch input, buttons, the name-entry editor and sprite animation playback:
// the small shared pieces every scene uses.
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace picarat {
// The stylus this frame: down, newly pressed, just released, and where.
struct Touch {
    bool held = false, pressed = false, released = false;
    int x = -1, y = -1;
};

// The game's touch-screen filter (overlay 2, 0206e2c0). The DS touch screen
// gives noisy samples, so the game only accepts a press when two consecutive
// samples are within 30 pixels of each other, and while the stylus is held it
// ignores a sample that jumps more than 30 pixels from the previous one (the
// position stays where it was). A release takes effect at once. (A "locked"
// state 4 exists in the original but is never entered here.)
class TouchFilter {
public:
    Touch update(bool touching, int x, int y);
    int state = 0;                            // 0 idle, 1 pressed, 2 held, 3 released
    int x = -1, y = -1, rawX = -1, rawY = -1; // +0x10/+0x14 and +0x18/+0x1c
    int pressX = -1, pressY = -1, releaseX = -1, releaseY = -1; // +0x28/+0x2c, +0x20/+0x24
    float threshold = 30.0f;                                    // +0x0c, observed 0x41f00000
};

// A rectangle on screen. Like the game's test (overlay 2, 0206ae10), the
// right and bottom edges count as inside, so neighbouring keyboard keys share
// their border pixels; the first key in table order wins there.
struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
    bool contains(int px, int py) const;
};

// The game's ordinary button (overlay 2, 02080300). A click is a press on
// the button and a release while the stylus is still over it; dragging off
// and back is allowed. The click is reported (consumeClick) six frames
// later, while the button shows its pressed image, as in the game.
class Button {
public:
    enum class State { Idle, Armed, Clicked };

    explicit Button(Rect bounds = {}, int sound = -1) : bounds(bounds), sound(sound) {}

    void update(const Touch& touch);
    bool consumeClick();
    Rect bounds;
    // ov2 0208003c stores a sequenced-effect id at +0x10 (-1: none); 02080300
    // plays it through the global sound object when a press is released inside.
    int sound = -1;
    static inline std::function<void(int)> clickSound;
    State state = State::Idle;
    bool inside = false;
    std::uint8_t age = 0;
};

// The player's name as typed on the name-entry screen (overlay 12, 020f0418 /
// 020f0438): ten character slots and a cursor. Once the cursor reaches the
// last slot it stays there, and further characters replace that slot.
class NameEditor {
public:
    void insert(std::uint8_t character);
    void erase();
    void clear();
    std::string value() const;
    // ov12:020f02f0: exact comparison with the lead characters' names
    // (ROM literals, see a2_character_names.hpp).
    bool reserved(const std::array<std::string, 2>& names) const;
    // The rectangle of key `index` on the on-screen keyboard. Layouts 0..2
    // have four staggered rows of 19-pixel keys (12, 12, 11 and 10 keys);
    // layout 3 (special characters) is a 13x4 grid.
    static Rect keyBounds(unsigned index, int layout);
    std::array<std::uint8_t, 10> characters{};
    unsigned cursor = 0;
    int layout = 0;
};

// One step of a sprite animation: the step that follows, how many frames it
// lasts, and which image it shows.
struct AnimationStep {
    std::uint32_t next, duration, image;
};

// Plays a sprite animation (overlay 2, 02075e50). Animation 0 is the
// sprite's "no animation" slot and never plays.
class AnimationPlayer {
public:
    void select(unsigned index, std::uint32_t tick,
                const std::vector<std::vector<AnimationStep>>& animations);
    // Call after drawing: the original draws the current image before stepping.
    void advance(std::uint32_t tick, const std::vector<std::vector<AnimationStep>>& animations,
                 bool frozen = false);
    unsigned animation = 0, step = 0;
    std::uint32_t lastTick = 0, duration = 0;
};
} // namespace picarat
