// The boss of world 1 (map 791): Accela, a droid that rides a rail round the room with
// Fluffy in its dome (type 301; ObjControllerBoss, updateAccela @0x1ac158). Reconstructed from
// ObjControllerBoss::init @0x1a8a24, postInit @0x1a9128, update @0x1a9598,
// updateAnimations @0x1a984c, receiveMessage @0x1a9ccc, touchPlayer @0x1a9be0 and updateAccela
// with its helpers canAccelaSeePlayer @0x1ac094 and accelaStartRage @0x1abdf8, and the parts of
// Object::updatePath @0x115614 and Object::updatePositions @0x115430 that apply to it.
//
// How it behaves: it sleeps at the start of its rail until the mouse is in line with it (ahead
// or behind, within 25 degrees, nothing in between). Then it builds up rage for a moment,
// charges along the rail towards the mouse, faster and faster, and from the next rail point
// rolls straight on until it hits something; it bounces back a little and looks around. If it
// sees the mouse again it charges again; if the mouse is merely close it turns to it; after a
// while it gives up and rolls back to the start of its rail. Running over one of the room's
// trigger lines (type 0x81) while charging sends that line's message: the first one sets it
// sliding out of control, the second (while still sliding) wins the fight.
#pragma once

#include "game/boss_world.hpp"
#include "game/cat.hpp"
#include "game/geometry.hpp"
#include "game/level_geometry.hpp"
#include "game/objects.hpp"
#include "game/particles.hpp"
#include "game/random.hpp"
#include "game/sound.hpp"
#include "game/sprite_anim.hpp"

#include <functional>
#include <vector>

namespace doc::game {

class Droid {
public:
    Droid() = default;
    // Placed on the first point of its rail (the route whose start vertex carries its tag),
    // facing along the first leg.
    struct Sheets {
        SheetSize body, base, wreck;  // boss1_part1.png, boss1_part0.png, boss1_destroyed.png
    };
    Droid(int objectId, int floor, CatRoute rail, float radius, Vec2 size, const Sheets& sheets);

    int objectId() const { return objectId_; }
    Vec2 position() const { return position_; }
    float heading() const { return heading_; }
    float aim() const { return aim_; }
    Vec2 velocity() const { return velocity_; }
    int state() const { return state_; }  // Object +0x1c: 0x3c in a cutscene, 0x16/0x17 active, 0x15 caught the mouse, 0x18 broken
    void setState(int state) { state_ = state; }
    int step() const { return step_; }    // Object +0x1d4: what it is doing while active
    const CatRoute& rail() const { return rail_; }
    int railIndex() const { return railIndex_; }
    int railDirection() const { return railDirection_; }

    // SnapshotHandler::rollback: the kept fields come back.
    void restore(const ObjectSnapshot& o);
    // ObjControllerBoss::update: one tick.
    void update(int dtMs, BossWorld& world);
    // ObjControllerBoss::updateAnimations: the body picture faces its heading (8 facings of
    // two frames, 400 ms each).
    void updateAnimations(int dtMs);
    // ObjControllerBoss::receiveMessage: a trigger line's message (types 0 and 4).
    void receiveMessage(int type, BossWorld& world);
    // ScriptSequence::blowUpDroid @0x1d4604 (the droid's part): an explosion over it, broken
    // (state 0x18), smoke from 1 s on, sound 0x8f. The key is thrown by the caller in between.
    void blowUp(BossWorld& world, const std::function<void()>& throwKey);
    // ObjControllerBoss::touchPlayer: the droid catches the mouse (world.killMouse) unless
    // `mouseProtected`, the mouse's own guard (Player +0x1e4 > 0, or out of play).
    void touchMouse(bool mouseProtected, BossWorld& world);
    // Set when a trigger line's message reached it while sliding: the level fires its
    // "boss_win" event (EventManager::fireEvent).
    bool winRequested = false;

    SpriteAnim body;  // Object +0x38: sprites/boss1/boss1_part1.png
    SpriteAnim base;  // Object +0x530: the base's two frames (0 calm, 1 alert)
    SpriteAnim wreck; // Object +0x3b8: boss1_destroyed.png, looping while broken

    // Drawing (renderAccela): the dome's turn in degrees, whether the sensor head (looking
    // where it heads) replaces the body, and the crash's spark flash (one frame, along the
    // roll's direction).
    float wobble() const { return wobble_; }
    bool showsSensor() const { return step_ == 2 || (step_ >= 5 && step_ <= 7) || step_ == 9; }
    bool sparkShown() const { return sparkShown_; }
    Vec2 coastDirection() const { return coastDirection_; }
    // Before a frame is drawn: the spark flag is taken for this frame (renderAccela clears it).
    void prepareFrame() {
        sparkShown_ = sparked_;
        sparked_ = false;
    }

private:
    float updateAccela(int dtMs, BossWorld& world);
    bool canSeeMouse(const BossWorld& world) const;
    void startRage(BossWorld& world);
    bool mouseClose(const BossWorld& world) const;
    float followRail(float speed);
    void turnAndMove(int dtMs, float speed);
    // Touching a trigger line (type 0x81) sends its message: type 4 while sliding, else 0.
    void touchTriggerLines(BossWorld& world);
    void playMovementSound(int effect, BossWorld& world);

    int objectId_ = -1;
    int floor_ = 0;
    Vec2 position_;          // +0xf4
    Vec2 size_;              // +0xcc: the frame size
    float radius_ = 0.0f;    // +0x104
    float heading_ = 0.0f;   // +0x108
    float aim_ = 0.0f;       // +0x10c
    Vec2 velocity_;          // +0x1ac: the last step
    int state_ = 0x3C;
    int step_ = 0;
    int talkPicture_ = -1;   // +0x21c: >= 0 while a cutscene animates one of its parts (not seen)
    int scriptFlag_ = -1;    // +0x204 (broken: time to the next puff of smoke)
    int movementSound_ = -1; // +0x1c4: the looping engine sound
    int particleSlot_ = -1;  // +0x218: the particle it shows over itself
    int caughtTimerMs_ = 0;  // +0x1dc (set when it catches the mouse; not read in a fight)
    CatRoute rail_;
    int railIndex_ = 0;      // +0x16c
    int railDirection_ = 1;  // +0x174

    // Controller fields.
    int searchTimerMs_ = 0;  // +0x5e4: re-armed while it watches a close mouse (draws rand())
    int stepClockMs_ = 0;    // +0x5e8: rage deadline, or time spent looking around
    int sensorFrame_ = 0;    // +0x5ec: the sensor head's facing
    int sensorTick_ = 0;     // +0x5f0
    float wobble_ = 0.0f;    // +0x5f4: a clock for the dome's wobble (drawing)
    Vec2 coastFrom_;         // +0x5f8: where the roll straight on started
    Vec2 coastDirection_;    // +0x600
    float coastDistance_ = 0.0f;  // +0x608
    float coastSpeed_ = 0.0f;     // +0x60c
    float chargeSpeed_ = 0.0f;    // +0x610
    bool sparked_ = false;        // +0x614: it hit a wall (sparks)
    bool sparkShown_ = false;
};

}  // namespace doc::game
