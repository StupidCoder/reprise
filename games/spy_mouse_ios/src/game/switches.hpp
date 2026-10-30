// Floor buttons and the doors they work (world 3 on): ObjControllerSwitch (types 65..67, 80,
// 81, 150..155: init @0x1adf24, loadAssets @0x1ae4cc, update @0x1ae7b8, touchPlayer
// @0x1ae92c, receiveMessage @0x1ae98c, render @0x1aeab4, triggerPressed @0x1aec54) and
// ObjControllerDoors (types 210..225: init @0x1b008c, postInit @0x1b0628, loadAssets
// @0x1b06ac, update @0x1b08dc, receiveMessage @0x1b0958, render @0x1b0ab0).
//
// Buttons and doors share a tag (its hundreds give the colour: blue, green, red, yellow).
// Stepping on an "open" button (65) opens the tag's doors (message 1: their wall lines off,
// sound 0x58 at each door) and shows every open button of the tag pressed and every "close"
// button (66) raised; a close button does the opposite (message 2). A timed button (67) opens
// them for 10 s: its countdown shows beside it, in the last 5 s it ticks faster and the doors
// rattle (message 5 each tick, with the time left), and at 0 it closes them again. Doors 210..217
// start closed, 218..225 open; the type's last two bits turn the bars a quarter turn each.
#pragma once

#include "game/geometry.hpp"
#include "game/sound.hpp"
#include "game/sprite_anim.hpp"

#include <vector>

namespace doc::game {

inline bool isSwitch(int type) { return type == 65 || type == 66 || type == 67 || type == 80 || type == 81 || (type >= 150 && type <= 155); }
inline bool isDoor(int type) { return type >= 210 && type <= 225; }
inline bool isCatSwitch(int type) { return type >= 151 && type <= 154; }  // pressed by cats only
inline bool isTimedSwitch(int type) { return type == 67 || type == 80 || type == 81 || type == 153; }

// The colour of a switch's or door's tag (loadAssets: under 100 blue, under 200 green, under
// 300 red, else yellow).
const char* tagColour(int tag);

constexpr int kSwitchUp = 0x2d, kSwitchDown = 0x2e;
constexpr int kDoorClosed = 0x29, kDoorOpen = 0x2a;

struct Switch {
    int objectId = -1;
    int type = 0;
    int tag = 0;
    int state = kSwitchUp;
    int durationMs = 10000;  // +0x1d8 (types 80 and 81: 20 and 30 s)
    int timerMs = 0;         // +0x1dc: the countdown (timed), else the time it may be pressed again
    bool warned = false;     // +0x204: the last-seconds sound is due
    int touched = 0;         // +0x204 of a fight button (0x9b): 2 when touched this tick
    bool updating = false;   // +5: a timed button runs while counting down
    bool snappable = false;  // +6
    int movementSound = -1;  // +0x1c4: the looping tick
    SpriteAnim anim;         // +0x38: trigger_set_<colour>.png, 20 wide
    SpriteAnim timer;        // +0x2fc: timer.png, 18 wide
    // The first frame of its pair (up: +0, down: +1).
    int baseFrame() const;
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(objectId, type, tag, state, durationMs, timerMs, warned, touched, updating, snappable,
                           movementSound, anim, timer); }
};

struct Door {
    int objectId = -1;
    int type = 0;
    int tag = 0;
    int state = kDoorClosed;
    float rotation = 0.0f;   // +0x108, radians
    Vec2 home;               // +0x1c8: where it rests while rattling
    int pass = 2;            // +0x1c0: drawn in the normal pass while closed, low while open
    SpriteAnim anim;         // +0x38: bars_<kind>_<colour>.png, 32 wide, frames 0 closed .. 2 open
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(objectId, type, tag, state, rotation, home, pass, anim); }
};

}  // namespace doc::game
