// =============================================================================================
// StageDoorKey.hpp - the door key a plant gives and the locked door it opens (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: pulling a plant three times brings out a key, which the protagonist
// carries over his head like a turnip. Walking up to a locked door with it pushes the key into
// the lock and the door swings open; throwing the key at the door opens it too. A key left on the
// ground blinks and vanishes after a while; the plant then gives it again.
//
// DoorKey (vtable 0x4cfc64, init 0x3c3cb0, receiveMsg 0x3c2c98, attackSensor 0x3c3160; model
//   DoorKeyBright in GettonForest / ClimbTower / HeyhoDarkness / ToadBrigadeClimbTower stages,
//   else DoorKey). The plant builds it dead at its init (0x143fa8 -> 0x3c42e0, plant +0xa8); on the
//   pull the keeper puts it at the item position and starts it (0x2ecdb4 -> 0x3c39c0: nerve
//   PullOut). It uses the turnip's hold component (0x266ca4) and launch states (0x2ddc30), so the
//   port derives it from the turnip (StageRadish.hpp). Nerves (0x4f5848..0x4f585c):
//     PullOut 0x40d34c   sound PgAppear; after step 9 (and its component's end) -> Ground.
//     Ground  0x3c3fd8   "Wait"; a timer of 750 steps (0x2dd5e0 with 0x2ee) whose blinks play
//                        BlinkL / BlinkH; gravity and the ground probe (0x3c3a08); at the timer's
//                        end (or in a DisappearDoorKeyArea after 30 steps) the hit reaction 消失
//                        and the key is gone.
//     Held    0x3c3668   "Hold", PgHold; the holder's hold position 50 / 60 (holder +0x1c / +0x20).
//     Thrown  0x40d220   the throw launch (block 0x501028: up 8, forward 25, gravity 1.3, damping
//                        0.99, limit 2, thrower 1.05, wall reflect 0.2) until landed: 着地 -> Ground.
//     Inserted 0x40d330  still; the door moves it into the lock (0x3c3b44).
//     Vanish  0x40d2dc   消失 and gone (after an insertion: two steps later, without the reaction).
//   receiveMsg 0x3c2c98: the hold request is accepted in Ground, or in PullOut after step 9; the
//   release (0x2670c4) starts the throw.
//   attackSensor 0x3c3160:
//     Held, from step 10: its Hold sensor (type 0xc) on the door's HoldKey sensor (0x2f) while the
//     holder faces the door: the angle between the holder's front and the door's back within 50
//     degrees, and the direction from the holder to the door within 40 degrees of his front
//     (|cross| -> 0x2eee88 -> x 57.2958, literals 0x3c3574..0x3c3584; the angles as asin of the
//     cross product's length: hypothesis for 0x2eee88). Then the key message (0x265c68, tag
//     0x4fe0f0); accepted: the key leaves the holder, keeps its position (+0xb4) and front (+0xcc),
//     takes the door's Goal joint (+0xc0, 0x2ef8e4) and the door's back (+0xd8) as targets, and
//     goes to Inserted.
//     Thrown: its Hold sensor on the door's ThrowKey sensor (0x18): the key message; accepted ->
//     Vanish.
//   Sensor types are indices into the name list at 0x4fd8e0 (Hold 12, Push 17, ThrowKey 24,
//   HoldKey 47).
//
// DoorLock (creator 0x454470, vtable 0x4d056c, init 0x3ccc3c, receiveMsg 0x3cca98, attackSensor
//   0x3ccb98): the frame (DoorLock.kcl) and the door panel, a collision actor "DoorLockDoor"
//   (0x2f0064; exported as slot 1). Nerves (0x4f5860..0x4f586c):
//     Wait   0x40e41c   "CloseWait"; accepts the key message -> Unlock (key at +0x94).
//     Unlock 0x40e440   moves the key into the lock each tick (0x3c3b44 over 2 key steps: the
//                       position and the front interpolated from the key's to the targets); after
//                       step 1 -> Open.
//     Open   0x40e350   "Open"; step 21 the hit reaction シーンストップ用; step 60 the door panel's
//                       collision goes (0x2cdaa4); at the action's end -> OpenWait.
//   attackSensor 0x3ccb98: in Open before step 60 its Push sensor (type 0x11) pushes the player
//   (0x2ef700).
// =============================================================================================
#pragma once

#include "game/StageRadish.hpp"

#include <cmath>

namespace captain_toad::RecoveredStageDoorKey {

using namespace captain_toad::RecoveredStage;
using captain_toad::RecoveredStageRadish::LaunchParams;
using captain_toad::RecoveredStageRadish::Radish;

inline constexpr LaunchParams kKeyThrow{8.0f, 25.0f, 1.3f, 0.99f, 2, 1.05f, 0.2f};   // 0x501028

struct DoorLock;

struct DoorKey : Radish {
    int timer = 0;           // the ground timer (steps)
    bool inserted = false;   // +0xb1
    V3 insertFrom{}, insertTo{}, frontFrom{}, frontTo{};
    int door = -1;   // the door the key went into
    float fallFloor = -1e30f;

    explicit DoorKey(Object& o) : Radish(o) {}

    bool grabbable() const override { return (nerve == Nerve::PullOut && step > 9) || nerve == Nerve::Wait; }

    // 0x3c39c0 from the keeper (0x2ecdb4): alive at the item position, nerve PullOut.
    void start(World& w, Object& o, const V3& at, const V3& facing) {
        startTick = w.tickCount;
        fallFloor = at.y - 3000.0f;
        pos = at;
        front = facing;
        vel = {0, 0, 0};
        o.visible = true;
        o.alive = true;
        fromGround = false;
        inserted = false;
        door = -1;
        setNerve(Nerve::PullOut);
        place(o);
    }

    // The door accepted the key from the holder (attackSensor 0x3c3160): toward the door's Goal
    // joint, turning to the door's back.
    void insert(Object& o, int doorIndex, const V3& goal, const V3& doorFront) {
        door = doorIndex;
        insertFrom = pos;
        insertTo = goal;
        frontFrom = front;
        frontTo = captain_toad::RecoveredVec::scale(doorFront, -1.0f);
        inserted = true;
        vel = {0, 0, 0};
        setNerve(Nerve::Inserted);
        o.startAction("Wait");
    }
    // 0x3c3b44 (called by the door each tick, with 2): the insertion by the key's own step.
    void insertStep(Object& o, int frames) {
        if (nerve != Nerve::Inserted) {
            return;
        }
        float t = 1.0f;
        if (frames > 0) {
            t = std::min(std::max(float(step) / float(frames), 0.0f), 1.0f);
        }
        pos = captain_toad::RecoveredVec::add(
            insertFrom, captain_toad::RecoveredVec::scale(captain_toad::RecoveredVec::sub(insertTo, insertFrom), t));
        V3 f = captain_toad::RecoveredVec::add(
            frontFrom, captain_toad::RecoveredVec::scale(captain_toad::RecoveredVec::sub(frontTo, frontFrom), t));
        f.y = 0;
        if (captain_toad::RecoveredVec::dot(f, f) > 1.0e-6f) {
            captain_toad::RecoveredVec::normalize(f);
            front = f;
        }
        place(o);
        if (frames <= step) {
            setNerve(Nerve::Vanish);
        }
    }

    // The key's sensors: Hold (r100, 60 up).
    V3 holdSensor(const Object& o) const {
        const SensorDecl* s = o.sensor("Hold");
        return captain_toad::RecoveredVec::add(pos, V3{0, s ? s->y : 60.0f, 0});
    }
    float holdRadius(const Object& o) const {
        const SensorDecl* s = o.sensor("Hold");
        return s ? s->radius : 100.0f;
    }

    bool carriedIntoDoor(World& w, Object& o);
    bool thrownIntoDoor(World& w, Object& o);

    void stepOnce(World& w, Object& o) override {
        switch (nerve) {
        case Nerve::PullOut:   // 0x40d34c
            if (step == 0) {
                w.sound(o, "PgAppear");
            }
            place(o);
            // Hypothesis: its component (the nerve keeper's state +0x10 +0xc) has ended.
            if (step > 9) {
                setNerve(Nerve::Wait);
                return;
            }
            break;
        case Nerve::Wait:   // Ground 0x3c3fd8
            if (step == 0) {
                o.startAction("Wait");
                timer = 750;
                fromGround = true;
                vel = {0, 0, 0};
            }
            if (--timer <= 0) {   // 0x3c413c: 消失, back to Ground (0x4f5848), killed (vt+0x18)
                w.reaction(o, "消失");
                o.visible = false;
                o.alive = false;
                setNerve(Nerve::Wait);
                return;
            }
            // 0x3c40d8: gravity 2 while off the ground (0x2f9bf4 with 4 grace ticks, 0x2fa2bc),
            // damping 0.9875 (0x2fa28c), then the ground probe 0x3c3a08 (here the collider's move).
            if (w.sphereContacts) {
                if (!collider.onGround(vel, 4)) {
                    vel.y -= 2.0f;
                }
                vel = captain_toad::RecoveredVec::scale(vel, 0.9875f);
                pos = captain_toad::RecoveredVec::add(pos, collider.move(w.sphereContacts, pos, kUp, vel));
            }
            place(o);
            break;
        case Nerve::HeldAfterPull:
        case Nerve::Held:   // 0x3c3668
            collider.reset(pos, kUp);
            if (step == 0) {
                o.startAction("Hold");
                if (fromGround) {
                    w.sound(o, "PgHold");
                }
            }
            if (step >= 10 && carriedIntoDoor(w, o)) {
                return;
            }
            break;
        case Nerve::Thrown:   // 0x40d220
            // Native guard: a key thrown off a course without a death area would fall forever
            // (the original's checks 0x2658a4 are crush tests); it vanishes 3000 below its plant.
            if (pos.y < fallFloor) {
                setNerve(Nerve::Vanish);
                return;
            }
            if (launchEnded) {
                w.reaction(o, "着地");
                setNerve(Nerve::Wait);
                return;
            }
            launchStepRun(w);
            place(o);
            if (thrownIntoDoor(w, o)) {
                return;
            }
            break;
        case Nerve::Inserted:   // 0x40d330 (moved by the door)
            break;
        case Nerve::Vanish:   // 0x40d2dc
            if (!inserted) {
                w.reaction(o, "消失");
            } else if (step <= 1) {
                break;
            }
            o.visible = false;
            o.alive = false;   // stays in Vanish (the plant's callback 0x438314 reads it)
            return;
        default:
            Radish::stepOnce(w, o);
            return;
        }
        step++;
    }
    // The throw (0x2670c4 + 0x2ddeac with block 0x501028).
    void release(World&, Object& o, const V3& holderFront, const V3& holderVel) override {
        if (!held()) {
            return;
        }
        front = {holderFront.x, 0, holderFront.z};
        captain_toad::RecoveredVec::normalize(front);
        const float speed = std::sqrt(holderVel.x * holderVel.x + holderVel.z * holderVel.z);
        startLaunch(kKeyThrow, speed * kKeyThrow.throwerSpeedRate);
        setNerve(Nerve::Thrown);
        place(o);
    }
};

struct DoorLock : Behaviour {
    enum class Nerve { Wait, Unlock, Open, OpenWait } nerve = Nerve::Wait;
    int step = 0;
    int key = -1;   // +0x94

    explicit DoorLock(Object& o) {
        o.partWorlds = {o.placement, o.placement};
        o.partShown = {true, true};
        enter(o, Nerve::Wait);
    }
    void enter(Object& o, Nerve n) {
        nerve = n;
        step = 0;
        if (n == Nerve::Wait) {
            o.startAction("CloseWait");
        } else if (n == Nerve::Open) {
            o.startAction("Open");
        } else if (n == Nerve::OpenWait) {
            o.startAction("OpenWait");
        }
    }
    V3 sensorAt(const Object& o, const char* name) const {
        const SensorDecl* s = o.sensor(name);
        if (!s) {
            return o.translation();
        }
        const auto& m = o.world.m;   // the sensor's offset in the door's frame (Y up, Z front)
        return {m[3] + m[1] * s->y + m[2] * s->z, m[7] + m[5] * s->y + m[6] * s->z, m[11] + m[9] * s->y + m[10] * s->z};
    }
    V3 front(const Object& o) const {
        V3 f{o.world.m[2], o.world.m[6], o.world.m[10]};
        captain_toad::RecoveredVec::normalize(f);
        return f;
    }
    // receiveMsg 0x3cca98: the key message, accepted in Wait.
    bool keyArrives(Object& o, int keyIndex) {
        if (nerve != Nerve::Wait) {
            return false;
        }
        key = keyIndex;
        enter(o, Nerve::Unlock);
        return true;
    }
    // attackSensor 0x3ccb98: the Push sensor while opening.
    bool pushesPlayer(const Object& o) const override { return o.alive && nerve == Nerve::Open && step < 60; }
    const char* pushSensor() const override { return "Push"; }

    void control(World& w, Object& o) override {
        switch (nerve) {
        case Nerve::Wait:
            break;
        case Nerve::Unlock:   // 0x40e440
            if (key >= 0) {
                if (auto* k = dynamic_cast<DoorKey*>(w.behaviours[size_t(key)].get())) {
                    k->insertStep(w.objects[size_t(key)], 2);
                }
            }
            if (step > 1) {
                enter(o, Nerve::Open);
                return;
            }
            break;
        case Nerve::Open:   // 0x40e350
            if (step == 21) {
                w.reaction(o, "シーンストップ用");
            }
            if (step == 60) {
                o.partShown[1] = false;   // 0x2cdaa4: the door panel's collision goes
            }
            if (o.isActionEnd()) {
                enter(o, Nerve::OpenWait);
                return;
            }
            break;
        case Nerve::OpenWait:
            break;
        }
        step++;
    }
};

// attackSensor 0x3c3160, Held: the door within reach and faced.
inline bool DoorKey::carriedIntoDoor(World& w, Object& o) {
    const V3 h = holdSensor(o);
    for (size_t i = 0; i < w.objects.size(); i++) {
        Object& d = w.objects[i];
        auto* lock = dynamic_cast<DoorLock*>(w.behaviours[i].get());
        const SensorDecl* hk = d.sensor("HoldKey");
        if (!lock || !d.alive || !hk) {
            continue;
        }
        const V3 c = lock->sensorAt(d, "HoldKey");
        const V3 dd = captain_toad::RecoveredVec::sub(c, h);
        const float r = hk->radius + holdRadius(o);
        if (captain_toad::RecoveredVec::dot(dd, dd) >= r * r) {
            continue;
        }
        // The holder's front against the door's back, and toward the door (horizontal).
        V3 hf{w.player.front.x, 0, w.player.front.z};
        captain_toad::RecoveredVec::normalize(hf);
        V3 back = captain_toad::RecoveredVec::scale(lock->front(d), -1.0f);
        back.y = 0;
        captain_toad::RecoveredVec::normalize(back);
        const V3 c1 = captain_toad::RecoveredVec::cross(hf, back);
        const float a1 = std::asin(std::min(1.0f, std::sqrt(captain_toad::RecoveredVec::dot(c1, c1)))) * 57.2958f;
        if (a1 > 50.0f) {
            continue;
        }
        V3 to = captain_toad::RecoveredVec::sub(d.translation(), w.player.pos);
        to.y = 0;
        if (captain_toad::RecoveredVec::dot(to, to) > 1.0e-6f) {
            captain_toad::RecoveredVec::normalize(to);
        }
        const V3 c2 = captain_toad::RecoveredVec::cross(hf, to);
        const float a2 = std::asin(std::min(1.0f, std::sqrt(captain_toad::RecoveredVec::dot(c2, c2)))) * 57.2958f;
        if (a2 > 40.0f) {
            continue;
        }
        size_t self = 0;
        for (size_t k = 0; k < w.objects.size(); k++) {
            if (&w.objects[k] == &o) {
                self = k;
            }
        }
        if (!lock->keyArrives(d, int(self))) {
            continue;
        }
        M34 goal;
        V3 g = d.translation();
        if (d.jointMatrix && d.jointMatrix("Goal", goal)) {
            g = {goal.m[3], goal.m[7], goal.m[11]};
        }
        insert(o, int(i), g, lock->front(d));
        return true;
    }
    return false;
}

// attackSensor 0x3c3160, Thrown: the door's ThrowKey sensor.
inline bool DoorKey::thrownIntoDoor(World& w, Object& o) {
    const V3 h = holdSensor(o);
    for (size_t i = 0; i < w.objects.size(); i++) {
        Object& d = w.objects[i];
        auto* lock = dynamic_cast<DoorLock*>(w.behaviours[i].get());
        const SensorDecl* tk = d.sensor("ThrowKey");
        if (!lock || !d.alive || !tk) {
            continue;
        }
        const V3 dd = captain_toad::RecoveredVec::sub(lock->sensorAt(d, "ThrowKey"), h);
        const float r = tk->radius + holdRadius(o);
        if (captain_toad::RecoveredVec::dot(dd, dd) < r * r && lock->keyArrives(d, -1)) {
            vel = {0, 0, 0};
            setNerve(Nerve::Vanish);
            return true;
        }
    }
    return false;
}

inline const Register<DoorKey> kDoorKey{"DoorKey"};
inline const Register<DoorLock> kDoorLock{"DoorLock"};

}   // namespace captain_toad::RecoveredStageDoorKey
