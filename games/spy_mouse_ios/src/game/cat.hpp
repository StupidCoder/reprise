// Cats (ObjControllerCat @0x19fbc4..0x190168, object types 0x32..0x3b; the brown cat 0x32 of
// world 1 is the one reconstructed). A cat walks a route stored in the map (the second line
// list, MapLinking::linkPath @0x13fe68), turning smoothly towards the next point
// (Object::updatePath @0x115614, ObjControllerCat::update @0x1a0438), feels ahead with two
// whiskers so it does not walk into walls (applySteering @0x1a1b60), checks every few ticks
// what it can see (awarenessCheck @0x1a5468: rand() sets the next check) and spots the mouse
// inside its view (Object::canSeePlayer @0x113808: 400 ahead, 145 degrees wide, clear sight).
// A cat that spots the mouse chases it (chase @0x1a3850): it stops with a "?", walks over,
// takes aim with a heart once close, pounces and, touching the mouse, catches it
// (touchPlayer @0x1a0ae8). The way it went is kept as a trail of points it walks back along
// to its route (Object::setReturnPos @0x1129f0, updateReturnPath @0x112820).
#pragma once

#include "data/map_file.hpp"
#include "game/geometry.hpp"
#include "game/objects.hpp"
#include "game/level_geometry.hpp"
#include "game/random.hpp"
#include "game/particles.hpp"
#include "game/sound.hpp"
#include "game/sprite_anim.hpp"

#include <functional>
#include <limits>
#include <vector>

namespace doc::game {

inline bool isCat(int type) { return (type >= 0x32 && type <= 0x3b) || type == 0x11d; }

// An object's route (Object +0x114, a Path): points with a pause in seconds each (the map
// vertex's `e` field; -1 cuts the route there), how it starts and how it ends.
struct CatRoute {
    struct Point {
        Vec2 position;
        int pauseSeconds = 0;  // short at +8
        // Quick saves (state_io.hpp).
        template <class Io>
        void io(Io& io) { io(position, pauseSeconds); }
    };
    std::vector<Point> points;
    int startMode = 0;  // +0x120 (start vertex 0x82: 1, 0x84: 2)
    int endMode = 0;    // +0x124 (end vertex 0x67: 2, loops back to the first point)

    // MapLinking::linkPath: from the start vertex (type 101) whose |d| is `tag` along the
    // map's second line list.
    static CatRoute link(const data::MapFile& map, int tag);
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(points, startMode, endMode); }
};

// What a cat needs to know about the mouse.
struct CatTarget {
    Vec2 position;      // Player +0x1b8
    Vec2 velocity;      // Player +0x264: its last step (zero while standing)
    int trailLength = 0;  // Player +0x174: carried items
    int hurtMs = 0;     // Player +0x1e4: cats do not catch it while positive
    int state = 0;      // Player +0x130
    int floor = 0;
    bool active = true; // Player +0x70
    bool shadowed = false;  // Player +0x53e (in a dark area: not seen)
    bool carriesSomething = false;  // Player +0x180/+0x184
    bool carriesLure = false;  // twine or catnip in the trail (Player::trailContainsObject)
};

// The world around a cat during its update, and what the cat did to it.
struct CatWorld {
    const LevelGeometry& walls;
    GameRandom& random;
    Particles& particles;
    int gameTimeMs = 0;      // SceneGame +8
    float viewLeft = 0.0f;   // SceneGame +0x13838
    bool infinite = false;   // the level scrolls (game mode 1)
    // Set by the boss cat in an infinite level: the scroll's speed (SceneGame +0x13834) this
    // tick, or not touched (NaN).
    float scrollSpeed = std::numeric_limits<float>::quiet_NaN();
    CatTarget mouse;

    const std::vector<class Cat>* cats = nullptr;  // every cat of the level (SceneGame +0x135b4)
    class Projectiles* projectiles = nullptr;       // SceneGame +0x1165c (the ninja's shurikens)
    // Things cats play with or watch (the TVs), in object order; the cats change their seats.
    std::vector<LevelObject*> playthings;
    // The mouse's trail (Player +0x174, a list through the items' +0x17c), head first, and the
    // way to take an item out of it (Player::drop(false, id)).
    std::vector<LevelObject*> trail;
    std::function<void(int)> dropFromTrail;
    std::vector<LevelObject>* objects = nullptr;  // the level's objects by id (getObjectById)
    bool closeBankHoles = false;     // SceneGame::closeBankHoles: no cheese left in the trail

    std::vector<SoundEvent> sounds;  // effects to play, in order
    bool spottedMouse = false;       // the level's "spotted" flag (LevelState +0x54) goes up
    bool bossSawMouse = false;       // the boss cat saw the mouse: the chase is lost
    bool meanCatSpotted = false;     // a black cat saw the mouse: it jumps
};

class Cat {
public:
    Cat() = default;
    // ObjControllerCat::init @0x19f8e4 (with the object's place and size already set).
    Cat(int objectId, int type, int floor, Vec2 position, float radius, CatRoute route, SheetSize walkSheet,
        SheetSize idleSheet);

    int objectId() const { return objectId_; }
    int type() const { return type_; }
    // ObjControllerCat::update: one tick.
    void update(int dtMs, CatWorld& world);
    // SnapshotHandler::rollback: the kept fields come back.
    void restore(const ObjectSnapshot& o);
    void reload();
    // ObjControllerCat::touchPlayer: the mouse touches the cat. Returns true when the cat
    // catches it (the caller kills the mouse); `meetingPoint` is then where the fight cloud
    // goes, halfway between the two (the cat moves there too).
    bool touchMouse(const CatTarget& mouse, CatWorld& world, Vec2& meetingPoint);
    void setCommonSheet(SheetSize sheet);  // browncat_common.png
    // The black cat's sitting sequences (anim_sequences/black_cat_idle: idle_0N facing N,
    // standing_up_0N, the TV ones); it starts on the one facing away from its heading.
    void setIdleSequences(const std::vector<data::AnimSequence>& sequences);
    // The idle sequence is drawn mirrored (Object +0x38c: facings 5..7 use 3..1).
    bool idleMirrored() const { return idleMirrored_; }
    // SceneGame::resetCats @0xf80d0 (Object::reInit @0x117524) when the level is reloaded
    // after being lost or from the pause menu: back on its start, patrolling from route point
    // 0, timers and the way back cleared. What it saw last and its "?" timer stay. A reload
    // also runs the cat's init and loadAssets again (facing and animation clock start over);
    // a floor change (SceneGame::pinholeCallback) only resets (`reload` false).
    void restart(bool reload = true);
    // Object::onFloorChanged @0x11208c: a cat not on the new floor stops its looping sounds.
    void leaveFloor(CatWorld& world) { stopPurring(world); }
    // ObjControllerCat::renderCat @0x1a41cc, once per presented frame: drawing folds the aim
    // into [0, 2pi) and turns the picture to the aim's facing (at most every 50 ms of
    // animation). Both are game state.
    void prepareFrame();
    int facing() const { return facing_; }  // +0x824: sheet row, -1 before the first frame
    int sightings() const { return sightings_; }  // +0x214: times it spotted the mouse
    int headBumps() const { return headBumps_; }  // +0x210: pounces into a wall

    Vec2 position() const { return position_; }
    float heading() const { return heading_; }
    int state() const { return state_; }
    bool sawMouse() const { return seenMouse_; }
    // The boss cat stopped at the end of its route (its object no longer updates).
    bool stoppedForGood() const { return stoppedForGood_; }
    int trimmed() const { return trimmed_; }
    void forgetMouse() { seenMouse_ = false; }

    // State for the trace comparison (doc-verify); offsets of the original's Object.
    float targetHeading() const { return targetHeading_; }  // +0x10c
    float steerOffset() const { return steerOffset_; }      // +0x110
    Vec2 velocity() const { return velocity_; }             // +0x1ac
    int routeIndex() const { return routeIndex_; }          // +0x16c
    int awarenessMs() const { return awarenessMs_; }        // +0x1dc
    int collisionStreak() const { return collisionStreak_; }  // +0x1f0
    int chaseStep() const { return chaseStep_; }            // +0x1d4
    int targetId() const { return targetId_; }              // +0x1d8
    int chaseTimerMs() const { return chaseTimerMs_; }      // +0x1ec
    int lookMs() const { return lookMs_; }                  // +0x1e8
    int calmMs() const { return calmMs_; }                  // +0x1e4
    Vec2 seenAt() const { return seenAt_; }
    // Player::hide (1.1.1 @0x124938): a cat whose target is the mouse (and that is not in state
    // 0x11) remembers where the mouse went into hiding and makes that the end of its way back.
    void mouseHidAt(Vec2 at, const CatWorld& world) {
        if (state_ == 0x11 || targetId_ != 0) return;
        seenAt_ = at;
        setReturnPos(at, world);
    }                 // +0x1c8
    const std::vector<Vec2>& returnPath() const { return returnPath_; }  // +0x140
    int returnIndex() const { return returnIndex_; }        // +0x170
    float speed() const { return speed_; }
    float radius() const { return radius_; }
    int floor() const { return floor_; }
    const CatRoute& route() const { return route_; }

    // ObjControllerCat::getActiveSprite @0x18c8dc (1.0.5): the picture drawn and animated now (Fluffy:
    // none while out of play, a part picture while a cutscene poses him).
    enum class Picture { Walk, Idle, Common, Pose, Balloon, None };
    Picture picture() const;
    const SpriteAnim& activeSprite() const;

    // Cutscenes (ScriptSequence): Fluffy steps out in his pose facing `aim` (showFluffy), walks a
    // path (walkPath: returns this tick's speed, 0 at the end) or has his state set.
    void showInCutscene(float aim);
    void startScriptedWalk();
    float scriptedWalk(const CatRoute& path, int& point, CatWorld& world);
    void setState(int state) { state_ = state; }
    // World 3's henchman (ObjControllerCat::loadAssets @0x19fd92 for 0x3b in world index 2:
    // boss3_cry.png, 64 wide, as his +0x240 picture; boss3_splash.png, 52 wide, as +0x3b8,
    // neither updated nor drawn by the cat itself, +0x449).
    void setHenchmanSheets(SheetSize cry, SheetSize splash, float splashWidth = 52.0f);
    // ScriptSequence::launchHenchman @0x1d577c: put in the beaten boss (state 0x3c, his
    // cutscene picture slot 0, frames 0..last looping at 50 ms, +0x108 = 180: degrees here);
    // flown along the arc, turned by the flight; landed (the splash runs once, heading 0).
    void launchHenchman(Vec2 at, int lastFrame);
    void flyHenchman(Vec2 at, float headingDegrees) {
        position_ = at;
        heading_ = headingDegrees;
    }
    void landHenchman();
    // +0x208: set by the launch; renderCat then turns his picture by +0x108 (degrees).
    bool henchmanLaunched() const { return henchmanLaunched_; }
    // ScriptSequence::shockHenchman @0x1d227c: shocked (state 0x13) on one frame, then shaking
    // on frames first..last at 200 ms.
    void shockHenchman(int frame);
    void shakeHenchman(int first, int last) { common.setAnim(first, last, SpriteAnim::Loop, 200); }
    // World 4's henchman (ObjControllerCat::init @0x19f9a6 and loadAssets for 0x3b in world
    // index 3): in his balloon (state 0x14) from the start, swaying about where he was placed
    // (+0x20c); boss4.png, 48 wide, frames 0..7 looping at 400 ms (+0x474).
    void rideBalloon(SheetSize boss);
    void initForWorld(int world);
    void reset();  // SceneGame::resetCats: starts over, pictures untouched
    void reloadPictures();  // Object::loadAssets
    bool ridesBalloon() const { return ridesBalloon_; }
    // ScriptSequence::dunkHenchman2 @0x1d486c: out of the balloon, posed on his cry picture's
    // first frame (state 0x3c); sunk to a height; splashed (the splash once through frames 0..4
    // at 150 ms, crying on frames 1.. at 125 ms, state 0x13).
    void dunkHenchman();
    void sinkTo(float y) { position_.y = y; }
    void splashHenchman();

    SpriteAnim pose;    // Object +0x3b8: Fluffy's sitting picture (bosscat_sit.png)
    SpriteAnim walk;    // Object +0x38: the walking strips (8 facings in 5 pictures)
    SpriteAnim idle;    // Object +0x2fc: sitting, looking about (browncat_idle, 50 wide)
    SpriteAnim common;  // Object +0x240: browncat_common (stunned)
    SpriteAnim balloon; // Object +0x474: world 4's henchman in his balloon
    int animatedMs = 0;  // +0x874

private:
    // Object::setMeanCatIdleSequence @0x11276c: the black cat sits looking towards `target`.
    void setMeanCatIdleSequence(const char* prefix, Vec2 target);
    // ObjControllerCat::loadAssets: the sitting picture a (re)loaded cat starts on.
    void startIdlePicture();
    bool idleMirrored_ = false;
    float followRoute(float speed, CatWorld& world);  // Object::updatePath
    float initialSpeed() const;
    int initialState() const;
    Vec2 initialVelocity() const;
    void steer(const CatWorld& world, float speed);  // applySteering
    void flock(const CatWorld& world);               // applySteering, its second part
    void awareness(int dtMs, CatWorld& world);       // awarenessCheck
    float throwProj(int dtMs, CatWorld& world);       // throwProj (the ninja cat)
    float behave(int dtMs, CatWorld& world);         // runBehaviour
    float chase(int dtMs, CatWorld& world);          // chase (state 5)
    float pounce(int dtMs, CatWorld& world);         // pounce (state 6, 1.1.1)
    float stun(int dtMs, CatWorld& world);           // stun (state 7, 1.1.1)
    void lookAfterPounce(CatWorld& world);
    void takeAim(CatWorld& world);
    bool canSeeMouse(const CatWorld& world, bool wide, float halfAngle);  // Object::canSeePlayer
    bool canSeeTrail(const CatWorld& world);         // canSeeTrail
    int validTrailTarget(const CatWorld& world) const;  // getValidTrailTarget (-1: none)
    LevelObject* stealTrail(CatWorld& world);        // stealTrail
    void takeFromTrail(CatWorld& world);             // updateSpecific's stealing
    // canSeePlaything @0x1a10c4: a TV playing in sight; it becomes the target (+0x1d8).
    bool canSeePlaything(CatWorld& world);
    LevelObject* target(CatWorld& world) const;      // the target object (getObjectById)
    float play(int dtMs, CatWorld& world);           // play @0x1a2b90 (state 6)
    float dazewalk(CatWorld& world);                 // dazewalk @0x1a2a88 (state 1)
    void hypnotised(CatWorld& world);                // hypnotised @0x1a29b0 (state 7)
    void turnAndMove(int dtMs, float speed, CatWorld& world);
    void stopPurring(CatWorld& world);
    void scrollWithView(CatWorld& world);
    void attachParticle(CatWorld& world, int type);
    bool whiskerTouches(const CatWorld& world, Vec2 tip) const;
    int cry() const;  // Particles::addParticle with +0x218
    void moveParticle(CatWorld& world) const;        // update's particle follow
    // The way back (Object +0x140, a Path of up to 32 points).
    void setReturnPos(Vec2 target, const CatWorld& world);
    bool followReturnPath(bool backToRoute);
    int closestRoutePoint(const CatWorld& world) const;  // Object::getClosestPathPoint
    bool canSeeGoal(const CatWorld& world) const;        // Object::canSeeTarget

    int objectId_ = -1;
    int type_ = 0;
    int floor_ = 0;
    int state_ = 0;             // +0x1c
    Vec2 position_;             // +0xf4
    Vec2 start_;                // where it was placed
    float radius_ = 0.0f;       // +0x104
    float frameHeight_ = 0.0f;  // +0x100 (its frame is 32 wide, +0xfc)
    float speed_ = 0.0f;        // +0x24
    bool henchmanLaunched_ = false;  // +0x208 (the henchman)
    bool ridesBalloon_ = false;      // world 4's henchman
    bool stoppedForGood_ = false;
    int trimmed_ = -1;  // +0x208: route points dropped so far
    int world_ = -1;    // the profile's world (init by world)
    float initialRadius_ = 0.0f;  // the touch circle postInit gives it
    float balloonX_ = 0.0f;          // +0x20c: the middle of his sway
    int posePart_ = -1;         // +0x21c: which part picture a cutscene shows (2: pose)
    float heading_ = 0.0f;      // +0x108
    float targetHeading_ = 0.0f;  // +0x10c
    float steerOffset_ = 0.0f;  // +0x110
    Vec2 velocity_;             // +0x1ac
    Vec2 pushNormal_;           // +0x1b4
    bool collided_ = false;     // +0x1bc
    CatRoute route_;            // +0x114
    CatRoute startRoute_;       // the route as linked at the level start
    int routeIndex_ = 0;        // +0x16c (the first update moves on to point 1)
    int routeDirection_ = 1;    // +0x174
    std::vector<Vec2> returnPath_;  // +0x140
    int returnIndex_ = 0;       // +0x170 (the first update sets -1: no way back)
    int awarenessMs_ = 0;       // +0x1dc
    int timer1e0_ = 0;          // +0x1e0: twine ignored while positive ("twine_ignore_time")
    bool lurePlayed_ = false;   // state 6 with its target let go (0x1e): getActiveSprite
    int calmMs_ = 0;            // +0x1e4: negative while chasing; positive: sees nothing
    int throwMs_ = 0;           // +0x1f8: the ninja's wait before its next shuriken
    int burstLeft_ = 0;         // +0x1fc: shurikens left in its burst
    int lookMs_ = 0;            // +0x1e8: the "?" stop
    int meanRampMs_ = 0;        // +0x204 (black cat): time running after the mouse, up to 1400
    int chaseTimerMs_ = 0;      // +0x1ec: aim and pounce timing
    int collisionStreak_ = 0;   // +0x1f0
    int steerUntilMs_ = 0;      // +0x1f4
    int chaseStep_ = 0;         // +0x1d4
    bool seenMouse_ = false;    // +0x1d0
    bool seenTrail_ = false;    // +0x1d1
    int targetId_ = 0;          // +0x1d8
    Vec2 seenAt_;               // +0x1c8
    int particle_ = -1;         // +0x218: the "?"/heart over its head
    int facing_ = -1;           // +0x824
    bool purring_ = false;      // LoopingSoundManager: cat_purr_loop for this cat
    int headBumps_ = 0;         // +0x210: pounces that ended in a wall
    int sightings_ = 0;         // +0x214
public:
    // The handle its particle reports to (Particles::add).
    const int* particleHandle() const { return &particle_; }
    int* particleHandle() { return &particle_; }
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(pose, walk, idle, common, balloon, animatedMs, idleMirrored_, objectId_, type_, floor_,
                           state_, position_, start_, radius_, frameHeight_, speed_, henchmanLaunched_,
                           ridesBalloon_, stoppedForGood_, trimmed_, world_, initialRadius_, balloonX_,
                           posePart_, heading_, targetHeading_, steerOffset_, velocity_, pushNormal_, collided_,
                           route_, startRoute_, routeIndex_, routeDirection_, returnPath_, returnIndex_,
                           awarenessMs_, timer1e0_, lurePlayed_, calmMs_, throwMs_, burstLeft_, lookMs_,
                           meanRampMs_, chaseTimerMs_, collisionStreak_, steerUntilMs_, chaseStep_, seenMouse_,
                           seenTrail_, targetId_, seenAt_, particle_, facing_, purring_, headBumps_,
                           sightings_); }
};

}  // namespace doc::game
