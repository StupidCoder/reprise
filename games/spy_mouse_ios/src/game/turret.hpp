// World 4's boss fight (map 789): the shuriken launcher (type 0x113, tag 0x66;
// ObjControllerSpawner::update @0x1baa28), the reflectors (0x11a) and the breakable light
// (0x11b; both Object::update @0x118dd0, Object::touchProjectile @0x112ef4).
//
// How it plays: the henchman floats in his balloon over the pond on the right. Once the intro is
// over the launcher on the left wall rides its rail between three stops; at each it waits a
// second and fires a burst of three shurikens straight across the room. A shuriken that meets a
// reflector is turned by a right angle, to the reflector's left or right; the buttons on the
// floor flip the reflectors of their colour. Shurikens hurt the mouse and stun cats; one that
// reaches the balloon ends the fight ("boss_win"), and the henchman is dunked.
#pragma once

#include "game/geometry.hpp"

#include <array>

namespace doc::game {

class Launcher {
public:
    // States (Object +0x1c).
    static constexpr int kReload = 0x2f, kFiring = 0x30, kOff = 0x38, kMoving = 0x39, kResting = 0x3a;

    // ObjControllerSpawner::init @0x1ba22c for the boss's launcher: off until the intro's script
    // switches it on, its clock at 3 s, at the first stop.
    Launcher(int objectId, Vec2 position, float radius, float heading)
        : objectId_(objectId), position_(position), radius_(radius), heading_(heading) {}

    int objectId() const { return objectId_; }
    int state() const { return state_; }
    Vec2 position() const { return position_; }
    int stop() const { return stop_; }
    int clockMs() const { return clockMs_; }
    int shotsLeft() const { return shotsLeft_; }
    int intervalMs() const { return intervalMs_; }

    // ScriptSequence::turretSwitch @0x1d1410: off goes moving, anything else off.
    void toggle() { state_ = state_ == kOff ? kMoving : kOff; }

    // One shot of a burst: where it starts and how it flies.
    struct Shot {
        Vec2 at, velocity;
        int stop = 0;  // the stop it was fired from
        // Quick saves (state_io.hpp).
        template <class Io>
        void io(Io& io) { io(at, velocity, stop); }
    };
    // The update: true when a shuriken is due (`shot` filled); `burstOver` is set when the burst
    // has ended with it (the launcher then moves on to its next stop, sound 0xa1).
    bool update(int dtMs, Shot& shot, bool& burstOver);

private:
    int objectId_ = -1;
    Vec2 position_;          // +0xf4
    float radius_ = 0.0f;    // +0x104
    float heading_ = 0.0f;   // +0x108, radians
    int state_ = kOff;       // +0x1c
    int clockMs_ = 3000;     // +0x1dc
    int intervalMs_ = 0;     // +0x1f8: until the next shot of the burst
    int shotsLeft_ = 0;      // +0x1fc
    int stop_ = 0;           // +0x208: the rail stop it is going to
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(objectId_, position_, radius_, heading_, state_, clockMs_, intervalMs_, shotsLeft_,
                           stop_); }
};

// A reflector's catch (Object +0x228..+0x23c): the projectiles that touched it this tick and
// those of the tick before, by slot; a new one is turned once.
struct ReflectorCatch {
    int objectId = -1;
    std::array<int, 3> now{-1, -1, -1};
    std::array<int, 3> before{-1, -1, -1};
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(objectId, now, before); }
};

}  // namespace doc::game
