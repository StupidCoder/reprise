// Recovered player action machine (original game, ARM11 main code): the movement controller's
// state machine (0x3b9b40 subset, machine 0x334e7824), its transition conditions and the actions of
// the states reachable in the opening with stick and dash input. Corrected decompilation with the
// original operation order (compile with -ffp-contract=off).
//
// How the player is controlled: the protagonist is always in exactly one "state" (Wait, Walk, Fall,
// Land, Turn, ...). Each state has an action object with enter / update / exit functions and a
// collider pass, and an ordered list of transitions to other states. Every tick the machine:
//   1. applies a state change chosen on the previous tick, or evaluates the current state's
//      transitions (first true condition wins) and applies that change at once;
//   2. runs the state's collider pass (move against the terrain, PlayerCollider.hpp);
//   3. cuts the velocity against the contacts;
//   4. runs the state's update (for example the walk physics, PlayerWalk.hpp);
//   5. updates the animation frame mirror and advances the skeleton (PlayerAnim.hpp).
// The transition graph (order and shared condition objects) is generated from the game's runtime
// objects: tools/gen_player_graph.py -> src/PlayerGraph.inc.
//
// Addresses:
//   machine     0x1bace8 evaluate, 0x2d9c20 apply (old exit, new enter, conditions reset),
//               0x1badc8 collider pass (action +0x10), 0x1bade4 update (action +0x14)
//   Wait        0x1a8b78 pass, 0x1a8ba0 enter, 0x1a8c8c update, 0x1a8e08 exit
//   Idle        0x1e4758 enter, 0x1e47bc update, 0x1e49b8 exit, 0x3e8ec8 / 0x3e8f20 variant, 0x3e90c0 done
//   Walk        RecoveredPlayerWalk (0x1f3964 pass = collider tick)
//   Pivot       TurnAround / RunTurn: 0x1a8a9c pass, 0x1a8ae4 enter, 0x2b53a0 update (0x2b53f4 gravity)
//   Fall        0x2b6180 pass (air tick), 0x2b5c5c enter (0x2b5eec), 0x1a7e24 update (0x2b5858 air
//               move: 0x1d755c, 0x1a7fe0, 0x1d7c1c, 0x1d77ec, 0x1d7710; 0x2b5cbc animation), 0x1a7eac exit
//   Land        0x1a8424 pass, 0x1a8438 enter, 0x1a8a2c update (empty), 0x1a8a30 exit
//   PullOutSign (dash start) 0x1fd82c / 0x1fd840 / 0x1fd8a8; DashWait 0x1fd940 / 0x1fd954 / 0x1fd9c4
//   RunSign     0x1d7edc / 0x1d7ef0 / 0x1d7f68; Turn 0x1bae2c / 0x1bae54 / 0x1baff4 / 0x1bb37c
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "game/Pad.hpp"
#include "game/PlayerAnim.hpp"
#include "game/PlayerCollider.hpp"
#include "game/PlayerWalk.hpp"

namespace captain_toad::RecoveredPlayerActions {

using captain_toad::RecoveredMath::bitsf;
using captain_toad::RecoveredMath::fbits;
using captain_toad::RecoveredPlayerCollider::Body;
using captain_toad::RecoveredPlayerCollider::Collider;
using captain_toad::RecoveredVec::V3;

// PlayerConst values used outside the walk (the defaults equal the runtime values at the checkpoint).
struct ActionParams {
    float gravity = 6.0f;                              // 0x00
    int32_t waitFrame = 90;                            // 0x7c  Wait ticks before the idle animations
    uint32_t pivotFrame = 5;                           // 0x80
    uint32_t pivotFrameInvinciblePickaxe = 1;          // 0x84 (field 0x88)
    float pivotDegree = 25.0f;                         // 0x88
    float tornadoSum = 1900.0f;                        // 0x90 TornadoSpinStickRotateAngleDegreeSum
    float tornadoPerFrame = 11.5f;                     // 0x94 TornadoSpinStickRotateAngleDegreePerFrame
    float oppositeAngleDegree = 150.0f;                // 0xa4
    float oppositeInputMoveVecLength = 0.85f;          // 0xa8
    int32_t runSignDashButtonHoldFrame = 0;            // 0x78
    uint32_t fallAnimStartStep = 5;                    // 0x1c4
    float fallStartSlideOffset = 0.0f;                 // 0x1c8
    float fallPanicStartHeight = 700.0f;               // 0x1cc
    float fallSpeedMax = 90.0f;                        // 0x1d0
    float fallSpeedMaxWater = 20.0f;                   // 0x1d4
    float speedDampAir = 0.98f;                        // 0x1d8
    float speedDampWater = 0.97f;                      // 0x1dc
    float normalSpeedMaxAir = 0.0f;                    // 0x1e0
    float normalSpeedMaxWater = 0.0f;                  // 0x1e4
    float moveSideVelRate = 0.125f;                    // 0x1e8
    float moveFrontVelRate = 0.04375f;                 // 0x1ec
    float moveSideFrontBrakeRate = 0.0253f;            // 0x1f0
    float landPanicCancelFrame = 50.0f;                // 0x208
    float jumpRotBlendRate = 0.106f;                   // 0x21c
    uint32_t jumpAccelAddFrame = 30;                   // 0x220
    float pullOutAngle = 83.0f;                        // 0x64 PullOutAngle (degrees)
    float pullOutAngleCheckDisableDistance = 70.0f;    // 0x68
    float pullOutSnapDistance = 100.0f;                // 0x50
    float pullOutDirectTargetEnableDistance = 50.0f;   // 0x6c
    uint32_t pullOutCancelableFrame = 6;               // 0x54
    // Carrying (PlayerConst defaults, evidence/player/params.json).
    uint32_t pullOutSuccessCarryHoldFrame = 5;   // 0x4c (getter vtable +0x48)
    uint32_t holdThrowFrontTiming = 2;           // 0x420 (getter vtable +0x41c)
    uint32_t holdThrowUpTiming = 2;              // 0x424 (getter vtable +0x420)
    // Damage and invincibility.
    uint32_t damageInvalidCount = 90;   // 0x368 (getter vtable +0x364)
    uint32_t invincibleFrame = 270;     // 0x370 (getter vtable +0x36c)
};

// AreaCubeBase boxes of the stage map (0x297768: strict min < p - translate < max, unrotated).
struct AreaBox {
    V3 translate, min, max;
    std::string animName;
    int32_t startFrame = 0;
    int32_t interval = -1;
};

inline bool contains(const AreaBox& a, const V3& p) {
    V3 l = captain_toad::RecoveredVec::sub(p, a.translate);
    if (!(l.x > a.min.x) || !(l.x < a.max.x)) {
        return false;
    }
    if (!(l.y > a.min.y) || !(l.y < a.max.y)) {
        return false;
    }
    return l.z > a.min.z && l.z < a.max.z;
}

// The stage areas the player queries.
struct Areas {
    std::vector<AreaBox> death, playerAnim, water;

    const AreaBox* playerAnimAt(const V3& p) const {
        for (auto& a : playerAnim) {
            if (contains(a, p)) {
                return &a;
            }
        }
        return nullptr;
    }
    bool deathAt(const V3& p) const {
        for (auto& a : death) {
            if (contains(a, p)) {
                return true;
            }
        }
        return false;
    }
    bool waterAt(const V3& p) const {
        for (auto& a : water) {
            if (contains(a, p)) {
                return true;
            }
        }
        return false;
    }
};

// Everything the actions and conditions read.
struct Context {
    Body* body = nullptr;
    Collider* collider = nullptr;
    captain_toad::RecoveredPad::Pad* pad = nullptr;
    captain_toad::RecoveredPlayerAnim::Player* anim = nullptr;
    const Areas* areas = nullptr;
    captain_toad::RecoveredPlayerWalk::WalkParams walkParams;
    ActionParams params;
    uint32_t buttons0 = 0;                    // button object 0x334e7520 (+0 held word)
    uint32_t buttons1 = 0;                    // (+4 word)
    bool dashTrigger = false;                 // pad vtable +0x64: dash buttons pressed this frame
    int32_t carryKind = 5;                    // 0x334e4c24 +8 (5: nothing carried)
    int32_t carryMode = 5;                    // +0xc
    bool damageFlag = false;                  // 0x334e7574 +0x1c (hit reaction)
    std::vector<std::string> soundRequests;   // the player's sound keeper requests of the actions
    std::vector<std::string> reactions;       // the player's hit reactions of the actions (this tick)
    bool invincibleFlag = false;              // 0x334e7594 +0x1c (star)
    V3 missLookTarget{0, 0, 0};               // the Miss action's look target (object 0x334e4da4, vtable 0x4caa20)
    bool haveMissLookTarget = false;
    // Every material of the opening's collision (its attribute file) has FloorCode "Ground";
    // the ground material conditions (0x2b5484) compare that name.
    std::string floorCode = "Ground";

    // Fall object +0x68 vtable +0 (0x20e590): is there terrain along `dir` from `from`?
    bool fallRay(const V3& from, const V3& dir) const {
        std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
        return captain_toad::RecoveredCollision::segmentCheck(collider->parts, from, dir, hits) != 0;
    }

    // Rendering-side hooks of the presentation object at ctx+0x20 (look controller); no movement state.
    void lookHook(int) {}
};

// ---- Transition conditions ------------------------------------------------------------------------

struct Pred {
    uint32_t address = 0;   // runtime address of the original condition object
    virtual ~Pred() = default;
    virtual bool check() = 0;
    virtual void reset() {}
};

using Fn = std::function<bool()>;

// A plain query.
struct Leaf : Pred {
    Fn f;
    explicit Leaf(Fn function) : f(std::move(function)) {}
    bool check() override { return f(); }
};

// 0x202220
struct Not : Pred {
    Pred* c;
    explicit Not(Pred* condition) : c(condition) {}
    bool check() override { return !c->check(); }
    void reset() override { c->reset(); }
};

// 0x214614: every element is evaluated (no short-circuit; stateful elements always update).
struct AndAll : Pred {
    std::vector<Pred*> items;
    explicit AndAll(std::vector<Pred*> conditions) : items(std::move(conditions)) {}
    bool check() override {
        bool result = true;
        for (auto* p : items) {
            bool v = p->check();
            result = v && result;
        }
        return result;
    }
    void reset() override {
        for (auto* p : items) {
            p->reset();
        }
    }
};

// 0x1fd768: every element is evaluated.
struct OrAll : Pred {
    std::vector<Pred*> items;
    explicit OrAll(std::vector<Pred*> conditions) : items(std::move(conditions)) {}
    bool check() override {
        bool result = false;
        for (auto* p : items) {
            bool v = p->check();
            result = v || result;
        }
        return result;
    }
    void reset() override {
        for (auto* p : items) {
            p->reset();
        }
    }
};

// 0x214580 (reset 0x2145d4): true once the condition has held for n consecutive ticks.
struct Consecutive : Pred {
    Pred* c;
    uint32_t n;
    uint32_t count = 0;
    Consecutive(Pred* condition, uint32_t ticks) : c(condition), n(ticks) {}
    bool check() override {
        if (c && !c->check()) {
            count = 0;
            return count == n;
        }
        if (count < n) {
            count++;
        }
        return count == n;
    }
    void reset() override {
        if (c) {
            c->reset();
        }
        count = 0;
    }
};

// 0x3700bc (reset 0x3700f8): true once the query has been true n times since the last reset.
struct CountAtLeast : Pred {
    Fn f;
    int32_t n;
    int32_t count = 0;
    CountAtLeast(Fn function, int32_t times) : f(std::move(function)), n(times) {}
    bool check() override {
        if (f()) {
            count = count + 1;
        }
        return count >= n;
    }
    void reset() override { count = 0; }
};

// ---- Actions ----------------------------------------------------------------------------------

struct Action {
    Context* ctx = nullptr;
    virtual ~Action() = default;
    virtual void colliderPass() { ctx->collider->tick(); }
    virtual void enter() {}
    virtual void update() {}
    virtual void exit() {}

    // vel += groundUp * -(groundUp . vel): drop the velocity component along the ground normal.
    void removeNormalVelocity() {
        Body& b = *ctx->body;
        float d = b.groundUp.x * b.vel.x;
        d = d + b.groundUp.y * b.vel.y;
        d = -d - b.groundUp.z * b.vel.z;
        b.vel = captain_toad::RecoveredVec::addScaled(b.groundUp, b.vel, d);
    }

    void ensure(const std::string& name) {
        if (!ctx->anim->isPlaying(name)) {
            ctx->anim->start(name);
        }
    }
};

// Wait (vtable 0x4c4dac; also the base of Idle). Flags at +0x18..+0x1a (opening: 0, 0, 1).
struct WaitAction : Action {
    int32_t counter = 0;             // +0x08
    bool airPass = false;            // +0x18
    bool runSignUpper = false;       // +0x19 (upper-body sign animation; off for Wait and Idle)
    bool enterHook = true;           // +0x1a
    std::string name1 = "Wait";      // vtable +0x24
    std::string name2 = "RunSign";   // vtable +0x28

    void colliderPass() override {   // 0x1a8b78
        if (airPass) {
            ctx->collider->airTick();
        } else {
            ctx->collider->tick();
        }
    }

    void enter() override {   // 0x1a8ba0
        counter = 0;
        ctx->body->vel = V3{0, 0, 0};
        ensure(name1);
        if (enterHook) {
            ctx->lookHook(0);
        }
    }

    void update() override {   // 0x1a8c8c
        counter = counter + 1;
        if (runSignUpper) {
            throw std::runtime_error("wait: upper-body RunSign branch (+0x19) not ported");
        }
        if (ctx->collider->onGround()) {
            removeNormalVelocity();
        }
    }

    void exit() override {   // 0x1a8e08
        ctx->lookHook(4);    // (+0x10 object 0x21a7c8 clears two counters)
    }
};

// Idle (vtable 0x4c9e24): standing still for a while plays idle animations. It is Wait plus the
// idle variants of table 0x48021c (two alternating sets of WaitRuck / WaitRove or WaitYawn /
// WaitSitDown / WaitSleepLoop with start frames 0 / 360 / 900), or a PlayerAnimArea's animation.
struct IdleVariant {
    const char* name;
    int32_t startFrame;
    int32_t loops;
};

static const IdleVariant kIdleTable[2][4] = {
    {{"WaitRuck", 0, -1}, {"WaitRove", 360, -1}, {"WaitSitDown", 900, -1}, {"WaitSleepLoop", 0, -1}},
    {{"WaitRuck", 0, -1}, {"WaitYawn", 360, -1}, {"WaitSitDown", 900, -1}, {"WaitSleepLoop", 0, -1}},
};

struct IdleAction : WaitAction {
    int32_t counter2 = 0;            // +0x20
    const AreaBox* area = nullptr;   // +0x28 area info (PlayerAnimArea)
    bool inArea = false;             // +0x2c
    bool firstInArea = false;        // +0x2d
    int32_t set = 0;                 // +0x30 which of the two variant sets
    int32_t stage = 0;               // +0x34 position in the set
    bool special = false;            // +0x3c (object +0x38 query; false in the opening)

    // Table entry +0xc: the next entry's name (all four entries are named; stage 3 is the last).
    bool nextNamed() const { return stage + 1 < 4; }

    std::string variantName() const {   // 0x3e8ec8
        if (inArea) {
            return area->animName;
        }
        if (special) {
            return "Sneeze";   // name at 0x48027c
        }
        return kIdleTable[set][stage].name;
    }

    int32_t variantFrame() const {   // 0x3e8f20
        if (inArea) {
            if (!firstInArea && area->interval != -1) {
                return area->interval;
            }
            return area->startFrame;
        }
        if (special) {
            return 0;
        }
        return kIdleTable[set][stage].startFrame;
    }

    void enter() override {   // 0x1e4758
        WaitAction::enter();
        area = ctx->areas ? ctx->areas->playerAnimAt(ctx->body->pos) : nullptr;
        inArea = area != nullptr;
        counter2 = 0;
        stage = 0;
        firstInArea = true;
        special = false;
    }

    void update() override {   // 0x1e47bc
        WaitAction::update();
        bool consider = inArea || (!special && stage < 3 && nextNamed());
        if (consider) {
            if (ctx->anim->isOneShot()) {
                if (ctx->anim->isPlaying(variantName()) && ctx->anim->isEnd()) {
                    stage = stage + 1;
                    counter2 = 0;
                    firstInArea = false;
                    ctx->anim->start("Wait");
                }
            } else if (!inArea && !special) {
                const int32_t threshold = kIdleTable[set][stage].loops;
                if (threshold > 0 && counter2 > threshold) {
                    stage = stage + 1;
                    counter2 = 0;
                    firstInArea = false;
                    ctx->anim->start("Wait");
                }
            }
        }
        if (variantFrame() == counter2) {
            ctx->anim->start(variantName());
        }
        counter2 = counter2 + 1;
    }

    void exit() override {   // 0x1e49b8: alternate the variant set
        ctx->lookHook(4);
        int32_t s = set + 1;
        set = s - ((s + (int32_t(uint32_t(s) >> 31))) & ~1);
    }

    bool done() {   // 0x3e90c0 (the idle state's exit request)
        if (inArea) {
            const AreaBox* now = ctx->areas ? ctx->areas->playerAnimAt(ctx->body->pos) : nullptr;
            if (!now || now != area) {
                return true;
            }
        }
        if (ctx->anim->isOneShot()) {
            if (!ctx->anim->isPlaying(variantName()) || !ctx->anim->isEnd()) {
                return false;
            }
            if (inArea) {
                return false;
            }
            return special || stage >= 3 || !nextNamed();
        }
        if (inArea || special) {
            return false;
        }
        const int32_t threshold = kIdleTable[set][stage].loops;
        if (!(threshold > 0 && counter2 > threshold)) {
            return false;
        }
        return stage >= 3 || !nextNamed();
    }
};

// Walk: the Move state, implemented in PlayerWalk.hpp.
struct WalkAction : Action {
    captain_toad::RecoveredPlayerWalk::Walk walk;
    captain_toad::RecoveredPlayerWalk::PadState padState;

    void bind() {
        walk.body = ctx->body;
        walk.collider = ctx->collider;
        walk.anim = ctx->anim;
        walk.params = ctx->walkParams;
    }

    void syncPad() {
        padState.move = ctx->pad->move;
        padState.stickActive = ctx->pad->stickActive;
        padState.dashHeld = ctx->pad->dashHeld;
        padState.stickRotate = ctx->pad->rotWindow;
        walk.pad = &padState;
    }

    void enter() override {
        bind();
        syncPad();
        walk.services.inWater = ctx->areas && ctx->areas->waterAt(ctx->body->pos);
        walk.enter();
    }
    void update() override {
        bind();
        syncPad();
        // walk+0x4c vtable +4 (0x3e4ad4 -> 0x2ed8b8): the player's position in a WaterArea scales
        // the walk speed by SpeedDampWaterGround.
        walk.services.inWater = ctx->areas && ctx->areas->waterAt(ctx->body->pos);
        walk.update();
    }
    void exit() override {
        bind();
        walk.exit();
    }
};

// TurnAround / RunTurn (vtable 0x4c4d80): a quick about-face when the stick is pulled backwards.
struct PivotAction : Action {
    std::string name;   // vtable +0x20
    explicit PivotAction(std::string clipName) : name(std::move(clipName)) {}

    void colliderPass() override {   // 0x1a8a9c
        if (ctx->collider->onGround()) {
            ctx->collider->tick();
        } else {
            ctx->collider->airTick();
        }
    }

    void enter() override {   // 0x1a8ae4
        Body& b = *ctx->body;
        V3 f{-b.front.x, -b.front.y, -b.front.z};
        b.front = f;   // 0x2dc0a8
        ctx->anim->start(name);
    }

    void update() override {   // 0x2b53a0 -> 0x2b53f4(body, collider, Gravity, FallSpeedMax)
        Body& b = *ctx->body;
        const float g = ctx->params.gravity;
        const float maxFall = ctx->params.fallSpeedMax;
        const float zero = 0.0f;
        if (ctx->collider && ctx->collider->onGround()) {
            if (b.vel.y < zero) {
                b.vel.y = zero;
            }
        }
        b.vel = captain_toad::RecoveredVec::add(b.vel, V3{zero, -g, zero});
        if (b.vel.y < -maxFall) {
            b.vel.y = -maxFall;
        }
    }
};

// Fall (vtable 0x4c4cd4, air family base 0x2b5664): in the air after walking off a ledge.
// Gravity pulls down, air drag slows the horizontal speed, and the stick steers a little.
struct FallAction : Action {
    // InvincibleFall (vtable 0x4ce1e0): enter 0x2b5ec4 (the shared set-up only), update 0x36e29c
    // (the air move 0x2b5858 only: the "Invincible" clip keeps playing), exit 0x36e2a0 (the sound
    // "PgKinotanFallLand", then the shared exit 0x2b5724).
    bool invincible = false;
    bool noInput = false;       // +0x0c
    uint32_t accelCount = 0;    // +0x10
    bool turnAir = false;       // +0x14
    int32_t stun = 0;           // +0x18
    V3 dir0;                    // +0x1c initial horizontal direction
    V3 side;                    // +0x28
    bool skipRotate = false;    // +0x34
    float accelRatio = 0;       // +0x3c
    bool noSide = false;        // +0x40
    int32_t ceilingCount = 0;   // +0x44
    bool prevCeiling = false;   // +0x48
    bool forbidBack = false;    // +0x49
    uint32_t ticks = 0;         // +0x60
    uint32_t fallLoops = 0;     // +0x64
    bool water = false;         // +0x70

    bool inWater() const { return ctx->areas && ctx->areas->waterAt(ctx->body->pos); }

    // vtable +0x28 (0x3e56d8)
    float airSpeedMax(bool inWaterNow) const {
        return inWaterNow ? ctx->params.normalSpeedMaxWater : ctx->params.normalSpeedMaxAir;
    }

    void colliderPass() override { ctx->collider->airTick(); }   // 0x2b6180

    void enter() override {   // 0x2b5c5c
        if (invincible) {
            init();
            return;
        }
        // Collider vtable +0x20 (0x184858): the inherited ground velocity = follow delta (zero here).
        init();
        water = false;   // areas vtable +0x24 (water info); no water areas in the opening
        ticks = 0;
        fallLoops = 0;
    }

    void init() {   // 0x2b5eec
        using namespace captain_toad::RecoveredVec;
        Body& b = *ctx->body;
        const float eps = fbits(0x358637be);
        const float zero = 0.0f;
        // The horizontal direction of travel (or the front when not moving).
        dir0 = b.vel;
        {
            float l2 = dir0.x * dir0.x;
            l2 = l2 + dir0.y * dir0.y;
            l2 = l2 + dir0.z * dir0.z;
            if (l2 < eps) {
                dir0 = V3{zero, zero, zero};
            } else {
                normalize(dir0);
            }
        }
        {
            float d = b.up.x * dir0.x;
            d = d + b.up.y * dir0.y;
            d = -d - b.up.z * dir0.z;
            dir0 = addScaled(b.up, dir0, d);
        }
        {
            float l2 = dir0.x * dir0.x;
            l2 = l2 + dir0.y * dir0.y;
            l2 = l2 + dir0.z * dir0.z;
            if (l2 < eps) {
                dir0 = V3{zero, zero, zero};
                dir0 = b.front;
            } else {
                normalize(dir0);
            }
        }
        {
            V3 o = scale(dir0, ctx->params.fallStartSlideOffset);
            b.pos = add(b.pos, o);
        }
        const bool inWaterNow = inWater();
        const float s0 = airSpeedMax(inWaterNow);
        float ratio;
        if (s0 > zero) {
            float s1 = dir0.x * b.vel.x;
            s1 = s1 + dir0.y * b.vel.y;
            s1 = s1 + dir0.z * b.vel.z;
            if (s1 <= zero) {
                s1 = -s1;
            }
            float k = s0 * fbits(0x3f666666);
            ratio = s1 / k;
            if (ratio < zero) {
                ratio = zero;
            } else if (ratio > 1.0f) {
                ratio = 1.0f;
            }
        } else {
            ratio = zero;
        }
        accelRatio = ratio;
        side = cross(b.up, dir0);
        normalize(side);
        accelCount = 0;
        ceilingCount = 0;
        turnAir = false;
        prevCeiling = false;
        ctx->lookHook(0);
    }

    void update() override {   // 0x1a7e24
        if (invincible) {
            airMove();
            return;
        }
        ticks = ticks + 1;
        if (ctx->anim->isPlaying("Fall")) {
            fallLoops = fallLoops + 1;
        }
        airMove();
        selectAnimation();
    }

    void exit() override {   // 0x1a7eac -> 0x2b5724
        if (invincible) {
            ctx->soundRequests.push_back("PgKinotanFallLand");
        }
        static const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        memcpy(ctx->body->mtx30, identity, sizeof identity);
        if (ctx->anim->isPlaying("TurnAirL") || ctx->anim->isPlaying("TurnAirR")) {
            ctx->anim->start("TurnAirEnd");
        }
        ctx->lookHook(4);
    }

    void airMove() {   // 0x2b5858
        using namespace captain_toad::RecoveredVec;
        Body& b = *ctx->body;
        Collider& c = *ctx->collider;
        const float zero = 0.0f;
        if (c.lists[2].valid && !prevCeiling) {
            ceilingCount = ceilingCount + 1;
        }
        prevCeiling = c.lists[2].valid;
        auto velocityDotGravity = [&] {
            float s = b.vel.x * b.gravity.x;
            s = s + b.vel.y * b.gravity.y;
            s = s + b.vel.z * b.gravity.z;
            return s;
        };
        // How long the stick has been held (steering builds up over jumpAccelAddFrame ticks).
        if (c.lists[1].valid) {
            accelCount = ctx->params.jumpAccelAddFrame;
        } else if (!(velocityDotGravity() < zero)) {
            accelCount = ctx->params.jumpAccelAddFrame;
        } else {
            bool done = false;
            if (ceilingCount == 1 && c.lists[2].valid && velocityDotGravity() > zero) {
                accelCount = 0;
                accelRatio = zero;
                done = true;
            }
            if (!done) {
                if (!ctx->pad->stickActive) {
                    accelCount = accelCount >> 1;
                } else if (ctx->params.jumpAccelAddFrame > accelCount) {
                    accelCount = accelCount + 1;
                }
            }
        }
        if (turnAir) {
            turnAir = false;
            static const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            memcpy(b.mtx30, identity, sizeof identity);
        }

        // Split the velocity into horizontal (damped) and vertical parts.
        V3 horizontalVelocity;
        {
            float d = b.groundUp.x * b.vel.x;
            d = d + b.groundUp.y * b.vel.y;
            d = -d - b.groundUp.z * b.vel.z;
            horizontalVelocity = addScaled(b.groundUp, b.vel, d);
        }
        const bool inWaterNow = inWater();
        horizontalVelocity =
            scale(horizontalVelocity, inWaterNow ? ctx->params.speedDampWater : ctx->params.speedDampAir);
        float vy = b.groundUp.x * b.vel.x;
        vy = vy + b.groundUp.y * b.vel.y;
        vy = vy + b.groundUp.z * b.vel.z;
        b.vel = horizontalVelocity;

        // Stick strength: 0 below 0.1, 1 at full tilt.
        V3 dir = ctx->pad->move;   // vtable +0x34 (0x3e56ac)
        float strength;
        {
            float l2 = dir.x * dir.x;
            l2 = l2 + dir.y * dir.y;
            l2 = l2 + dir.z * dir.z;
            float s = std::sqrt(l2);
            s = s - fbits(0x3dcccccd);
            s = s * fbits(0x3f8e38e4);
            if (s < zero) {
                s = zero;
            } else if (s > 1.0f) {
                s = 1.0f;
            }
            strength = s;
        }
        if (stun != 0 || noInput) {
            strength = zero;
            if (stun != 0) {
                stun = stun - 1;
            }
        }
        V3 d = dir;
        {
            float l2 = d.x * d.x;
            l2 = l2 + d.y * d.y;
            l2 = l2 + d.z * d.z;
            if (l2 < fbits(0x358637be)) {
                d = side;
            } else {
                normalize(d);
                rotateFront();
            }
        }
        horizontal(b.vel, d, strength, horizontalVelocity, inWaterNow);
        vertical(vy, inWaterNow);
    }

    void rotateFront() {   // 0x1d755c: face the stick direction (turning sharply plays a mid-air turn)
        using namespace captain_toad::RecoveredVec;
        if (skipRotate) {
            return;
        }
        Body& b = *ctx->body;
        V3 dir = ctx->pad->move;
        normalize(dir);
        V3 f = b.front;
        {   // 0x1a7fe0: project onto the ground plane, fall back to the input direction
            float k = b.groundUp.x * f.x;
            k = k + b.groundUp.y * f.y;
            k = -k - b.groundUp.z * f.z;
            f = addScaled(b.groundUp, f, k);
            if (captain_toad::RecoveredPad::normalizeOrZero(f)) {
                f = dir;
            }
        }
        b.front = dir;
        if (!turnAir) {
            // ctx+0x20 vtable +0x1c: look mode != 3 (mode 3 in the opening -> false).
            float d = f.x * b.front.x;
            d = d + f.y * b.front.y;
            d = d + f.z * b.front.z;
            // cmn bits(d), 0x40cafb0d; bls skip: turn when bits(d) > 0xbf3504f3 (d < -0.7071068).
            if (bitsf(d) > 0xbf3504f3u) {
                turnInAir(f);
            }
            return;
        }
        const float rate = ctx->params.jumpRotBlendRate;
        V3 a = scale(b.front, rate);
        V3 o = scale(f, 1.0f - rate);
        b.front = add(a, o);
        normalize(b.front);
    }

    void turnInAir(const V3& oldFront) {   // 0x1d7c1c
        using namespace captain_toad::RecoveredVec;
        Body& b = *ctx->body;
        turnAir = true;
        V3 axis = cross(oldFront, b.front);
        float l2 = axis.x * axis.x;
        l2 = l2 + axis.y * axis.y;
        l2 = l2 + axis.z * axis.z;
        float angle;
        if (l2 < fbits(0x358637be)) {
            angle = fbits(0x3fc90fdb);
        } else {
            normalize(axis);
            float d = b.groundUp.x * axis.x;
            d = d + b.groundUp.y * axis.y;
            d = d + b.groundUp.z * axis.z;
            angle = d > 0.0f ? fbits(0x3fc90fdb) : fbits(0xbfc90fdb);
        }
        captain_toad::RecoveredMath::Quat q = captain_toad::RecoveredMath::fromAxisAngle(b.groundUp, angle);
        captain_toad::RecoveredVec::M34 m = captain_toad::RecoveredMath::toMatrix(q);
        memcpy(b.mtx30, m.m, sizeof b.mtx30);
        // ctx+8 vtable +0 ("TurnAir"): sound request.
    }

    // 0x1d77ec: horizontal air control (forward and sideways acceleration, clamped per axis).
    void horizontal(V3& velocityOut, const V3& d, float strength, const V3& h, bool inWaterNow) {
        using namespace captain_toad::RecoveredVec;
        const float zero = 0.0f;
        const float one = 1.0f;
        const float maxSpeed = airSpeedMax(inWaterNow);
        {
            float s2 = float(accelCount);
            float s1 = accelRatio;
            float s3 = one - s1;
            s3 = s3 * s2;
            float t = s3 / float(ctx->params.jumpAccelAddFrame);
            float r = t + s1;
            if (r < zero) {
                r = zero;
            } else if (r > one) {
                r = one;
            }
            accelRatio = r;
        }
        float frontFactor = one;
        float frontAccel = zero;
        float sideAccel = zero;
        if (strength > zero) {
            float dd = dir0.x * d.x;
            dd = dd + dir0.y * d.y;
            dd = dd + dir0.z * d.z;
            float s0;
            if (dd > zero) {
                s0 = int32_t(bitsf(dd)) > int32_t(0x3db27eb6) ? (dd - fbits(0x3db27eb6)) * fbits(0x3f8c3898) : zero;
            } else {
                s0 = bitsf(dd) > 0xbdb27eb6u ? (dd + fbits(0xbdb27eb6)) * fbits(0x3f8c3898) : zero;
            }
            frontFactor = s0;
            float sd = side.x * d.x;
            sd = sd + side.y * d.y;
            sd = sd + side.z * d.z;
            float f = ctx->params.moveFrontVelRate * maxSpeed;
            f = f * frontFactor;
            f = f * strength;
            frontAccel = f * accelRatio;
            float s = ctx->params.moveSideVelRate * maxSpeed;
            s = s * sd;
            sideAccel = s * strength;
        }
        if (noSide) {
            sideAccel = zero;
        }
        V3 a = add(h, scale(dir0, frontAccel));
        V3 bVector = add(a, scale(side, sideAccel));
        float frontComponent = dir0.x * bVector.x;
        frontComponent = frontComponent + dir0.y * bVector.y;
        frontComponent = frontComponent + dir0.z * bVector.z;
        V3 f = scale(dir0, frontComponent);
        float sideComponent = side.x * bVector.x;
        sideComponent = sideComponent + side.y * bVector.y;
        sideComponent = sideComponent + side.z * bVector.z;
        V3 g = scale(side, sideComponent);
        V3 rest = sub(bVector, f);
        rest = sub(rest, g);
        auto clampLength = [&](V3& v) {
            float l2 = v.x * v.x;
            l2 = l2 + v.y * v.y;
            l2 = l2 + v.z * v.z;
            float l = std::sqrt(l2);
            if (l > maxSpeed && l > zero) {
                float k = maxSpeed / l;
                v = {v.x * k, v.y * k, v.z * k};
            }
        };
        clampLength(f);
        clampLength(g);
        if (frontFactor == zero) {
            float k = one;
            k = k - ctx->params.moveSideFrontBrakeRate * strength;
            f = scale(f, k);
        }
        if (forbidBack) {
            float s = dir0.x * f.x;
            s = s + dir0.y * f.y;
            s = s + dir0.z * f.z;
            if (s < zero) {
                f = V3{zero, zero, zero};
            }
        }
        V3 fg = add(f, g);
        velocityOut = add(fg, rest);
    }

    // 0x1d7710: gravity, terminal speed and damping of the vertical speed.
    void vertical(float vy, bool inWaterNow) {
        using namespace captain_toad::RecoveredVec;
        Body& b = *ctx->body;
        vy = vy - ctx->params.gravity;
        const float limit = -(inWaterNow ? ctx->params.fallSpeedMaxWater : ctx->params.fallSpeedMax);
        vy = vy > limit ? vy : limit;
        vy = (inWaterNow ? ctx->params.speedDampWater : ctx->params.speedDampAir) * vy;
        V3 t = scale(b.groundUp, vy);
        b.vel = add(b.vel, t);
    }

    // 0x2b5cbc: a few ticks into the fall, a ray down decides between a normal fall and a panic
    // fall (no ground within 700 units).
    void selectAnimation() {
        if (water) {
            throw std::runtime_error("fall: water branch not ported");
        }
        if (ctx->params.fallAnimStartStep != ticks) {
            return;
        }
        V3 down{-0.0f, -1.0f, -0.0f};
        V3 ray = captain_toad::RecoveredVec::scale(down, ctx->params.fallPanicStartHeight);
        if (ctx->fallRay(ctx->body->pos, ray)) {
            ctx->anim->start("Fall");
        } else {
            ctx->anim->start("FallPanic");
        }
    }
};

// Land (vtable 0x4c4d28): picks the landing animation that matches the jump or fall.
struct LandAction : Action {
    bool hook = true;   // +0x10

    void enter() override {   // 0x1a8438
        auto& anim = *ctx->anim;
        auto playing = [&](const char* name) {
            return anim.isPlaying(name);
        };
        // The player's landing reaction (0x15ce40, in this order): the dash panel's (not ported),
        // in water 着地[水中], after the panic fall 着地[高所], on poison or lava 着地[毒沼or溶岩]
        // (not ported), otherwise 着地.
        if (ctx->areas && ctx->areas->waterAt(ctx->body->pos)) {
            ctx->reactions.push_back("着地[水中]");
        } else if (playing("FallPanic")) {
            ctx->reactions.push_back("着地[高所]");
        } else {
            ctx->reactions.push_back("着地");
        }
        if (playing("JumpTurn")) {
            anim.start("LandTurn");
        } else if (playing("JumpBack") || playing("JumpBackHigh") || playing("TrampleJumpBack")) {
            anim.start("SquatLand");
        } else if (playing("RollingAir") || playing("RollingAirShort") || playing("JumpBroad")) {
            anim.start("LandRolling");
        } else if (playing("WallHit")) {
            anim.start("WallHitLand");
        } else if (playing("BodyAttack")) {
            anim.start("BodyAttackLand");
        } else if (playing("GiantJump") || playing("GiantJump2") || playing("GiantJump3") || playing("GiantFall")) {
            anim.start("GiantLand");
        } else if (playing("FallPanic")) {
            if (ctx->areas && ctx->areas->waterAt(ctx->body->pos)) {
                anim.start("Land");
            } else {
                anim.start("LandPanic");
            }
        } else if (playing("Fall")) {
            anim.start("Land");
        }
        if (ctx->collider->onGround() && ctx->floorCode == "Slide") {
            removeNormalVelocity();
        } else {
            ctx->body->vel = V3{0, 0, 0};
        }
        if (hook) {
            ctx->lookHook(0);
        }
    }

    void exit() override {   // 0x1a8a30
        if (hook) {
            ctx->lookHook(4);
        }
    }

    // 0x43aac0: a panic landing can only be cancelled after its first 50 frames.
    bool cancel() const {
        if (!ctx->anim->isPlaying("LandPanic")) {
            return true;
        }
        return !(ctx->anim->currentFrame() < ctx->params.landPanicCancelFrame);
    }
};

// PullOutSign (the dash start, vtable 0x4cbdd8) and the following Wait (vtable 0x4cbe04).
struct PullOutSignAction : Action {
    void enter() override {   // 0x1fd840
        ctx->body->vel = V3{0, 0, 0};
        ctx->anim->start("PullOutSign");
    }
    void update() override {   // 0x1fd8a8
        ctx->lookHook(4);
        if (ctx->collider->onGround()) {
            removeNormalVelocity();
        }
    }
};

struct DashWaitAction : Action {
    int32_t counter = 0;      // +0x0c
    void enter() override {   // 0x1fd954
        counter = 0;
        ctx->body->vel = V3{0, 0, 0};
        ctx->anim->start("Wait");
    }
    void update() override {   // 0x1fd9c4
        counter = counter + 1;
        ctx->lookHook(4);
        if (ctx->collider->onGround()) {
            removeNormalVelocity();
        }
    }
};

// PullOutStart (vtable 0x4c8d5c; state holder 0x334e7de8): pulling a plant out of the ground.
// The action itself only stands still and plays "PullOutStart" (name at 0x4fe714); the plant's
// reply to the pull message (StageObjects / session) switches the clip to PullOutSuccess,
// PullOutSuccessQuick, PullOutSuccessCarry or PullOutMiss (player receiveMsg 0x1574dc), and the
// state ends with that clip.
struct PullOutStartAction : Action {
    int32_t counter = 0;      // +0x08 ticks in the action (cleared by the start)
    void enter() override {   // 0x1d7d7c (vtable +0x18, the action start)
        ctx->body->vel = V3{0, 0, 0};
        ctx->anim->start("PullOutStart");
        counter = 0;
    }
    void update() override {   // 0x1d7e20
        ctx->lookHook(4);
        if (ctx->collider->onGround()) {
            removeNormalVelocity();
        }
        counter = counter + 1;
    }
    // vtable +0x20 (0x3e57bc, through 0x43a630): the stick may cancel the pull once
    // PullOutCancelableFrame ticks have passed, except during PullOutMiss.
    bool cancelable() const {
        if (ctx->anim->isPlaying("PullOutMiss")) {
            return false;
        }
        return int32_t(ctx->params.pullOutCancelableFrame) < counter;
    }
};

// The wait after a failed pull (vtable 0x4cda20; state holder 0x334e7e04): "PullOutMissWait"
// (name string at 0x4fe7ae, returned by vtable +0x20 0x3ec0a4; read from memory).
struct PullOutMissWaitAction : Action {
    void enter() override {   // 0x218a08
        ctx->body->vel = V3{0, 0, 0};
        ctx->anim->start("PullOutMissWait");
    }
    void update() override {   // 0x218a70
        ctx->lookHook(4);
        if (ctx->collider->onGround()) {
            removeNormalVelocity();
        }
    }
};

// Miss (vtable 0x4ca8d4; state holder 0x334e7cc8): the protagonist is hit. Stops, stands up
// along the ground normal, faces +Z and plays "DieOver" (0x1ed4fc); each update turns him toward
// the look target without its up component (0x1ed62c). The state has no transitions; the course
// handles what follows.
struct MissAction : Action {
    void enter() override {   // 0x1ed4fc
        ctx->body->vel = V3{0, 0, 0};
        ctx->body->up = ctx->body->groundUp;
        ctx->body->front = V3{0, 0, 1.0f};
        ctx->anim->start("DieOver");
    }
    void update() override {   // 0x1ed62c
        if (!ctx->haveMissLookTarget) {
            return;
        }
        V3 d = captain_toad::RecoveredVec::sub(ctx->missLookTarget, ctx->body->pos);
        const V3& up = ctx->body->up;
        d = captain_toad::RecoveredVec::sub(
            d, captain_toad::RecoveredVec::scale(up, captain_toad::RecoveredVec::dot(d, up)));
        if (captain_toad::RecoveredVec::dot(d, d) < 1.0e-5f) {
            return;
        }
        captain_toad::RecoveredVec::normalize(d);
        ctx->body->front = d;
    }
};

// RunSign (vtable 0x4c8d98)
struct RunSignAction : Action {
    bool released = false;   // +0x0c the dash button has been released

    void enter() override {   // 0x1d7ef0
        ctx->body->vel = V3{0, 0, 0};
        ctx->anim->start("RunSign");
        released = false;
    }
    void update() override {   // 0x1d7f68
        ctx->lookHook(4);
        if (ctx->collider->onGround()) {
            removeNormalVelocity();
        }
        if (!released && !ctx->pad->dashHeld) {
            released = true;
        }
    }
    bool done() const {   // 0x43a6a4
        if (!released) {
            return false;
        }
        float frame = ctx->anim->currentFrame();
        float end = ctx->anim->length();
        end = end - 2.0f;
        return frame >= end;
    }
};

// Turn (vtable 0x4c6828): turn on the spot towards the stick within PivotFrame ticks.
struct TurnAction : Action {
    // InvincibleTurn (vtable 0x4ce474) overrides +0x24 (0x40a1c0: PivotFrameInvinciblePickaxe) and
    // +0x28 (0x40a1b4: the clip "Invincible").
    bool invincible = false;
    const char* name = "Turn";   // +0x28 (0x3e45c0; TurnWater 0x3ea348: "TurnWater")
    const char* clip() const { return invincible ? "Invincible" : name; }
    uint32_t pivotFrames() const {
        return invincible ? ctx->params.pivotFrameInvinciblePickaxe : ctx->params.pivotFrame;
    }
    uint32_t counter = 0;   // +0x10
    V3 target;              // +0x14
    bool airPass = false;   // +0x20
    bool hook = true;       // +0x21

    void colliderPass() override {   // 0x1bae2c
        if (airPass) {
            ctx->collider->airTick();
        } else {
            ctx->collider->tick();
        }
    }

    void enter() override {   // 0x1bae54
        using namespace captain_toad::RecoveredVec;
        counter = 0;
        ensure(clip());
        if (hook) {
            ctx->lookHook(0);
        }
        Body& b = *ctx->body;
        b.vel = V3{0, 0, 0};
        target = ctx->pad->move;
        float d = b.groundUp.x * target.x;
        d = d + b.groundUp.y * target.y;
        d = -d - b.groundUp.z * target.z;
        target = addScaled(b.groundUp, target, d);
        float l2 = target.x * target.x;
        l2 = l2 + target.y * target.y;
        l2 = l2 + target.z * target.z;
        if (l2 < fbits(0x358637be)) {
            target = V3{0, 0, 0};
        } else {
            normalize(target);
        }
    }

    void update() override {   // 0x1baff4
        using namespace captain_toad::RecoveredVec;
        Body& b = *ctx->body;
        counter = counter + 1;
        if (ctx->collider->onGround()) {
            removeNormalVelocity();
        }
        float t = float(counter) / float(pivotFrames());
        if (int32_t(bitsf(t)) > int32_t(0x3f800000)) {
            t = 1.0f;
        }
        V3 f = b.front;
        {
            float d = b.groundUp.x * f.x;
            d = d + b.groundUp.y * f.y;
            d = -d - b.groundUp.z * f.z;
            f = addScaled(b.groundUp, f, d);
        }
        const float eps = fbits(0x358637be);
        const float zero = 0.0f;
        {
            float l2 = f.x * f.x;
            l2 = l2 + f.y * f.y;
            l2 = l2 + f.z * f.z;
            if (l2 < eps) {
                return;
            }
        }
        normalize(f);
        V3 m = ctx->pad->move;
        {
            float d = b.groundUp.x * m.x;
            d = d + b.groundUp.y * m.y;
            d = -d - b.groundUp.z * m.z;
            m = addScaled(b.groundUp, m, d);
        }
        {
            float l2 = m.x * m.x;
            l2 = l2 + m.y * m.y;
            l2 = l2 + m.z * m.z;
            if (l2 < eps) {
                m = V3{zero, zero, zero};
                float t2 = target.x * target.x;
                t2 = t2 + target.y * target.y;
                t2 = t2 + target.z * target.z;
                if (t2 == zero) {
                    return;
                }
                m = target;
            } else {
                normalize(m);
                target = m;
            }
        }
        // Rotate the front by the fraction t of the remaining angle, about the ground up.
        float d = f.x * m.x;
        d = d + f.y * m.y;
        d = d + f.z * m.z;
        V3 c = cross(f, m);
        float c2 = c.x * c.x;
        c2 = c2 + c.y * c.y;
        c2 = c2 + c.z * c.z;
        float angle = captain_toad::RecoveredMath::atan2f(std::sqrt(c2), d);
        V3 axis = cross(f, m);
        float s = axis.x * b.groundUp.x;
        s = s + axis.y * b.groundUp.y;
        s = s + axis.z * b.groundUp.z;
        float a = s >= zero ? angle * t : -(angle * t);
        captain_toad::RecoveredMath::Quat q = captain_toad::RecoveredMath::fromAxisAngle(b.groundUp, a);
        captain_toad::RecoveredMath::rotate(b.front, q);
        normalize(b.front);
    }

    void exit() override {   // 0x1bb37c (+0xc object 0x21a7c8: clears two counters)
        if (hook) {
            ctx->lookHook(4);
        }
    }

    bool done() const { return !(pivotFrames() > counter); }   // 0x43a5fc: vtable +0x24 <= counter
};

// ---- Condition queries (the leaf classes of the original) -------------------------------------------

namespace check {

inline bool movingUp(const Body& b) {   // 0x2738f4
    float s = b.up.x * b.vel.x;
    s = s + b.up.y * b.vel.y;
    s = s + b.up.z * b.vel.z;
    return s > 0.0f;
}

inline bool horizontalSpeedSqBelow(const Body& b, float threshold) {   // 0x21ab54
    float d = b.groundUp.x * b.vel.x;
    d = d + b.groundUp.y * b.vel.y;
    d = -d - b.groundUp.z * b.vel.z;
    V3 h = captain_toad::RecoveredVec::addScaled(b.groundUp, b.vel, d);
    float l2 = h.x * h.x;
    l2 = l2 + h.y * h.y;
    l2 = l2 + h.z * h.z;
    return l2 < threshold;
}

inline bool groundMaterial(const Context& c, const char* name, bool requireGround) {   // 0x36b3c0 -> 0x2b5484
    if (requireGround && !c.collider->onGround()) {
        return false;
    }
    static const int order[6] = {0, 1, 2, 3, 5, 4};
    for (int i : order) {
        if (c.collider->lists[i].valid && c.floorCode == name) {
            return true;
        }
    }
    return false;
}

// 0x370b30: the stick points within PivotDegree of the front.
inline bool stickNearFront(const Context& c) {
    using namespace captain_toad::RecoveredVec;
    if (!c.pad->stickActive) {
        return false;
    }
    const Body& b = *c.body;
    V3 m = c.pad->move;
    float d = b.up.x * m.x;
    d = d + b.up.y * m.y;
    d = -d - b.up.z * m.z;
    V3 a = addScaled(b.up, m, d);
    float l2 = a.x * a.x;
    l2 = l2 + a.y * a.y;
    l2 = l2 + a.z * a.z;
    if (l2 < fbits(0x358637be)) {
        return false;
    }
    normalize(a);
    float angle = c.params.pivotDegree * fbits(0x3c8efa35);
    float s = a.x * b.front.x;
    s = s + a.y * b.front.y;
    s = s + a.z * b.front.z;
    return s > captain_toad::RecoveredMath::cosf(angle);
}

// 0x36c9b4: the stick points more than PivotDegree away from the front.
inline bool stickAwayFromFront(const Context& c) {
    using namespace captain_toad::RecoveredVec;
    const Body& b = *c.body;
    V3 m = c.pad->move;
    float d = b.up.x * m.x;
    d = d + b.up.y * m.y;
    d = -d - b.up.z * m.z;
    V3 a = addScaled(b.up, m, d);
    float l2 = a.x * a.x;
    l2 = l2 + a.y * a.y;
    l2 = l2 + a.z * a.z;
    if (l2 < fbits(0x358637be)) {
        return false;
    }
    normalize(a);
    float angle = c.params.pivotDegree * fbits(0x3c8efa35);
    float s = a.x * b.front.x;
    s = s + a.y * b.front.y;
    s = s + a.z * b.front.z;
    return captain_toad::RecoveredMath::cosf(angle) >= s;
}

// 0x36c900: the stick has been spun around quickly (total and per-tick rotation).
inline bool tornadoSpin(const Context& c) {
    float s = c.pad->rotationSumDegrees();
    if (!(s > 0.0f)) {
        s = -s;
    }
    if (s < c.params.tornadoSum) {
        return false;
    }
    float r = c.pad->rotationSpeedDegrees();
    if (!(r > 0.0f)) {
        r = -r;
    }
    return r > c.params.tornadoPerFrame;
}

// 0x1a8030 -> 0x1a807c: the stick was pushed hard in the opposite direction of the front.
inline bool oppositeInput(const Context& c) {
    using namespace captain_toad::RecoveredVec;
    const Body& b = *c.body;
    const float minLength = c.params.oppositeInputMoveVecLength;
    const float degrees = c.params.oppositeAngleDegree;
    V3 m = c.pad->move;
    {
        float l2 = m.x * m.x;
        l2 = l2 + m.y * m.y;
        l2 = l2 + m.z * m.z;
        if (minLength > std::sqrt(l2)) {
            return false;
        }
    }
    float d = b.up.x * m.x;
    d = d + b.up.y * m.y;
    d = -d - b.up.z * m.z;
    V3 a = addScaled(b.up, m, d);
    if (captain_toad::RecoveredPad::normalizeOrZero(a)) {
        return false;
    }
    V3 f = b.front;
    if (captain_toad::RecoveredPad::normalizeOrZero(f)) {
        return false;
    }
    // A stick that was already pointing the same way last tick is not a new opposite input.
    V3 p = c.pad->prevMove;
    float pl2 = p.x * p.x;
    pl2 = pl2 + p.y * p.y;
    pl2 = pl2 + p.z * p.z;
    const float pl = std::sqrt(pl2);
    if (!(minLength > pl) && !std::isnan(pl)) {
        float k = b.up.x * p.x;
        k = k + b.up.y * p.y;
        k = -k - b.up.z * p.z;
        V3 q = addScaled(b.up, p, k);
        if (!captain_toad::RecoveredPad::normalizeOrZero(q)) {
            float s = a.x * q.x;
            s = s + a.y * q.y;
            s = s + a.z * q.z;
            if (int32_t(bitsf(s)) > int32_t(0x248d3000)) {
                return false;
            }
        }
    }
    float angle = degrees * fbits(0x3c8efa35);
    float s = f.x * a.x;
    s = s + f.y * a.y;
    s = s + f.z * a.z;
    return s <= captain_toad::RecoveredMath::cosf(angle);
}

// 0x21a5b8: the (named) clip has ended, or reached `frame`.
inline bool animationEnd(const Context& c, const char* name, int32_t frame) {
    if (name && !c.anim->isPlaying(name)) {
        return false;
    }
    if (c.anim->isEnd()) {
        return true;
    }
    if (frame < 0) {
        return false;
    }
    return !(c.anim->currentFrame() < float(frame));
}

}   // namespace check

// ---- The machine ------------------------------------------------------------------------------------

struct State;

struct Transition {
    Pred* first;          // the condition
    State* second;        // the target state
    uint32_t condition;   // the condition's runtime object address
};

struct State {
    std::string name;
    Action* action = nullptr;   // null: not ported
    std::vector<Transition> transitions;
};

struct Machine {
    Context ctx;
    std::map<std::string, std::unique_ptr<State>> states;
    std::map<uint32_t, std::unique_ptr<Pred>> preds;
    std::vector<std::unique_ptr<Action>> actions;
    State* current = nullptr;
    State* next = nullptr;
    uint32_t step = 0;
    std::string blocked;   // an unported state the original would have entered this tick
    std::set<std::string> blockedEver;

    WaitAction* wait = nullptr;
    IdleAction* idle = nullptr;
    WalkAction* walk = nullptr;
    FallAction* fall = nullptr;
    LandAction* land = nullptr;
    RunSignAction* runSign = nullptr;
    DashWaitAction* dashWait = nullptr;
    TurnAction* turn = nullptr;
    WalkAction* invincibleWalk = nullptr;
    FallAction* invincibleFall = nullptr;
    TurnAction* invincibleTurn = nullptr;
    WaitAction* waitWater = nullptr;   // the water states (RevolverRuinsStage)
    WalkAction* moveWater = nullptr;
    TurnAction* turnWater = nullptr;
    PullOutStartAction* pullOutStart = nullptr;
    PullOutMissWaitAction* pullOutMissWait = nullptr;
    MissAction* miss = nullptr;

    State* state(const std::string& name) {
        auto& s = states[name];
        if (!s) {
            s = std::make_unique<State>();
            s->name = name;
        }
        return s.get();
    }

    // A condition shared by its runtime address: created on first use.
    Pred* pred(uint32_t address, const std::function<std::unique_ptr<Pred>()>& make) {
        auto& p = preds[address];
        if (!p) {
            p = make();
            p->address = address;
        }
        return p.get();
    }

    template <class A, class... Args>
    A* addAction(Args&&... args) {
        auto a = std::make_unique<A>(std::forward<Args>(args)...);
        a->ctx = &ctx;
        A* result = a.get();
        actions.push_back(std::move(a));
        return result;
    }

    // ---- Graph construction helpers (used by PlayerGraph.inc) ----
    Pred* leaf(uint32_t address, Fn query) {
        return pred(address, [&] { return std::unique_ptr<Pred>(new Leaf(std::move(query))); });
    }
    Pred* notOf(uint32_t address, Pred* condition) {
        return pred(address, [&] { return std::unique_ptr<Pred>(new Not(condition)); });
    }
    Pred* allOf(uint32_t address, std::vector<Pred*> conditions) {
        return pred(address, [&] { return std::unique_ptr<Pred>(new AndAll(std::move(conditions))); });
    }
    Pred* anyOf(uint32_t address, std::vector<Pred*> conditions) {
        return pred(address, [&] { return std::unique_ptr<Pred>(new OrAll(std::move(conditions))); });
    }
    Pred* countAtLeast(uint32_t address, Fn query, int32_t times) {
        return pred(address, [&] { return std::unique_ptr<Pred>(new CountAtLeast(std::move(query), times)); });
    }
    Pred* consecutive(uint32_t address, Pred* condition, uint32_t ticks) {
        return pred(address, [&] { return std::unique_ptr<Pred>(new Consecutive(condition, ticks)); });
    }
    void transition(State* from, const char* target, Pred* condition) {
        from->transitions.push_back({condition, state(target), condition->address});
    }

    void build();   // PlayerGraph.inc

    std::vector<std::pair<uint32_t, bool>>* evalTrace = nullptr;   // optional: (condition, result) in order

    // 0x1bace8: test the current state's transitions in order; the first true one wins.
    void evaluate() {
        next = nullptr;
        for (auto& t : current->transitions) {
            const bool result = t.first->check();
            if (evalTrace) {
                evalTrace->push_back({t.condition, result});
            }
            if (result) {
                next = t.second;
                return;
            }
        }
    }

    // 0x2d9c20: change state (old exit, new enter, the new state's conditions reset).
    bool apply() {
        if (!next) {
            return false;
        }
        if (!next->action) {
            blocked = next->name;
            blockedEver.insert(next->name);
            next = nullptr;
            return false;
        }
        current->action->exit();
        current = next;
        next = nullptr;
        current->action->enter();
        for (auto& t : current->transitions) {
            t.first->reset();
        }
        step = 0;
        return true;
    }

    // The skeleton's advance (0x294780) belongs to the player model's movement (0x160a8c ->
    // 0x2d6e90), which runs after the player's whole update: a host that exchanges messages in
    // between (the course's pull replies) defers it and calls the anim's advance itself.
    bool deferAdvance = false;

    // One movement-controller tick after the pad update (0x3b9b40 subset).
    void tick() {
        blocked.clear();
        ctx.collider->dashHeld = ctx.pad->dashHeld;   // collider +0x20 -> pad vtable +0x50 (dash ground probe)
        if (!apply()) {
            evaluate();
            apply();
        }
        current->action->colliderPass();   // 0x1badc8
        // 0x3b9ca4: collider velocity cut, mask 1 | (controller +0x40 ? 4 : 0); +0x40 is 0 in the opening.
        ctx.collider->cutVelocity(1u);
        current->action->update();   // 0x1bade4
        step = step + 1;
        ctx.anim->copyMirror();   // 0x1d8094
        if (!deferAdvance) {
            ctx.anim->advance();
        }
    }
};

}   // namespace captain_toad::RecoveredPlayerActions
