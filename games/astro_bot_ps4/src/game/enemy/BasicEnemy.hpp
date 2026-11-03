// The basic enemies (enemy_standard and kin; MupBasicEnemy, constructor 0x2d9b20, vtable 0x1685908):
// a character body driven by a behaviour tree (game/ai/Behavior.hpp) whose leaves play animation
// clips and set the body's velocity. Findings: .analysis/docs/enemies.md.
//
// Body (MupRootDynamics at +0x2570, game/enemy/BasicEnemy.cpp "Root dynamics"): root position
// +0x2670, rotation +0x2680, velocity +0x26a0, mode +0x25a8 (0x10 held in place, 4 airborne, 2 on
// the ground, 8 thrown), ground distance and the "grounded" test (0x7e9ad0: within
// ThresholdOfGround). The root drives an active rigid body of the physics library (the
// definition's RootCollision body, placed through the BodyJoint's frame): each update the body
// is read back, the behaviours move the root, and the body is given the velocities that take it
// there in one world step, whose contacts then push it (walls, other enemies).
#pragma once

#include "game/LevelDocument.hpp"
#include "game/LevelRuntime.hpp"
#include "game/ai/Behavior.hpp"
#include "game/enemy/EnemyTuning.hpp"
#include "math/Transform.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace beacon::physics {
class PhysicsWorld;
}

namespace beacon::data {
struct PhysicsFile;
}

namespace beacon::game::enemy {

class BasicEnemy;
using Behavior = ai::Behavior<BasicEnemy>;
using ai::Status;

// Body modes (+0x38 of the root dynamics; set by the clip a behaviour plays). 8: thrown (the
// defeated enemy flies by its velocity and spin).
enum BodyMode : int { kModeInitial = 1, kModeGround = 2, kModeAir = 4, kModeThrown = 8, kModeHeld = 0x10 };

// A hit on an enemy (the info passed to virtual +0x210, kept at +0x3ad0..): type (+4; the hero's
// punch 1 or 2, 5 damage by amount, 6 reaction request, 0xd ignored), where it landed (+0x10),
// the push direction (+0x20; punch: horizontal, from the hero to the enemy), damage for type 5
// (+0x50).
struct EnemyHit {
    int type = 1;
    math::Vec3 position, direction;
    int damage = 1;
};

// A hit an enemy's attack lands on the hero (0x2e0130 / 0x2e12a0), for the scene to hand on
// (0x66faf0): type, the attacking body's position, the direction from it towards the hero.
struct EnemyHeroHit {
    int type = 0;
    math::Vec3 position, direction;
};

// What an enemy sees of the world each update.
struct EnemyWorld {
    const LevelRuntime* runtime = nullptr;
    physics::PhysicsWorld* physics = nullptr;   // the bodies and the module's queries
    bool hasHero = false;
    math::Vec3 heroFeet;
    int heroFoot = -1, heroSensor = -1;   // the hero's physics bodies (foot capsule, enemy sensor)
    int heroGround = -1;                  // the body the hero stands on (+0x1b0)
    std::vector<EnemyHeroHit>* heroHits = nullptr;   // the attacks' hits of this update
    float dt = 1.0f / 60.0f;
};

// A clip of the enemy's definition: length at its playback speed and the root-motion joint's
// local translation per frame (joint hash 0x45279a6e; walk moves it 0.0976 m per loop).
struct EnemyClip {
    float length = 0;
    bool loop = false;
    float speed = 1;                  // definition playback speed
    float rate = 60;                  // frames per second of `root`
    std::vector<math::Vec3> root;     // empty: no root motion
    math::Vec3 rootAt(float time) const;   // clip time (seconds of the source clip)
    // Model-space frames of the joints bodies hang on (Bind/BodyJoint for the root dynamics'
    // body, 0x7e83b0; the damage body's joint), per frame, with the root-motion joint held at its
    // base pose.
    struct JointTrack {
        std::vector<math::Vec3> at;
        std::vector<math::Quat> turn;
    };
    std::map<std::uint32_t, JointTrack> joints;   // by joint name hash
    bool jointFrame(std::uint32_t joint, float time, math::Vec3& at, math::Quat& turn) const;
};

// Blend durations between clips (<IAnimation><Transition>: Source, Destination,
// JointsBlendDuration), keyed by (source hash, destination hash).
using EnemyTransitions = std::map<std::pair<std::uint32_t, std::uint32_t>, float>;

class BasicEnemy {
public:
    // `kindId` is the class's id (+0x1cc8): 0x7d2 enemy_standard, ...
    BasicEnemy(const Level& level, std::size_t placement, int kindId, EnemyTuning tuning,
               std::map<std::uint32_t, EnemyClip> clips, EnemyTransitions transitions, std::uint32_t seed);
    ~BasicEnemy();

    void update(const EnemyWorld& world);

    std::size_t placement() const { return placement_; }
    const math::Vec3& position() const { return pos_; }
    const math::Quat& rotation() const { return rot_; }
    const math::Vec3& velocity() const { return vel_; }
    int bodyMode() const { return mode_; }
    bool visible() const { return visible_; }
    // Debug / verification: the top behaviour's label and the clip playing.
    const char* stateName() const;
    std::uint32_t clipHash() const { return clip_; }
    float clipTime() const { return clipTime_; }
    // The clip being blended out and the blend-in weight of the current clip (for drawing).
    std::uint32_t blendFromClip() const { return fromClip_; }
    float blendFromTime() const { return fromTime_; }
    float blendWeight() const { return blendDuration_ > 0 ? std::min(1.0f, blendTime_ / blendDuration_) : 1.0f; }

    // --- for behaviours ---------------------------------------------------------------------
    const EnemyWorld& world() const { return *world_; }
    const EnemyTuning& tuning() const { return tuning_; }
    int kindId() const { return kindId_; }
    float dt() const { return world_->dt; }

    // Plays a clip by name hash (virtual +0x348 -> 0x2df9d0): sets the body mode; false when the
    // definition has no such clip.
    // rootMotion / rootMotionY: the clip's root moves the body horizontally / vertically (the
    // axis flags +0x2620 = (h, v, h) set by 0x2df9d0 from its 4th / 5th arguments).
    bool playClip(std::uint32_t hash, bool loop, int mode, bool rootMotion = false, bool rootMotionY = false);
    bool clipPlaying() const;            // the base layer still plays (0xbb2880)
    bool clipLoops() const { return clipLoop_; }   // the player's loop flag (0xbb8170)
    float clipLength(std::uint32_t hash) const;
    void setClipSpeed(float s) { clipSpeed_ = s; }   // +0x25ec

    void setMode(int m) { mode_ = m; }
    bool grounded() const;               // 0x7e9ad0
    void setVelocity(const math::Vec3& v) {   // virtual +0x330
        vel_ = v;
        velocitySet_ = true;
    }
    // The root, its last pose and the body move at once (teleport; script poses, launches).
    void setPosition(const math::Vec3& p);
    // Virtual +0x70 (0x255c20): the root alone (the body follows through the next apply).
    void moveRoot(const math::Vec3& p) { pos_ = p; }
    // The root dynamics' body (0x7dd0c0 with a RootCollision body): record `record` of `file`,
    // the body's frame in the root (the BodyJoint's model frame, unscaled) and the body's own
    // offset in that frame (bodyShift, bodyTwist: the record's body frame as the joint's base
    // frame sees it); mass 200 and the matching inertia, friction 0.01, fixed and unfiltered
    // until the first mode change.
    void attachBody(physics::PhysicsWorld& world, const data::PhysicsFile& file, std::size_t record,
                    std::uint32_t joint, const math::Vec3& frameOffset, const math::Quat& frameTurn,
                    const math::Vec3& bodyShift, const math::Quat& bodyTwist);
    // The DamageJoint body (0x2daf40 for enemy_standard: keyframe, self 0x1000), hung on `joint`
    // (base frame given) at its own offset in that frame, moved with it every update.
    void attachDamageBody(physics::PhysicsWorld& world, const data::PhysicsFile& file, std::size_t record,
                          std::uint32_t joint, const math::Vec3& frameOffset, const math::Quat& frameTurn,
                          const math::Vec3& bodyShift, const math::Quat& bodyTwist);
    int body() const { return body_; }
    // The DeadCollision body (record `record`; created off: fixed, no filter bits).
    void attachDeadBody(physics::PhysicsWorld& world, const data::PhysicsFile& file, std::size_t record);
    // Virtual +0x380 (0x2e2750): the dead body on (placed at the root, active, filter 0x1000, the
    // enemy's velocity and spin; the other body off) or off; false without a dead body.
    bool switchDeadBody(bool on);
    // ThrownLoop's update (0x2f81c0): the root follows the dead body (the model's transform).
    void followDeadBody();
    void setRotation(const math::Quat& q);
    float gravity() const { return gravity_; }            // +0x25d0
    void setGravity(float g) { gravity_ = g; }
    void setCollision(bool on) { collide_ = on; }          // +0x25dc / 0x7e9db0
    bool collides() const { return collide_; }
    void setVisible(bool v) { visible_ = v; }
    float scale() const { return scale_; }
    float random(float lo, float hi);   // stand-in for the engine's global generator (0xebe680)
    // Verification: cruising goals and waits taken from a trace of the original instead of the
    // generator (each call pops the next one; empty: use the generator).
    std::vector<math::Vec3> scriptedGoals;
    std::vector<float> scriptedWaits;

    // Common enemy triggers (virtual +0x2f0, 0x609180): 0 launched (spawn link + LaunchDelay),
    // 1 chase, 2 end.
    bool triggerFired(int which) const;
    // A hero to go after (Cruising update -> 0x305b40): the chase trigger fired (with
    // ChaseDelayTime) and a hero is there.
    bool chaseTarget() const { return world_->hasHero && triggerFired(1); }
    bool noDiscovery() const { return noDiscovery_; }
    // 0x2dfef0: the attack flag (+0x3aaf); both ways it forgets the heroes this attack has hit.
    void attackHero();   // virtual +0x358
    void setAttacking(bool on) {
        attacking_ = on;
        heroHit_ = false;
    }
    bool attacking() const { return attacking_; }
    // Placement attributes.
    int launchType() const { return launchType_; }
    bool invisibleStandby() const { return invisibleStandby_; }          // Stand 0, Parachute 1, Jumpup 2, Panic 3
    float launchJumpHeight() const { return launchJumpHeight_; }
    float launchJumpGravity() const { return launchJumpGravity_; }
    bool launchTarget(math::Vec3& at, math::Quat& rot) const;   // LaunchJumpEnd locator
    bool hitPending() const { return hitPending_; }           // +0x3aac
    int reactionRequest() const { return reaction_; }         // +0x3d10
    // A hit arrives (virtual +0x210, 0x2e20a0); ignored while one is pending.
    void hit(const EnemyHit& h);
    const EnemyHit& lastHit() const { return hit_; }
    void clearHit();                                          // 0x2e2300
    int hitpoints() const { return hp_; }                     // +0x3aa8 (virtual +0x298)
    void setHitpoints(int hp) { hp_ = hp; }
    void setAngularVelocity(const math::Vec3& w) {   // virtual +0x338
        angular_ = w;
        angularSet_ = true;
    }
    const math::Vec3& angularVelocity() const { return angular_; }
    // Defeated: hidden and taken out of the level (0x2e2a50: model off, death model effect,
    // +0x3aae, +0x30). deathEffect() stays true from then on so the view can place the effect.
    void remove();
    bool removed() const { return removed_; }
    // Sensors (the root dynamics' m_sensors, set up by virtual +0x3a8 = 0x2dba50): four feelers
    // around the feet whose hits are condensed into a sensor state each update (0x2e2c60):
    // 0 none, 1 another enemy ahead, 2 an obstacle ahead, 3 airborne over ground ahead, 4 no
    // ground ahead. `rootHeight` is the RootCollision body's height in the root (its body frame,
    // unscaled). Without a physics world the state stays 0.
    void attachSensors(physics::PhysicsWorld& world, float rootHeight);
    int sensorState() const { return sensorState_; }   // +0x3bfc
    // The ground sweep (sensor D): its start in the root's frame, length and radius (accessors
    // 0x7e9ef0 / 0x7ea020 / 0x7ea0b0), and its enabled flag (0x7e9e60 / 0x7e9dc0). The path
    // planner borrows its geometry while it is off.
    bool groundSweep(math::Vec3& at, float& length, float& radius) const;
    bool groundSweepOn() const { return sensors_.size() > 3 && sensors_[3].enabled; }
    void setGroundSweep(bool on) {
        if (sensors_.size() > 3) {
            sensors_[3].enabled = on;
        }
    }
    // 0x2e2f40: the share of the root's last intended horizontal move the world step took back
    // (1 - m^ . (m + push) / |m|; 0 when the root did not move).
    float stressSample() const;
    // The running brain's stress (+0x108 of the brain), kept for traces.
    float brainStress() const { return brainStress_; }
    void setBrainStress(float s) { brainStress_ = s; }
    void setSensors(bool on);                          // virtual +0x368 (0x2e2dc0), +0x3ab4
    void setHome(const math::Vec3& p, const math::Quat& q) { homePos_ = p; homeRot_ = q; }
    const math::Vec3& home() const { return homePos_; }
    void setActivated() { activated_ = true; }

private:
    void updateTriggers(float dt);   // 0x60a520
    // Root dynamics: before the behaviours (0x7dfe50: read back, achieved velocity, ground probe)
    // and after them (0x7e5c60: root motion, air flight, the body's move-to velocities 0x7e71b0).
    void readBody();
    void probeGround();
    void applyBody(float dt, const math::Vec3& rootDelta);
    void writeBody(float dt, const math::Vec3& from, const math::Quat& fromTurn);
    void bodyPose(const math::Vec3& root, const math::Quat& rot, math::Vec3& at, math::Quat& turn) const;
    void updateBodyFrame();   // 0x7e83b0 (virtual +0x50): the frame from the animated joint
    void sampleJoint(std::uint32_t joint, math::Vec3& at, math::Quat& turn) const;
    void moveDamageBody();
    bool traced() const;
    void readSensors();       // 0x7e0020 (start of the step)
    void classifySensors();   // 0x2e2c60
    void placeSensors();      // 0x7e7bb0 (end of the apply)
    std::uint32_t sensorBits(int index, std::uint32_t mask) const;   // 0x7e9bd0 (-1: the body's contacts)

    std::size_t placement_;
    int kindId_;
    EnemyTuning tuning_;
    std::map<std::uint32_t, EnemyClip> clips_;
    const EnemyWorld* world_ = nullptr;

    // Placement attributes.
    LevelLink spawnLink_, chaseLink_, endLink_, launchEnd_;
    int launchType_ = 0;
    float launchJumpHeight_ = 0.1f, launchJumpGravity_ = -9.8f, launchDelay_ = 0, chaseDelay_ = 0;
    bool invisibleStandby_ = false, noDiscovery_ = false, attacking_ = false;
    math::Vec3 speedScale_{1, 1, 1};   // +0x2610: SpeedScale, taken when chase starts

    // Common trigger state (+0x24a4.. ).
    bool spawned_ = false, launched_ = false, chaseOn_ = false;
    float chaseTime_ = 0;
    bool inDefaultSensor(const char* group) const;
    float sinceSpawn_ = -1;
    bool activated_ = false;

    // Body.
    math::Vec3 pos_, vel_, homePos_;
    math::Quat rot_, homeRot_;
    int mode_ = kModeInitial;
    float gravity_ = 3.4028235e38f;   // unset: the physics world's gravity
    float groundDistance_ = -1;       // +0x4c, < 0 = no ground below
    float threshold_ = 0.01f;         // ThresholdOfGround (+0x58)
    float radius_ = 0.05f;            // Bind/RootRadius (binder default 0.05)
    bool collide_ = false;            // +0x66: the ground probe runs
    int prevMode_ = kModeInitial;     // +0x3c
    math::Vec3 lastPos_;              // +0x210 / +0x220: the root after the last read-back
    math::Vec3 applyMove_;            // root dynamics +0x1b0: the root's move in the last apply
    math::Vec3 applyRoot_;            // +0x160: the root the last apply left
    math::Vec3 push_;                 // +0x1d0: read-back root - +0x160 (the world step's push)
    float brainStress_ = 0;
    math::Quat lastRot_;
    int body_ = -1;                   // +0x2a0
    int deadBody_ = -1;               // +0x3b40
    int damageBody_ = -1;             // +0x3b48 (also the attack's body +0x3b38)
    bool heroHit_ = false;            // +0x3cf8: the hero was hit by this attack
    bool blockedPunch_ = false;       // +0x3b30: the hero punched the guard (spiky kinds)
    std::uint32_t bodyJoint_ = 0, damageJoint_ = 0;
    math::Vec3 damageOffset_;         // the damage joint in the base pose
    math::Quat damageTurn_;
    math::Vec3 damageShift_;          // the damage body in the joint's frame
    math::Quat damageTwist_;
    math::Vec3 deadShift_;            // +0x3b70: the dead body in the root's frame (unscaled)
    math::Quat deadTwist_;
    bool deadSynced_ = false;         // the model's copy of the dead body's pose (one update old)
    math::Vec3 deadModelAt_;
    math::Quat deadModelTurn_;
    physics::PhysicsWorld* bodyWorld_ = nullptr;
    math::Vec3 frameOffset_;          // +0x2b0..: the body's frame in the root (unscaled)
    math::Quat frameTurn_;
    math::Vec3 baseOffset_;           // the BodyJoint in the base pose (no clip)
    math::Quat baseTurn_;
    math::Vec3 bodyShift_;            // +0xc0: the body's own offset in that frame
    math::Quat bodyTwist_;
    math::Vec3 carry_;                // +0x1f0: the ground body's motion this update
    bool flatGround_ = false;         // +0x500: the last walkable ground point
    math::Vec3 flatPoint_;
    int flatBody_ = -1;               // +0x508: its body; point and normal in its frame (+0x530 / +0x540)
    math::Vec3 flatLocal_, flatNormalLocal_;
    float stepRadius_ = 0.1f;         // Bind/StepRadius (+0x90)
    float stepSlope_ = 0;             // Bind/StepSlopeAngle (+0x94, radians)
    float probeRadius_ = 0.025f;      // GeometrySensorRadius (+0x48)
    bool visible_ = true;

    // Sensors (entries of 0xa0 at root dynamics +0x338): a trigger capsule from `at` along `axis`
    // (whose contacts of the last world step count) or a sphere swept along it at the start of the
    // step; in the root's frame turned by its yaw; hit bits = the self bits of what it met.
    struct Sensor {
        bool sweep = false;
        math::Vec3 at, axis;
        float length = 0, radius = 0.01f;
        std::uint32_t target = 0;
        int modes = -1;              // body modes it runs in (+0x88)
        bool enabled = true;         // +0x80
        bool hit = false;            // +0x82
        std::uint32_t bits = 0;      // +0x84
        int body = -1;
        bool cylinder = false;       // type 1 (0xcaa220), else a capsule (type 0) or the sweep
    };
    std::vector<Sensor> sensors_;    // A wall ahead, B ground ahead, C higher wall, D ground sweep,
                                     // then the body's own ground cylinder (index +0x350)
    bool sensorsOn_ = true;          // +0x3ab4
    int sensorState_ = 0;            // +0x3bfc
    std::uint32_t contactBits_ = 0;  // root dynamics +0x440: the body's contacts' bits

    // Animation (base layer) and the clip it blends from.
    EnemyTransitions transitions_;
    std::uint32_t clip_ = 0, fromClip_ = 0;
    float clipTime_ = 0, clipSpeed_ = 1, fromTime_ = 0;
    float blendTime_ = 0, blendDuration_ = 0;
    bool clipLoop_ = false, fromLoop_ = false;
    bool rootMotion_ = false, fromRootMotion_ = false;
    bool rootMotionY_ = false, fromRootMotionY_ = false;
    bool velocitySet_ = false;   // a behaviour commanded the velocity this update (+0x25de)
    bool angularSet_ = false;    // ... the angular velocity (+0x25df)
    float scale_ = 1;
    std::uint32_t rng_ = 1;
    std::uint32_t traceId_ = 0;       // the UUID's hash (research traces)
    void advanceClips(float dt, math::Vec3& rootDelta);

    bool hitPending_ = false, wasHit_ = false;   // +0x3aac, +0x3aad
    int reaction_ = 0;
    EnemyHit hit_;
    int hp_ = 1;
    math::Vec3 angular_;
    bool removed_ = false;

    std::unique_ptr<Behavior> root_;
};

}   // namespace beacon::game::enemy
