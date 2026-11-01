// =============================================================================================
// StageRadish.hpp - the turnip a plant gives (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: pulling a turnip plant, the protagonist heaves a big turnip out of the
// ground and lifts it over his head. He can walk around with it; pressing an action button throws
// it forward in an arc. It bursts where it lands or when it hits a wall head-on.
//
// Radish (pooled actor; vtable 0x4cf830, init 0x3bb2ec, receiveMsg 0x3ba810, archive Radish):
// the item generator's branch カブ[引っこ抜き] (0x3e8594) takes it from the pool (0x26061c), puts it
// at the item position and starts it (0x260434: appear, action "PullOut", nerve PullOut).
// Nerves (static objects 0x4f5fa4.., execs via the vtables 0x47ba30..):
//   PullOut 0x40c224      runs its launch state (PullOut parameters) until it has landed -> Wait.
//   Wait 0x40bff0         "Wait" on the ground, still; can be picked up again.
//   HeldAfterPull 0x40c2c8 / Held 0x25ddcc: "Hold" (after "PullOut" has ended when it comes from
//                         the pull; "PgHold" when it is picked up from the ground, +0xc0). The hold
//                         component (0x266ca4, nerve 0x1d43b0) places it at the holder each tick.
//   Thrown 0x3bb870       the launch state with the Attack parameters; it bursts (0x266abc ->
//                         Break) when the launch state has landed or on a head-on wall contact
//                         (wall normal against the flight direction by more than 110 degrees,
//                         literal 0x3bba98).
//   Break 0x40c118        hit reaction 破裂 (effect Break, sound Break), the coin item when it
//                         carries one (+0xb8, IsAppearCoinFromRadish; not set in this course),
//                         then the turnip is gone.
// Messages (receiveMsg 0x3ba810): the hold request (tag 0x4fde00, sent by the player's grab
// 0x2da15c via 0x31750c) is accepted in PullOut and Wait; the throw (0x31757c, from the player's
// release 0x2db54c) is handled by the hold component's release (0x2670c4): the turnip faces the
// holder's front and starts the Attack launch, its forward speed raised by 0.55 x the holder's
// horizontal speed (0x2ddeac).
//
// The launch state 0x1d4a78 (parameter blocks built by the static constructor 0x43c098):
//   step 0: velocity = front * forward - gravity direction * up;
//   every step: velocity += gravity direction * gravityScale (0x2fa2bc), velocity *= damping
//   (0x2cdc48), a wall contact removes the velocity into the wall scaled by wallReflect
//   (0x2f9a20, "HitWall"); done once past `limit` steps and on the ground.
//   PullOut (0x5016d0): up 15, forward 0, gravity 1, damping 1, limit 0.
//   Attack (0x501738): up -2, forward 25, gravity 0.8, damping 0.99, limit 1, thrower 0.55,
//   wall reflect 0.
// Collision: the generic actor collider (ActorCollider.hpp; InitCollider radius 60 at 60 up). The
// launch state reads the previous movement's contacts; collision stays off until the launch's
// limit step (on with a collider reset, 0x2dd9bc / 0x2f9fec); a held turnip moves without it.
// attackSensor 0x3bac14 (thrownContacts, explode, pushesPlayer): homing toward enemies, the
// attack on what the flight touches, the burst's explosion, the push while lying.
// Not in this course: the aimed throw (receiveMsg 0x2671e0 takes a target from the message
// type 0x4fe1ac, sent only by BossBird's attackSensor 0x3c7008 and the class of 0x187e48; the
// release then flies the throw component's path, nerve 0x40c284). Not ported: water and death
// areas in flight (0x3bb870), breaking while held when hit by something faster than 15
// (receiveMsg 0x3ba810), the messages that move a waiting turnip to nerve 0x40c04c.
// =============================================================================================
#pragma once

#include "game/ActorCollider.hpp"
#include "game/StageObjects.hpp"

#include <cmath>

namespace captain_toad::RecoveredStageRadish {

using namespace captain_toad::RecoveredStage;

// A launch parameter block (0x25958c layout, copied by 0x2ddeac).
struct LaunchParams {
    float up, forward;        // vector y, z (+0x40, +0x44)
    float gravityScale;       // +0x54
    float damping;            // +0x5c
    int limit;                // +0x64
    float throwerSpeedRate;   // +0x6c
    float wallReflect;        // +0x74
};
inline constexpr LaunchParams kPullOutLaunch{15.0f, 0.0f, 1.0f, 1.0f, 0, 0.0f, 1.0f};     // 0x5016d0
inline constexpr LaunchParams kAttackLaunch{-2.0f, 25.0f, 0.8f, 0.99f, 1, 0.55f, 0.0f};   // 0x501738

struct Radish : Behaviour {
    // Inserted and Vanish: the door key's own nerves (StageDoorKey.hpp).
    enum class Nerve {
        Hidden,
        PullOut,
        PopUp,
        Wait,
        HeldAfterPull,
        Held,
        Thrown,
        Break,
        Inserted,
        Vanish
    } nerve = Nerve::Hidden;
    int step = 0;
    V3 pos, vel;
    V3 front{0, 0, 1};
    // The launch state.
    LaunchParams launch = kPullOutLaunch;
    float forward = 0;
    int launchStep = 0;
    bool launchEnded = false;   // the launch state has landed (its component ended)
    bool collisionOn = false;   // (+0x78)+10 clear: the movement goes through the collider
    bool fromGround = false;    // +0xc0: picked up from the ground (PgHold)
    bool hasCoin = false;       // +0xb8

    static constexpr float kColliderRadius = 60, kColliderUp = 60;   // InitCollider
    static constexpr V3 kUp{0, 1, 0};
    captain_toad::RecoveredActorCollider::ActorCollider collider{kColliderRadius, kColliderUp};

    explicit Radish(Object& o) {
        o.visible = false;
        o.collision = false;
    }
    bool held() const { return nerve == Nerve::Held || nerve == Nerve::HeldAfterPull; }
    // attackSensor 0x3bac14, in Wait: the Body sensor pushes the player out (0x2f401c).
    bool pushesPlayer(const Object& o) const override { return o.alive && nerve == Nerve::Wait; }
    int homingTarget = -1;                          // +0xbc: the enemy the flight bends toward
    int holdStep = 0;                               // the hold component's step
    captain_toad::RecoveredVec::M34 holdFrom;       // component +0x2c: the pose at the grab
    static constexpr int kHoldEaseSteps = 10;       // 0x5017a0 +0x28
    static constexpr float kHoldStartRate = 0.2f;   // 0x5017a0 +0x2c
    virtual bool grabbable() const { return nerve == Nerve::PullOut || nerve == Nerve::Wait; }

    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    void startLaunch(const LaunchParams& p, float extraForward) {
        launch = p;
        forward = p.forward + extraForward;
        launchStep = 0;
        launchEnded = false;
        collisionOn = false;   // switched on at the launch's limit step (0x1d4a78)
    }
    void place(Object& o) const {
        V3 side = captain_toad::RecoveredVec::cross(V3{0, 1, 0}, front);
        captain_toad::RecoveredVec::normalize(side);
        const float m[12] = {side.x, 0, front.x, pos.x, side.y, 1, front.y, pos.y, side.z, 0, front.z, pos.z};
        for (int k = 0; k < 12; k++) {
            o.world.m[k] = m[k];
        }
    }
    V3 sensorCentre(const SensorDecl& s) const { return captain_toad::RecoveredVec::add(pos, V3{0, s.y, 0}); }

    long startTick = -1;   // the start's tick: its nerve runs from the next keeper update
    // 0x260434 (from the plant's item callback): appear at the item position.
    void appear(World& w, Object& o, const V3& at, const V3& facing) {
        startTick = w.tickCount;
        pos = at;
        front = facing;
        vel = {0, 0, 0};
        o.visible = true;
        o.alive = true;
        fromGround = false;
        o.startAction("PullOut");
        startLaunch(kPullOutLaunch, 0.0f);
        setNerve(Nerve::PullOut);
        place(o);
    }
    // 0x2605a0 (the generator's branch カブ, from a plant uprooted by the pickaxe): appear at the
    // item position facing the plant's front, "PopUp", nerve PopUp.
    void popUp(World&, Object& o, const V3& at, const V3& facing) {
        pos = at;
        front = facing;
        vel = {0, 0, 0};
        o.visible = true;
        o.alive = true;
        fromGround = false;
        o.startAction("PopUp");
        setNerve(Nerve::PopUp);
        place(o);
    }
    // The hold request (accepted in PullOut and Wait).
    bool grab(World& w, Object& o) {
        if (!grabbable()) {
            return false;
        }
        vel = {0, 0, 0};
        holdStep = 0;
        setNerve(nerve == Nerve::PullOut ? Nerve::HeldAfterPull : Nerve::Held);
        stepOnce(w, o);   // the nerve's step 0 runs with the message's tick
        return true;
    }
    // The hold component's placement (session, from the holder's hold matrix).
    // The component (nerve 0x1d43b0, parameters 0x5017a0: position and rotation easing on, 10
    // steps, start rate 0.2) eases the turnip in: for steps up to 10, s = 0.2 + 0.8 x clamp(step /
    // 10, 0, 1); the position becomes now x (1 - s) + hold point x s (scale 0x2fd34c, add 0x2fd230),
    // the rotation turns from the pose at the grab toward the hold rotation by s (0x2ae6e4 ->
    // slerp 0x23cde4). From step 11 on it sits at the hold point.
    // The turnip's components run twice a tick (the actor update 0x2d6f8c: its vtable +0x34
    // answers 1, so the list 0x2c8700 runs before and after the movement +0x78, which while held
    // only places the Body sensor, 0x266da0): two steps toward the same hold point (traced: steps
    // 0 and 1 at the grab's tick 1237, 2 and 3 at 1239).
    void holdAt(Object& o, const float m[12]) {
        if (holdStep == 0) {
            holdFrom = o.world;
        }
        captain_toad::RecoveredVec::M34 target;
        for (int k = 0; k < 12; k++) {
            target.m[k] = m[k];
        }
        for (int pass = 0; pass < 2; pass++) {
            const float t = captain_toad::RecoveredMath::clamp01(float(holdStep) / float(kHoldEaseSteps));
            const float rate = (1.0f - t) * kHoldStartRate + t;
            o.world = captain_toad::RecoveredMath::slerp(holdFrom, target, rate);
            pos = {pos.x * (1.0f - rate) + m[3] * rate, pos.y * (1.0f - rate) + m[7] * rate,
                   pos.z * (1.0f - rate) + m[11] * rate};
            o.world.m[3] = pos.x;
            o.world.m[7] = pos.y;
            o.world.m[11] = pos.z;
            holdStep++;
        }
    }
    // Let go by a damaged holder (onDamage 0x4399a4 -> 0x325f7c): hypothesis: it falls from the
    // hold position as from the pull (the PullOut launch without its upward speed) to Wait.
    void drop(World&, Object& o) {
        if (!held()) {
            return;
        }
        LaunchParams p = kPullOutLaunch;
        p.up = 0;
        startLaunch(p, 0.0f);
        setNerve(Nerve::PullOut);
        place(o);
    }
    // The throw (0x2670c4 + 0x2ddeac): face the holder's front, start the Attack launch.
    virtual void release(World&, Object& o, const V3& holderFront, const V3& holderVel) {
        if (!held()) {
            return;
        }
        front = {holderFront.x, 0, holderFront.z};
        captain_toad::RecoveredVec::normalize(front);
        const float speed = std::sqrt(holderVel.x * holderVel.x + holderVel.z * holderVel.z);
        startLaunch(kAttackLaunch, speed * kAttackLaunch.throwerSpeedRate);
        homingTarget = -1;
        setNerve(Nerve::Thrown);
        place(o);
    }

    // One step of the launch state 0x1d4a78, then the actor's movement 0x2a9124. The state reads
    // the contacts of the previous movement.
    void launchStepRun(World& w) {
        using namespace captain_toad::RecoveredVec;
        if (launchStep == 0) {
            vel = add(scale(front, forward), V3{0, launch.up, 0});
        }
        vel.y -= launch.gravityScale;       // 0x2fa2bc: gravity direction (0, -1, 0)
        vel = scale(vel, launch.damping);   // 0x2cdc48
        if (launchStep == launch.limit) {
            collisionOn = true;   // 0x2dd9bc, (+0x78)+10 cleared and the collider reset 0x2f9fec
            collider.reset(pos, kUp);
        }
        // In the air against a wall or a ceiling: 0x2f9a20 with the wall reflect, threshold 0 and
        // keep 1: the part of the velocity into the contacts' normal goes, plus reflect times it
        // back ("HitWall").
        if (!collider.onGround(vel) && (collider.wall().present() || collider.ceiling().present())) {
            const V3 n = collider.contactNormal();
            if (dot(n, n) >= 1.0e-6f) {
                const float into = dot(n, vel);
                if (into < 0.0f) {
                    vel = sub(add(vel, scale(n, -into)), scale(n, into * launch.wallReflect));
                }
            }
        }
        if (launchStep > launch.limit && collider.onGround(vel)) {
            launchEnded = true;   // the component ends (vtable +0x10)
        }
        launchStep++;
        // The movement 0x2a9124: through the collider, or (collision off) straight on with the
        // collider following (0x2f9fec).
        if (collisionOn && w.sphereContacts) {
            pos = add(pos, collider.move(w.sphereContacts, pos, kUp, vel));
        } else {
            pos = add(pos, vel);
            collider.reset(pos, kUp);
        }
    }
    // The thrown nerve's head-on test 0x3bb870 (last movement's wall contact): the wall's normal
    // against the flight direction by more than 110 degrees (literal 0x3bba98, cos 0x2f7264).
    bool headOnWall() const {
        if (!collider.wall().present()) {
            return false;
        }
        V3 d = vel;
        if (captain_toad::RecoveredVec::dot(d, d) < 1.0e-6f) {
            return false;
        }
        captain_toad::RecoveredVec::normalize(d);
        return captain_toad::RecoveredVec::dot(collider.wall().hit.faceNormal, d) < std::cos(1.9198622f);
    }

    // The centre of one of the turnip's sensors (Y up, Z along its front).
    V3 sensorAt(const SensorDecl& s) const {
        return captain_toad::RecoveredVec::add(captain_toad::RecoveredVec::add(pos, V3{0, s.y, 0}),
                                               captain_toad::RecoveredVec::scale(front, s.z));
    }
    static V3 objectSensorAt(const Object& e, const SensorDecl& s) {
        const auto& m = e.world.m;
        return {m[3] + m[1] * s.y, m[7] + m[5] * s.y, m[11] + m[9] * s.y};
    }
    // attackSensor 0x3bac14 while thrown.
    //   AttackSearch (id 0x16, r300 ahead) against an enemy body (kind 6): with no target yet, an
    //   enemy whose direction lies within 30 degrees of the flight (dot > 0.866, 0x3baf98)
    //   becomes the target (+0xbc); for the target the velocity gains the direction toward it
    //   times (direction . flight direction)^5 (0x300b48 pow with 5.0, 0x2f62a4 adds).
    //   Attack (id 4): the attack message 0x2efdb0 to the touched object; accepted by a MapObj
    //   sensor -> Break; accepted by another sensor -> hit reaction ヒット敵, Break. (The generic
    //   attack 0x2ec064 sent first and the reflection 0x2adf14 have no receiver here.)
    bool thrownContacts(World& w, Object& o) {
        const SensorDecl* search = o.sensor("AttackSearch");
        const SensorDecl* attack = o.sensor("Attack");
        const V3 v = vel;
        const float vl = std::sqrt(captain_toad::RecoveredVec::dot(v, v));
        for (size_t i = 0; i < w.objects.size(); i++) {
            Object& e = w.objects[i];
            Behaviour* b = w.behaviours[i].get();
            if (&e == &o || !e.alive || !b) {
                continue;
            }
            for (const SensorDecl& s : e.sensors) {
                if (s.type == "Eye" || s.type == "CollisionParts" || s.type == "HoldObj") {
                    continue;
                }
                const V3 c = objectSensorAt(e, s);
                if (search && s.type == "EnemyBody") {
                    const V3 me = sensorAt(*search);
                    V3 d = captain_toad::RecoveredVec::sub(c, me);
                    const float r = search->radius + s.radius;
                    const float dl = std::sqrt(captain_toad::RecoveredVec::dot(d, d));
                    if (dl < r && dl * dl >= 1.0e-6f && vl * vl >= 1.0e-6f) {
                        d = captain_toad::RecoveredVec::scale(d, 1.0f / dl);
                        const V3 vn = captain_toad::RecoveredVec::scale(v, 1.0f / vl);
                        const float c30 = captain_toad::RecoveredVec::dot(d, vn);
                        if (homingTarget < 0) {
                            if (c30 > 0.866f) {
                                homingTarget = int(i);
                            }
                        } else if (homingTarget == int(i)) {
                            vel = captain_toad::RecoveredVec::add(
                                vel, captain_toad::RecoveredVec::scale(d, std::pow(c30, 5.0f)));
                        }
                    }
                }
                if (attack) {
                    const V3 a = sensorAt(*attack);
                    const V3 d = captain_toad::RecoveredVec::sub(c, a);
                    const float r = attack->radius + s.radius;
                    w.attacker = {a, attack->radius, 0};
                    const bool hit = captain_toad::RecoveredVec::dot(d, d) < r * r && b->thrownHit(w, e, pos, false);
                    w.attacker = {{0, 0, 0}, 0, 0};
                    if (hit) {
                        if (s.type != "MapObj") {
                            w.reaction(o, "ヒット敵");
                        }
                        vel = {0, 0, 0};
                        setNerve(Nerve::Break);
                        return true;
                    }
                }
            }
        }
        return false;
    }
    // attackSensor 0x3bac14 in Break: the Explosion sensor (id 9) sends the explosion (0x2ec0d4)
    // to MapObj sensors, except hard blocks (0x2d9328: 硬ブロック, BlockHard).
    void explode(World& w, Object& o) {
        const SensorDecl* ex = o.sensor("Explosion");
        if (!ex) {
            return;
        }
        const V3 c = sensorAt(*ex);
        for (size_t i = 0; i < w.objects.size(); i++) {
            Object& t = w.objects[i];
            if (&t == &o || !t.alive || !w.behaviours[i] || t.cls == "BlockHard") {
                continue;
            }
            for (const SensorDecl& s : t.sensors) {
                if (s.type != "MapObj") {
                    continue;
                }
                const V3 d = captain_toad::RecoveredVec::sub(World::sensorCentre(t, s), c);
                const float r = ex->radius + s.radius;
                if (captain_toad::RecoveredVec::dot(d, d) < r * r) {
                    w.behaviours[i]->explosion(w, t);
                    break;
                }
            }
        }
    }

    // The item category runs every 60 Hz frame: two steps per game tick (InitExecutor アイテム;
    // see StagePowerUps.hpp).
    void control(World& w, Object& o) override {
        if (startTick == w.tickCount) {
            return;   // started this tick (traced: the turnip stands at the item position at 1225)
        }
        stepOnce(w, o);
        stepOnce(w, o);
        if (getenv("DIORAMA_RADISH_LOG") && nerve != Nerve::Hidden) {   // research: position per tick
            fprintf(stderr, "radish %s frame %d nerve %d step %d pos %.1f %.1f %.1f vel %.2f %.2f %.2f\n", o.id.c_str(),
                    w.researchFrame, int(nerve), step, pos.x, pos.y, pos.z, vel.x, vel.y, vel.z);
        }
    }
    virtual void stepOnce(World& w, Object& o) {
        switch (nerve) {
        case Nerve::Hidden:
            return;
        case Nerve::PullOut:   // 0x40c224: until its launch state has landed
            if (launchEnded) {
                setNerve(Nerve::Wait);
                return;
            }
            launchStepRun(w);
            place(o);
            break;
        case Nerve::PopUp:   // 0x40c1a8: at step 0 +0xc0 and the turn to the camera; -> Wait
            if (step == 0) {
                fromGround = true;
                // 0x2dc3c4 with the camera position (0x2f73dc): the front toward the camera
                // when the direction is not zero (0x2dc0b8 / 0x2f7be4; horizontal: hypothesis).
                V3 d = captain_toad::RecoveredVec::sub(w.cameraEye, pos);
                d.y = 0;
                if (captain_toad::RecoveredVec::dot(d, d) > 0) {
                    captain_toad::RecoveredVec::normalize(d);
                    front = d;
                }
            }
            place(o);
            if (o.isActionEnd() || !o.hasClip("PopUp")) {
                setNerve(Nerve::Wait);
                return;
            }
            break;
        case Nerve::Wait:   // 0x40bff0
            if (step == 0) {
                o.startAction("Wait");
                vel = {0, 0, 0};
                fromGround = true;
            }
            place(o);
            break;
        case Nerve::HeldAfterPull:      // 0x40c2c8: "Hold" once "PullOut" has ended
            collider.reset(pos, kUp);   // held: moved without collision, the collider follows (0x2f9fec)
            if (o.action == "PullOut" ? o.isActionEnd() || !o.hasClip("PullOut") : step == 0) {
                o.startAction("Hold");
            }
            break;
        case Nerve::Held:               // 0x25ddcc
            collider.reset(pos, kUp);   // held: moved without collision, the collider follows (0x2f9fec)
            if (step == 0) {
                o.startAction("Hold");
                if (fromGround) {
                    w.sound(o, "PgHold");
                }
            }
            break;
        case Nerve::Thrown:   // 0x3bb870: head-on wall or landed (last movement) -> Break
            if (headOnWall() || launchEnded) {
                vel = {0, 0, 0};
                setNerve(Nerve::Break);
                return;
            }
            launchStepRun(w);
            place(o);
            if (thrownContacts(w, o)) {
                return;
            }
            break;
        case Nerve::Inserted:
        case Nerve::Vanish:
            return;
        case Nerve::Break:   // 0x40c118
            w.reaction(o, "破裂");
            explode(w, o);
            if (hasCoin) {
                w.counters.coins += 1;   // InitItem コインx1[自動取得] (Timing コイン)
            }
            o.visible = false;
            o.alive = false;
            nerve = Nerve::Hidden;
            return;
        }
        step++;
    }
};

inline const Register<Radish> kRadish{"Radish"};

}   // namespace captain_toad::RecoveredStageRadish
