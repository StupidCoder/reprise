// World 2's boss fight (map 795): the Spitter in its glass dome (type 302; ObjControllerBoss,
// updateSpitter @0x1ab3a8), the two blue spheres riding rails along the sides (type 366) and
// the two gold devices on the end walls (type 367; Object::init @0x11aa30, update @0x118dd0,
// receiveMessage @0x115f18, render @0x11401c, touchProjectile @0x112ef4).
//
// How it plays: once awake the Spitter walks its round (turning back at a corner when that
// leads towards the mouse) and, whenever it sees the mouse (all round, nothing in between),
// throws a fork, spoon or knife at it after a short wind-up; one hit kills. A throw that
// strikes a moving sphere stops the sphere for 15 s and lights the device it names; with both
// devices lit an electric beam spans the room (the wall line tagged 123, type 0x8f), the
// Spitter stops throwing and everything in the air vanishes. Walking into the beam blows the
// Spitter's dome: it breaks (state 0x18), the devices go dark, the spheres stop for good and
// the "boss_win" cutscene follows (blowUpDroid: the explosion and the key).
#pragma once

#include "game/boss_world.hpp"
#include "game/object_motion.hpp"
#include "game/objects.hpp"
#include "game/sprite_anim.hpp"

#include <functional>

namespace doc::game {

constexpr int kSpitter = 302, kRailTarget = 366, kBeamEmitter = 367;

class Spitter {
public:
    struct Sheets {
        SheetSize body, wreck;  // sprites/boss2/boss2.png, boss2_destroyed.png (64 wide)
    };
    Spitter(int objectId, int floor, Vec2 position, float radius, Vec2 size, CatRoute route, const Sheets& sheets);

    int objectId() const { return objectId_; }
    Vec2 position() const { return position_; }
    float heading() const { return heading_; }
    float aim() const { return aim_; }
    Vec2 velocity() const { return velocity_; }
    int state() const { return state_; }  // +0x1c: 0x15 asleep, 0x16 waking, 0x17 active, 0x18 broken
    int routeIndex() const { return walk_.index; }
    int routeDirection() const { return walk_.direction; }
    int cooldownMs() const { return cooldownMs_; }
    int windUpMs() const { return windUpMs_; }
    int throwsDue() const { return throwsDue_; }
    int throwing() const { return throwing_; }

    // SnapshotHandler::rollback: the kept fields come back.
    void restore(const ObjectSnapshot& o) {
        state_ = o.state;
        position_ = o.position;
        heading_ = o.heading;
        walk_.index = o.routeIndex;
        body.setFrame(o.frame);
    }
    // SceneCutscene::hideScene @0x1c95c0: the intro is over, the boss wakes (after +0x1dc).
    void wake() { state_ = 0x16; }
    // ObjControllerBoss::update @0x1a9598 for the Spitter.
    void update(int dtMs, BossWorld& world);
    // ObjControllerBoss::receiveMessage @0x1a9ccc: messages 0 and 4 (the beam is on) stop the
    // throwing for good.
    void receiveMessage(int type) {
        if (type == 0 || type == 4) throwing_ = 0;
    }
    // ScriptSequence::blowUpDroid @0x1d4604 (the boss's part): the explosion over it, broken,
    // smoke from 1 s on, sound 0x8f. The key is thrown by the caller in between.
    void blowUp(BossWorld& world, const std::function<void()>& throwKey);
    // ObjControllerBoss::touchPlayer @0x1a9be0: unless the mouse is protected or the boss
    // broken, it catches the mouse.
    void touchMouse(bool mouseProtected, BossWorld& world);

    SpriteAnim body;   // Object +0x38: sixteen facings
    SpriteAnim wreck;  // Object +0x3b8: looping while broken

private:
    float updateSpitter(int dtMs, BossWorld& world);
    void updateAnimations(const BossWorld& world);
    void throwAtMouse(BossWorld& world);
    bool canSeeMouse(const BossWorld& world) const;

    int objectId_ = -1;
    int floor_ = 0;
    Vec2 position_;          // +0xf4
    Vec2 size_;              // +0xcc
    float radius_ = 0.0f;    // +0x104
    float heading_ = 0.0f;   // +0x108
    float aim_ = 0.0f;       // +0x10c
    Vec2 velocity_;          // +0x1ac
    float speed_ = 1.0f;     // +0x24
    int state_ = 0x15;
    RouteWalk walk_;         // +0x114.., +0x16c, +0x174
    int cooldownMs_ = 0;     // +0x1dc: before waking, then between looks for the mouse
    int windUpMs_ = 0;       // +0x1f8: before a throw
    int throwsDue_ = 0;      // +0x1fc
    int throwing_ = 1;       // +0x204 (broken: time to the next puff of smoke)
};

// A sphere riding a rail (type 366): it shuttles along its route; a throw striking it while it
// moves stops it and lights the device named by its entity text.
struct RailTarget {
    int objectId = -1;
    int floor = 0;
    int messageTag = -1;     // the entity text as a number: the device it lights
    Vec2 position;
    float radius = 0.0f;
    float heading = 0.0f, aim = 0.0f;
    Vec2 velocity;
    RouteWalk walk;
    int state = 0x1b;        // 0x1b moving, 0x1c stopped
    int stopMs = 0;          // +0x1dc (60001: stopped for good)
    bool updating = true;    // +5
    SpriteAnim anim;         // sprites/target_moving.png

    // Object::update: frames, then (only while the level is the top scene) along the rail or
    // waiting out the stop.
    void update(int dtMs, bool levelOnTop, const LevelGeometry& walls, float viewLeft);
};

// A device on an end wall (type 367): lit (0x23) for 15 s by its sphere's message; with its
// partner lit too the beam between them is on (step 1) until the end. A message while lit
// shuts it down: it flickers for half a second (step 2) and goes dark for good.
struct BeamEmitter {
    int objectId = -1;
    int floor = 0;
    int partner = -1;        // +0x1d8: the other device
    int state = 0x22;        // 0x22 dark, 0x23 lit
    int step = 0;            // +0x1d4: 1 the beam is on, 2 shutting down
    int timerMs = 0;         // +0x1dc
    int clockMs = 0;         // +0x204: runs on (the beam's flicker and scroll)
    bool updating = true;    // +5
    SpriteAnim anim;         // sprites/emitter_boss2.png
    // Whether the beam starts here (Object::render @0x1142a2: lit, on or shutting down, and
    // in the shutdown's flicker only every other 50 ms).
    bool beamShown() const {
        if (state != 0x23 || (step != 1 && step != 2)) return false;
        return !(step == 2 && clockMs % 100 < 50);
    }
    void update(int dtMs, std::vector<SoundEvent>& sounds);
};

}  // namespace doc::game
