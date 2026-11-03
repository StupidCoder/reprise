// The robot hero: state machine and movement, re-implemented from the rules recovered from the
// game's code (see .analysis/docs/hero.md) and checked against traces of the original. Runs at
// the game's fixed 1/60 s step; the caller feeds input already turned into a world-space stick
// (setHeroStick, after game/PadStick for pad bytes).
//
// Implemented: Idle, Walk (with coyote time), Jump (variable height), Fall, Land, Hover; states
// ask for the next state and switch after their update, as the game's state machine does.
// Collision: a thin ground probe that puts the feet on the floor, a foot body for ceilings and
// a wall sphere (CollisionForWall*) for the horizontal collide-and-slide, against the static level.
#pragma once

#include "game/HeroClips.hpp"
#include "game/HeroTuning.hpp"
#include "math/Transform.hpp"
#include "physics/PhysicsWorld.hpp"

#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace beacon::game {

struct HeroInput {
    float stickX = 0, stickZ = 0;   // world-space direction scaled by stick deflection (|s| <= 1)
    bool jumpPressed = false;       // pressed this step
    bool jumpHeld = false;
    bool punchPressed = false;      // square (hero +0x11f)
    bool punchHeld = false;         // (+0x120)
    // A lost bot touches the hero's enemy sensor (0x678330 over the sensor's contacts): a punch
    // started now is a kick.
    bool kickable = false;
};

// The kick's reach this update (Kick 0x4dc4e0, from 0.1 s until it meets something): a sphere of
// `radius` swept from `from` to `to`, hitting along `direction` (the hero's facing).
struct HeroKickSweep {
    math::Vec3 from, to, direction;
    float radius = 0;
};

// One step of a punching hand (0x66a730 -> 0x674c80): a sphere of `radius` swept from `from` to
// `to` this update. `window` numbers the punch, so a target is hit once per punch.
struct HeroAttackSweep {
    math::Vec3 from, to;
    float radius = 0;
    int window = 0;
};

// The hero's stick (0x667b20): pad axes below MinStickInput are dropped one by one, the rest is
// laid onto the ground along the camera's right and forward directions (unit, horizontal), and
// a result longer than 1 is scaled back to length 1.
void setHeroStick(HeroInput& in, float padX, float padY, const math::Vec3& right, const math::Vec3& forward,
                  float minStickInput);


enum class HeroState : std::uint8_t {
    Idle, Walk, Jump, Fall, Land, Hover, Teeter, Punch, Slide, Kick,
    Damage,        // 11: knocked down (a plain hit; a deadly one ends in the death routine)
    LightDamage,   // 12: pushed back by its clip's root motion, then on its feet
    DeathOnSpike,  // 59: spikes (attack 0x53)
    Respawn        // the second machine's DefaultRespawn (0x5c); the main machine has no state
};

// A hit on the hero (the record copied to hero +0x198 by 0x66faf0): type (+0x4), the source's
// position (+0x10) and the direction from the source towards the hero (+0x20).
struct HeroHit {
    int type = 0;
    math::Vec3 position, direction;
};

// A teetering wall (class teetering_wall, 0x9537c0): an invisible thin box at a ledge, half extents
// scale * (0.5, 0.25, 0.05) around the placement in its rotation; only the hero's foot body notices
// it (0x6738f0). axisZ is the wall's normal (towards the safe side).
struct TeeterWall {
    math::Vec3 centre, axisX, axisY, axisZ, half;
};
// The level's teetering walls at their placed pose.
std::vector<TeeterWall> collectTeeterWalls(const struct Level& level);

const char* heroStateName(HeroState s);

// The hero's bodies in the physics world (0x667200), placed after each update (0x66d5f0):
// - the foot body (+0x150): an active capsule standing upright, radius CollisionRadius, half length
//   0.5 CollisionHeight, CollisionYOffset above the feet, mass 100; self bits 1 for the robot
//   (0x12241d4: 2 / 4 / 8 for the other heroes), target 0x7fffffff. Each update it is given the
//   velocities that take it to the feet in one world step, upright, without spin.
// - the enemy sensor (+0x158): a trigger cylinder, radius EnemySensorCollisionRadius, half length
//   0.5 EnemySensorCollisionHeight, half the model scale above the feet; self 0x800000, target
//   0x10041000; set straight to the feet.
// Other bodies meet them (enemy sensors see the hero as bit 1); the hero's own movement does not
// read them back yet (0x668240).
class HeroBodies {
public:
    void attach(physics::PhysicsWorld& world, const HeroTuning& t, const math::Vec3& feet, std::uint32_t self = 1);
    void place(physics::PhysicsWorld& world, const math::Vec3& feet);
    int foot() const { return foot_; }
    int sensor() const { return sensor_; }

private:
    int foot_ = -1, sensor_ = -1;
};

class Hero {
public:
    static constexpr float kStep = 1.0f / 60.0f;   // fixed update (hypothesis: the game's rate)

    explicit Hero(const HeroTuning& tuning, const HeroClipLengths& clips = {}) : t_(tuning), clips_(clips) {}

    void spawn(const math::Vec3& feet, float yaw, bool standing = false);   // standing: Idle on the ground
    void update(const HeroInput& input, const physics::PhysicsWorld& world);
    void setTeeterWalls(const std::vector<TeeterWall>* walls) { teeterWalls_ = walls; }

    const math::Vec3& position() const { return pos_; }   // feet
    const math::Vec3& velocity() const { return vel_; }
    float yaw() const { return yaw_; }      // radians, 0 = facing +Z
    float tilt() const { return tilt_; }    // radians, lean into turns
    HeroState state() const { return state_; }
    float stateTime() const { return stateTime_; }
    bool grounded() const { return grounded_; }
    int supportBody() const { return grounded_ ? support_ : -1; }   // moving collision body stood on
    int groundBody() const { return grounded_ ? groundBody_ : -1; } // any body stood on (+0x1b0)
    float horizontalSpeed() const;
    const HeroTuning& tuning() const { return t_; }
    // The punch clip playing (Punch state), and the hand sweeps of this update.
    const char* punchClip() const { return clips_.punch[punchClip_].name; }
    // The clip the Slide state plays (waterslide / slide_belly, then a landing clip) and since when.
    const char* slideClip() const { return slideClip_; }
    float slideClipTime() const { return slideClipTime_; }
    const std::vector<HeroAttackSweep>& attackSweeps() const { return sweeps_; }
    // The kick's sweep of this update, if any; kickLanded() tells it that the sweep met something
    // (+0x44: no more sweeps this kick).
    const std::optional<HeroKickSweep>& kickSweep() const { return kickSweep_; }
    void kickLanded() { kickLanded_ = true; }
    // A jump board under the hero (0x6cd890 -> 0x673650 -> 0x687c90, vertical): the next update
    // jumps at `speed` straight up, playing jump_bumper_01 or, with `funny`, one of
    // jump_bumper_02..04 (never the same twice).
    void boardLaunch(float speed, bool funny);
    // The clip the Jump state plays (jump_01_up, or a board jump's).
    const char* jumpClip() const { return jumpClip_; }
    // A hit (0x66faf0): types 0x14 / 0x37 / 0x47 / 0x53 -> DeathOnSpike, 0x12 / 0x2e ->
    // LightDamage, any other type -> Damage (water deaths not ported); refused while invincible
    // (+0x316) or not damageable (+0x108, LightDamage also +0x109). Forced: the next update ends
    // in the hit's state whatever the current state asks for (0x6a7370 with force).
    bool takeHit(const HeroHit& hit);
    // The death routine (0x671f80) ran: the hero is gone until the level brings it back.
    bool died() const { return died_; }
    // DefaultRespawn (0x5c, enter 0x6a1170, update 0x6a1260): hidden, without collision, the hero
    // waits while the VR player warps (`setVrBusy`), then stands at `at` (the respawn point plus
    // FlySpawnSomerSaultHeight x scale) facing -z playing "respawn"; RespawnEffectTime later
    // respawn_somersault_out, after it respawn_somersault_out_loop with collision and
    // InvincibleTimeAfterRespawn, falling; on the ground Land.
    void startRespawn(const math::Vec3& at, float effectTime, float invincibleTime);
    void setVrBusy(bool busy) { vrBusy_ = busy; }
    // The death routine for a fall (0x671f80 with 1: "fall"), hidden.
    void fallDeath() {
        died_ = true;
        hidden_ = true;
    }
    int respawnPhase() const { return state_ == HeroState::Respawn ? respawnPhase_ : -1; }
    bool hidden() const { return hidden_; }        // the model is switched off (0x6960f0)
    bool invincible() const { return invincible_; }
    bool collisionOff() const { return collisionOff_; }   // the foot body meets nothing (spike death)
    // The clip the damage states play and its time.
    const char* actionClip() const { return actionClip_; }
    float actionClipTime() const { return actionTime_; }
    int punchNumber() const { return windowCount_; }   // counts the punches started
    int hoverNumber() const { return hoverCount_; }    // counts the hovers started (one attack each)
    // Research aid: one collide-and-slide of `delta` from `feet` (0x66dd40), the end position.
    math::Vec3 collideAndSlide(const physics::PhysicsWorld& world, const math::Vec3& feet, const math::Vec3& delta);

private:
    void attackController();        // the state machine's post-update hook (0x684950)
    void enterPunch(const HeroInput& in);
    void updateKick(const HeroInput& in);
    void updateAttackWindow();      // 0x66a730
    math::Vec3 handPosition(bool right) const;
    void changeState(HeroState next);
    void groundControl(const HeroInput& in);
    void airControl(const HeroInput& in);
    void request(HeroState next);
    void bodyStep(const physics::PhysicsWorld& world);
    void slide(const physics::PhysicsWorld& world, const math::Vec3& delta);
    void moveVertical(const physics::PhysicsWorld& world, float dy, bool extendedReach);
    void enterSlide();
    void updateSlide(const HeroInput& in);
    void playSlideClip(const char* name, float length);
    float stickAmount(const HeroInput& in) const;
    void playAction(const char* name);   // 0x673630: a damage clip from its start
    bool actionDone() const;             // 0x694fd0: the clip has played
    math::Vec3 rootMotion() const;       // 0x695020: this update's root travel in the world
    void enterDamage();
    void enterLightDamage();
    void updateDamage(const HeroInput& in);
    void updateLightDamage(const HeroInput& in);
    void updateDeathOnSpike();
    void exitState(HeroState next);
    void setInvincible(float seconds);   // 0x676b10
    void die();                          // 0x671f80

    HeroTuning t_;
    HeroClipLengths clips_;
    math::Vec3 pos_, vel_;
    float yaw_ = 0, tilt_ = 0;
    HeroState state_ = HeroState::Idle;
    HeroState requested_ = HeroState::Idle;   // asked for during this update
    bool hasRequest_ = false;
    bool runningJump_ = false;   // the current jump started above 0.5 m/s
    int support_ = -1;           // moving physics body the hero stands on (-1: none or static)
    int groundState_ = 0;        // +0x14c: 0 airborne, 1 grounded, 2 stopped at an edge this update, 3 on a slide
    bool onSlope_ = false;       // the last ground probe met a slide surface steep enough (+0x14c = 3)
    math::Vec3 slopeNormal_;     // +0x320: that surface's normal
    math::Vec3 slopeDir_;        // +0x330: downhill along it (unit)
    math::Vec3 teeterDir_;       // +0x360: towards the edge the hero stopped at
    math::Vec3 stickDir_;        // this update's stick direction (unit), when there is one
    const std::vector<TeeterWall>* teeterWalls_ = nullptr;
    bool teeterWallStop(const math::Vec3& start);
    bool dropEdgeStop(const physics::PhysicsWorld& world, const math::Vec3& start);
    bool sampleHitsTeeterWall(const math::Vec3& sample) const;
    float stateTime_ = 0;
    bool grounded_ = false;
    float airTime_ = 0;      // time since last ground contact
    bool jumpHolding_ = false;
    int hoverCount_ = 0;
    int groundBody_ = -1;
    bool boardRequest_ = false, boardJump_ = false, boardFunny_ = false;   // +0x82 / +0x84; Jump +0x51
    float boardSpeed_ = 0;                                                 // +0x90
    int bumperClip_ = 0;                                                   // +0x1c0
    std::uint32_t bumperRandom_ = 0x1571;
    const char* jumpClip_ = "jump_01_up";
    float jumpV0_ = 0;       // launch speed of the current jump
    bool hoverAvailable_ = false;   // one hover per airborne phase (+0x81)
    bool hoverRising_ = false;

    // Attack controller (the state machine object, 0x66dd30): time since the last punch (+0xb4),
    // the hand of the last punch (+0xb0, 0 left / 1 right), a punch kept for later (+0xbd) and
    // its copy for the jump cancel (+0xbe); a punch asked for by the hook, applied next update.
    float sincePunch_ = 0;
    int punchHand_ = 0;
    bool punchBuffered_ = false, punchBufferedAtStart_ = false;
    bool punchRequest_ = false;
    bool punchPressed_ = false;
    // Punch state (0x169b5d0): the clip, started airborne (+0x48), at an edge (+0x49).
    int punchClip_ = 0;
    bool punchFromAir_ = false, punchAtEdge_ = false;
    // Attack window (0x674a50 / 0x66a730): open flag +0x249, time +0x24c, start +0x290, end
    // +0x294, hands +0x24a/+0x24b, last hand positions +0x260 (right) / +0x250 (left).
    bool windowOpen_ = false, windowRight_ = false, windowLeft_ = false;
    float windowTime_ = 0, windowStart_ = 0, windowEnd_ = 0;
    math::Vec3 lastRight_, lastLeft_;
    int windowCount_ = 0;
    std::vector<HeroAttackSweep> sweeps_;
    // Kick (state 0x33, vtable 0x169c250): asked for instead of a punch when a lost bot touched
    // the sensor; whether its sweep met something (+0x44).
    bool kickable_ = false, kickRequest_ = false, kickLanded_ = false;
    std::optional<HeroKickSweep> kickSweep_;
    // Slide state (0x169be90): its own velocity (+0x50), backwards (+0x44: entered facing
    // uphill, belly slide), time off the slope (+0x60); the clip it plays.
    math::Vec3 slideVel_;
    bool slideBackwards_ = false;
    float slideOffTime_ = 0;
    const char* slideClip_ = "";
    float slideClipLength_ = 0, slideClipTime_ = 0;
    // Damage: the hit's state, asked for with force (applied by the next update), and its record.
    bool hitRequest_ = false;
    HeroState hitState_ = HeroState::Idle;
    HeroHit hit_;
    bool damageable_ = true;        // machine +0x108: off from a knock-down until it has passed
    bool lightDamageable_ = true;   // machine +0x109: off during LightDamage until 0.3 s grounded
    bool invincible_ = false;       // hero +0x316 (time +0x318 of +0x31c): hits are ignored
    float invincibleTime_ = 0, invincibleFor_ = 0;
    // The damage clip (+0x90 player): name, time, its root path.
    const char* actionClip_ = "";
    float actionTime_ = 0;
    const HeroMotionClip* action_ = nullptr;
    // Damage (11, vtable 0x169b620): phase +0x44, clips in / loop / out (+0x48 / +0x4c / +0x50),
    // time since the out clip started (+0x54), dies (+0x58), weight (+0x5c: light / middle /
    // heavy down time), from spikes on a slide (+0x60).
    int damagePhase_ = 0;
    const char* damageLoop_ = "";
    const char* damageOut_ = "";
    float damageOutTime_ = 0;
    bool damageDies_ = false;
    int damageWeight_ = 0;
    // DeathOnSpike (59): phase +0x44, wait +0x48.
    int spikePhase_ = 0;
    float spikeWait_ = 0;
    bool collisionOff_ = false;     // 0x66fa60 mode 2: the foot body meets nothing
    bool hidden_ = false, died_ = false;
    // Respawn: phase +0x44 (0 waiting for the VR player, 1 effect time, 2 somersault, 3 falling),
    // timer +0x48, the target (+0x90 of the second machine).
    int respawnPhase_ = 0;
    float respawnTimer_ = 0, respawnEffectTime_ = 0.8f, respawnInvincible_ = 1;
    math::Vec3 respawnAt_;
    bool vrBusy_ = false;
};

}   // namespace game
