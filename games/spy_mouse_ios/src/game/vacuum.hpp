// World 3's boss fight (map 733): the vacuum machine (type 300; ObjControllerBoss, updateVaccuum
// @0x1ab7b0), the bomb dropper on the left wall (type 274, ObjControllerSpawner), the bombs it
// drops (type 253, Object::update @0x118dd0) and the marker at the end of the conveyor (type
// 254, not drawn).
//
// How it plays: once awake the vacuum walks its route along the left, top and right walls
// (turning back at a corner when that leads towards the mouse). When it sees the mouse within
// 160 units it sucks: the longer it sees it the harder (three stages of 850 ms), and the mouse,
// walking, is drawn towards its nozzle; touching it catches the mouse. The cheese button in the
// middle drops a bomb on the conveyor along the bottom, which carries it to the right. While a
// bomb is about, the vacuum turns at its corners towards the conveyor's end; once it sees the
// bomb within 96 units it pulls it in at full strength, and swallowing it ends the fight
// ("boss_win").
#pragma once

#include "game/boss_world.hpp"
#include "game/object_motion.hpp"
#include "game/objects.hpp"
#include "game/sprite_anim.hpp"

#include <functional>

namespace doc::game {

// What the vacuum acts on besides the BossWorld: the mouse it may pull (Player +0x1b8) and the
// first bomb of the level with the conveyor-end marker (SceneGame::getFirstObjectByType 253, 254).
struct VacuumTargets {
    Vec2* mouse = nullptr;
    LevelObject* bomb = nullptr;
    const LevelObject* bombTarget = nullptr;
};

class Vacuum {
public:
    struct Sheets {
        SheetSize body, nozzle, suction, effect;  // boss3_part1, boss3_part0, boss3_part2, vacuum_effect
        // Quick saves (state_io.hpp).
        template <class Io>
        void io(Io& io) { io(body, nozzle, suction, effect); }
    };
    Vacuum(int objectId, int floor, Vec2 position, Vec2 size, CatRoute route, const Sheets& sheets);

    int objectId() const { return objectId_; }
    Vec2 position() const { return position_; }
    Vec2 size() const { return size_; }
    float heading() const { return heading_; }
    float aim() const { return aim_; }
    Vec2 velocity() const { return velocity_; }
    int state() const { return state_; }  // +0x1c: 0x15 asleep, 0x16 waking, 0x17 active, 0x18 broken
    int stage() const { return stage_; }  // +0x1d4: suction, 0 none .. 3 full
    int timerMs() const { return timerMs_; }  // +0x1dc: the wake wait, then how long it has sucked
    int routeIndex() const { return walk_.index; }
    int routeDirection() const { return walk_.direction; }
    // Object +0x205 bit 0: the suction picture is up (drawn in place of the body).
    bool sucking() const { return state_ == 0x1b && stage_ >= 1 && (pictureFlags_ & 0x100) != 0; }

    // SnapshotHandler::rollback: the kept fields come back.
    void restore(const ObjectSnapshot& o) {
        state_ = o.state;
        position_ = o.position;
        heading_ = o.heading;
        walk_.index = o.routeIndex;
        body.setFrame(o.frame);
    }
    // SceneCutscene::hideScene @0x1c95c0: the intro is over, the boss wakes (after +0x1dc).
    void wake() { state_ = 0x1a; }
    // ScriptSequence::blowUpDroid @0x1d4604 (the boss's part): the explosion over it, broken,
    // smoke from 1 s on, sound 0x8f. The key is thrown by the caller in between.
    void blowUp(BossWorld& world, const std::function<void()>& throwKey);
    // ObjControllerBoss::update @0x1a9598 for the vacuum.
    void update(int dtMs, BossWorld& world, const VacuumTargets& targets);
    // ObjControllerBoss::touchPlayer @0x1a9be0: unless the mouse is protected or the boss
    // broken, it catches the mouse and falls asleep for 5 s.
    void touchMouse(bool mouseProtected, BossWorld& world);

    SpriteAnim body;     // Object +0x38: eight facings, frame 8 swallowing
    SpriteAnim effect;   // +0x2fc: the suction's swirl, ahead of the nozzle
    SpriteAnim nozzle;   // +0x240: frame 0 idle, 1 sucking
    SpriteAnim suction;  // +0x3b8: the body while sucking, two frames a facing

private:
    float updateVaccuum(int dtMs, BossWorld& world, const VacuumTargets& targets);
    void updateAnimations(int dtMs);
    bool canSeeMouse(const BossWorld& world) const;
    bool canSee(Vec2 at, float angle, float distance, const BossWorld& world) const;
    float walk(float speed, const BossWorld& world, const VacuumTargets& targets);

    int objectId_ = -1;
    int floor_ = 0;
    Vec2 position_;          // +0xf4
    Vec2 size_;              // +0xfc
    float radius_ = 22.0f;   // +0x104 (ObjControllerBoss::postInit)
    float heading_ = 0.0f;   // +0x108
    float aim_ = 0.0f;       // +0x10c
    Vec2 velocity_;          // +0x1ac
    float speed_ = 0.0f;     // +0x24
    int state_ = 0x19;
    RouteWalk walk_;         // +0x114.., +0x16c, +0x174
    int stage_ = 0;          // +0x1d4
    int timerMs_ = 0;        // +0x1dc
    bool seesMouse_ = false; // +0x1d0 (Object::canSeePlayer's result)
    int pictureFlags_ = -1;  // +0x204: the suction picture's facing x 2, bit 8 while it is up
    int smokeMs_ = 0;        // +0x204 once broken: to the next puff of smoke
    int moodMs_ = 0;         // ObjControllerBoss +0x5e4 (redrawn while sucking: rand() % 4000 + 8000)
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(body, effect, nozzle, suction, objectId_, floor_, position_, size_, radius_, heading_,
                           aim_, velocity_, speed_, state_, walk_, stage_, timerMs_, seesMouse_, pictureFlags_,
                           smokeMs_, moodMs_); }
};

}  // namespace doc::game
