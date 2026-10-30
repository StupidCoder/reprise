// The player's mouse: follows the drawn path, slowed by what it carries.
// Reconstructed from Player::update @0x1252bc (normal play: state 0, no platforms, no cats).
#pragma once

#include "game/drawn_path.hpp"
#include "game/random.hpp"
#include "game/sprite_anim.hpp"
#include "game/tuning.hpp"

#include <vector>

namespace doc::game {

class Mouse {
public:
    // The mouse stands on its map entity with its feet on the point: half a sprite height up
    // (Player::init @0x122922: position = entity + (0, size.y * 0.5)).
    void place(Vec2 entity, Vec2 spriteSize);

    // One game tick of `dtMs` milliseconds. `trailWeight` is the summed weight of the carried
    // cheese (0 when carrying nothing); `infinitePath` is the original's "infinite_path"
    // setting (on), which drops consumed points from the front of the path.
    void tick(int dtMs, DrawnPath& path, float trailWeight, const Tuning& tuning, bool infinitePath = true);

    Vec2 position;
    Vec2 size{37.0f, 37.0f};
    float heading = 4.712389f;  // radians, map space (y up); starts facing down
    float reachDistance = 10.0f;  // a path point counts as reached within this squared distance
    int pathIndex = 0;            // next path point to walk to
    int state = 0;                // 0 = normal play, 2 = hidden in a hole; others not recovered
    float acceleration = 1.0f;    // recovers toward 1 after slow-downs
    float scoreBoost = 0.0f;      // speed cheese: "chilli_speed_mult" (4) while it lasts (+0x21c)
    int chilliMs = 0;             // speed cheese time left (+0x220)
    int chilliTotalMs = 0;        // its whole time, "chilli_duration" 16 s (+0x224)
    int boostTimerMs = 0;         // 5x speed while positive (not yet driven)
    bool seenByBoss = false;      // the level's boss cat is in its "seen you" state (0x10)
    bool viewScrolling = false;   // an infinite level's view moves faster than 0.1 a tick
    Vec2 velocity;                // last movement step
    Vec2 stateEntry;              // +0x1fc: where it was when it last hid or came out
    float lastStepLength = 0.0f;
    float distanceWalked = 0.0f;
    bool moving() const { return moving_; }
    float lastSpeed() const { return speed_; }  // this tick's speed before the step (0: standing)
    bool wasDrawing = false;      // the path was being drawn last tick (ends boosts on release)
    int releases = 0;             // finger lifts after drawing
    int snapGlowMs = 0;           // set when the path snaps onto something

    // Going into a hole (Player::hide @0x1248d4, Player::setState @0x123e6c): the mouse
    // leaves the map and its drawn copy slides into the hole while it shrinks.
    int floor = 0;                // map layer the mouse is on
    int cup = -1;                 // object id of the teacup it wears (Player +0x180), else -1
    int hole = -1;                // object id of the hole the mouse is in
    int holeTag = -1;             // tag of the hole when it is not a home hole, else -1
    int lastHole = -1;            // a path may not snap back onto it for a moment
    int lastHoleUntilMs = 0;
    // The hole the mouse last came out of (Player +0x168): touching it does not hide the mouse
    // again until it has been away from it for 100 ms (Player +0x1ec).
    int leftHole = -1;
    int leftHoleUntilMs = 0;
    int slideRemainingMs = 0;
    int slideDurationMs = 0;
    int slideDirection = 0;       // -1 shrinks into the hole, 1 grows out of it
    bool growing = false;         // still growing out of a hole: it does not walk (Player +0x244 < 1)
    Vec2 slideTarget;
    Vec2 slidePosition;           // where the mouse is drawn while hidden
    float drawScale = 1.0f;

    // Footprints (Player::onFootprint @0x121874): every ~10 units a step, alternately left and
    // right of the path; each step sounds (a random choice of two sounds).
    bool footprintSide = true;    // observed 1 at the level start (reference runs)
    Vec2 lastFootprint;
    int wetMs = 0;                // +0x51c: feet that leave visible prints (paint, water)
    int wetKind = 0;              // +0x514: 0 water, 1 black, 2 the paint's own colour
    float wetFactor = 0.0f;       // +0x520: 1 / the time the prints were started with
    float wetRed = 1.0f, wetGreen = 1.0f, wetBlue = 1.0f;  // +0x52c: the paint's colour
    int footprintPending = -1;    // SceneGame per player: prints due when off the paint
    int footprintColour = -1;     // the paint line's colour (its tag, 0xRRGGBB)

    // Drawing the mouse folds its heading into [0, 2pi) once per presented frame
    // (Player::render @0x122932). It changes later movement bit-for-bit, so it is game state.
    void normaliseHeadingForFrame();

    // The mouse's picture: a 4-frame walk cycle and data-driven sequences (idle animations and
    // tricks) registered in Player::loadAssets @0x1270ec's order.
    enum Sequence : int {
        kIdle, kScratch, kFlying, kLanding, kSurprised, kSniff, kCheckMap, kPushWall, kEatCheese,
        kYoyo, kJuggle, kShurikenDeath, kFlameDeath, kFlameDeath2, kSequenceCount
    };
    // Sequence files and the sequence names the slots above refer to.
    static const char* const kSequenceFiles[13];
    static const char* const kSequenceNames[kSequenceCount];
    // `bucket`: the worn cup is world 3's bucket (sprites/bucket01..08, 49 wide).
    void setUpSprite(const std::vector<data::AnimSequence>& sequences, bool bucket = false);
    SpriteAnim sprite;
    int sequenceIds[kSequenceCount] = {};
    // The worn teacup's picture (Player +0x2b0, SceneGame::loadGameAssets @0x105a02:
    // sprites/teacup01..08 by facing, two frames of 34, looped at 200 ms).
    SpriteAnim cupSprite;
    bool bucket = false;
    // Standing still plays idle animations: the plain idle, then alternately the idle facing
    // down and a random trick, and after two minutes a long one (Player::update @0x126a5e).
    int idleMs = 0;     // +0x238
    int idleStep = 0;   // +0x234
    // After the tick's movement: walk cycle while moving, idle choices while standing.
    // `cupRuns`: the worn cup's picture is the one running this tick (it takes the walk).
    void updateAnimation(int dtMs, GameRandom& random, bool cupRuns = false);
    // Player::setState @0x124478 with a teacup on: any sequence but the plain idle (+0x26c) stops.
    void stopIdleSequence() {
        if (sprite.currentSequence() != sequenceIds[kIdle]) sprite.stopSequence();
    }

private:
    bool moving_ = false;
    float speed_ = 0.0f;
};

// Speed factor table for carried cheese (Player::updateTrail @0x1236c0): the weight of one
// item by its object type.
float carriedWeight(int objectType, const Tuning& tuning);
// Weight of a whole trail of carried items (same routine): the average item weight, truncated
// to a whole number, plus chain_mod times that for every further item.
float trailWeight(const int* objectTypes, int count, const Tuning& tuning);

}  // namespace doc::game
