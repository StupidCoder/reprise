// =============================================================================================
// StageSwitches.hpp - switches and the map parts they move (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: a lever sticks out of the ground; pulling it makes a stone lift with a
// ladder rise out of the floor, bounce once and stop, opening the way up.
//
// Stage switches: placements link to StageSwitch targets by id. An object turns its switch on
// (SwitchPullOn, SwitchTimerOn, ...); listeners react to SwitchStart (start moving) and
// SwitchValidate (become usable). World::switchesOn holds the switches that are on.
//
// SwitchPull (creator 0x4529e4, vtable 0x4bd760, init 0x14d8cc): a pullable lever (PullType 0:
//   once; 1: again). Sensors Body r80 y40 and Push r10 y10.
//   receiveMsg 0x14d6f0: in OffWait (0x4146a4, "OffWait") it accepts the pull request (0x2e2418);
//   the pull (0x2e23a0) replies Success (0x2e2364: the player plays PullOutSuccess) -> On.
//   On 0x14da6c: step 0 "On"; step 6 turns SwitchPullOn on (0x2f6884); step 11 invalidates the Push
//   sensor (0x2ebf7c); after step 6 at the end of the action: PullType 0 -> OffNeverWait
//   (0x4146c8, "OffNeverWait"), 1 -> OffWait.
//
// GateMapParts (creator 0x454804, vtable 0x4d2ed4, init 0x224ca0; units PullUpKeyLiftA/B): a map
//   part that moves to its MoveNext link when its SwitchStart switch turns on. Named nerves
//   (table at 0x535a7c, descriptors 0x48cfe8..0x48d024): Wait 0x41d0d4, Open 0x224e98,
//   Bound 0x224fe4, End 0x41d084. Parameters: OpenTime (ticks x 0.5, 0x224e20), BoundRate,
//   HitReactionCount; at most 10 bounces (+0xc8).
//   0x296db0(t): t clamped to [0, 1]; translation = start + (MoveNext - start) * t; rotation slerped
//   between the two placements (all ported moves are pure translations).
//   Open: u = step / (T - 1) clamped, t = u * u. At step T - 1: bound amplitude a = BoundRate,
//     bound duration D = int(T * 2 * a); with D >= 2 -> Bound, else End (hit reaction while
//     HitReactionCount <= 1).
//   Bound: step 0 counts the bounce (hit reaction while count <= HitReactionCount; sound
//     BoundStart); u = step / (D - 1), t = 1 - a^2 + (a (2u - 1))^2 (a dip of depth a^2); at step
//     D - 1: a *= BoundRate, D = int(D * BoundRate); another Bound while count < 10 and D >= 2,
//     else End.
//   End: t = 1 (step 0).
//   Named nerves (0x2cdb24): 0x2fd600 sets a nerve by name, starts the action of that name on
//   the action keeper (0x24ced0) and leaves the step at -1; the executor 0x2c8700 switches to a
//   pending nerve with step 0, runs it, increments the step, and switches at once (step 0) to a
//   nerve set during the run. The keepers then see frame = step - 1 in steps of 2 (0x24cd6c),
//   so an entry at StartFrame 0 fires on two consecutive ticks (frames 0 and 1): every lift effect
//   and sound comes twice, one tick apart (run sfxA). Sound entry PullUpKeyLift: Open (a level
//   sound while it rises), Bound, End; the Bound nerve's step 0 also plays "BoundStart" (0x2bdb00).
//   SwitchStart is a switch listener (0x2fc838) that sets Open from outside the lift's update;
//   the port polls it in Wait, as if the listener ran before the lift's executor that tick.
//   The part's collision moves with it (World::Object::world drives the collider part).
//
// TimerStageSwitch (creator 0x45339c, vtable 0x4c54d4, init 0x1ae86c): starts dead and appears when
//   SwitchStart turns on (0x2bfc74); control 0x1ae938 counts ticks up to Timer x 0.5, then turns
//   SwitchTimerOn on and kills itself.
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

#include <cmath>

namespace captain_toad::RecoveredStageSwitches {

using namespace captain_toad::RecoveredStage;

struct SwitchPull : Behaviour {
    enum class Nerve { OffWait, On, OffNeverWait } nerve = Nerve::OffWait;
    int step = 0;
    int pullType = 0;
    bool reserved = false;
    bool pushValid = true;

    // IsConnectToCollision: the lever rides the part below it (PullUpKeyLiftA's lever rises with
    // the lift it raises).
    CollisionConnector connector;

    explicit SwitchPull(Object& o) { pullType = int(o.number("PullType", 0)); }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    void control(World& w, Object& o) override {
        // 0x14d830 (after the placement): the ray from 50 above, 400 down along the world's Y.
        const V3 at = o.translation();
        if (connector.connect(w, o, o.placement, {at.x, at.y + 50.0f, at.z}, {0.0f, -400.0f, 0.0f})) {
            o.world = connector.follow(w, o.placement);
        }
        switch (nerve) {
        case Nerve::OffWait:
            if (step == 0) {
                o.startAction("OffWait");
            }
            break;
        case Nerve::OffNeverWait:
            if (step == 0) {
                o.startAction("OffNeverWait");
            }
            break;
        case Nerve::On:
            if (step == 0) {
                o.startAction("On");
            }
            if (step == 6) {
                w.turnOn(o, "SwitchPullOn");
            }
            if (step == 11) {
                pushValid = false;
            }
            if (step > 6 && o.isActionEnd()) {
                setNerve(pullType == 1 ? Nerve::OffWait : Nerve::OffNeverWait);
                return;
            }
            break;
        }
        step++;
    }
    bool pullable(const Object&) const override { return nerve == Nerve::OffWait; }
    bool pullRequest(World&, Object&) override {
        if (nerve != Nerve::OffWait || reserved) {
            return false;
        }
        reserved = true;
        return true;
    }
    PullReply pull(World&, Object&) override {
        if (!reserved) {
            return PullReply::None;
        }
        reserved = false;
        setNerve(Nerve::On);
        return PullReply::Success;
    }
    void pullCancel(World&, Object&) override { reserved = false; }
};

inline const Register<SwitchPull> kSwitchPull{"SwitchPull"};

struct GateMapParts : Behaviour {
    enum class Nerve { Wait, Open, Bound, End } nerve = Nerve::Wait;
    int step = 0;
    int openTime = 60;          // +0xcc
    int maxBounces = 10;        // +0xc8
    float boundRate = 0.1f;     // +0xd0
    int hitReactionCount = 0;   // +0xe0
    int boundDuration = 0;      // +0xd4
    int bounces = 0;            // +0xd8
    float amplitude = 0;        // +0xdc
    V3 start, target;

    explicit GateMapParts(Object& o) {
        o.nerveDriven = true;   // named nerves (init 0x224ca0 -> 0x2cdb24)
        openTime = int(float(o.number("OpenTime", 120)) * 0.5f);
        boundRate = float(o.number("BoundRate", 0.1));
        hitReactionCount = int(o.number("HitReactionCount", 0));
        start = o.translation();
        const LinkTarget* next = o.link("MoveNext");
        target = next ? next->translation() : start;
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    // A named nerve (0x2fd600) is also the action of that name, started when the nerve is set.
    static const char* actionOf(Nerve n) { return n == Nerve::Open ? "Open" : n == Nerve::Bound ? "Bound" : "End"; }
    void enter(Object& o, Nerve n) {
        setNerve(n);
        o.startAction(actionOf(n));
    }
    // 0x296db0 for a pure translation.
    void place(Object& o, float t) const {
        t = std::min(std::max(t, 0.0f), 1.0f);
        o.world = o.placement;
        o.world.m[3] = start.x + (target.x - start.x) * t;
        o.world.m[7] = start.y + (target.y - start.y) * t;
        o.world.m[11] = start.z + (target.z - start.z) * t;
    }
    static float progress(int step, int duration) {
        if (duration - 1 <= 0) {
            return 1.0f;
        }
        return std::min(std::max(float(step) / float(duration - 1), 0.0f), 1.0f);
    }
    void control(World& w, Object& o) override {
        run(w, o);
        o.nerveStep = step;   // the step after the executor, as the action keeper reads it
    }
    void run(World& w, Object& o) {
        switch (nerve) {
        case Nerve::Wait:
            if (w.switchOn(o, "SwitchStart")) {
                enter(o, Nerve::Open);   // the listener; Open's step 0 (t = 0) ran this tick
                break;
            }
            step++;
            return;
        case Nerve::Open: {
            const float u = progress(step, openTime);
            place(o, u * u);
            if (step >= openTime - 1) {
                amplitude = boundRate;
                boundDuration = int(float(openTime) * 2.0f * boundRate);
                if (maxBounces >= 1 && boundDuration >= 2) {
                    enter(o, Nerve::Bound);
                    return;
                }
                if (hitReactionCount <= 1) {
                    w.reaction(o, "バウンド");
                }
                enter(o, Nerve::End);
                return;
            }
            break;
        }
        case Nerve::Bound: {
            if (step == 0) {
                bounces++;
                if (bounces <= hitReactionCount) {
                    w.reaction(o, "バウンド");
                }
                w.sound(o, "BoundStart");
            }
            const float u = progress(step, boundDuration);
            const float a = amplitude;
            const float s = a * (2.0f * u - 1.0f);
            place(o, 1.0f - a * a + s * s);
            if (step >= boundDuration - 1) {
                amplitude = amplitude * boundRate;
                boundDuration = int(float(boundDuration) * boundRate);
                enter(o, bounces < maxBounces && boundDuration >= 2 ? Nerve::Bound : Nerve::End);
                return;
            }
            break;
        }
        case Nerve::End:
            if (step == 0) {
                place(o, 1.0f);
            }
            break;
        }
        step++;
    }
};

inline const Register<GateMapParts> kGateMapParts{"GateMapParts"};

struct TimerStageSwitch : Behaviour {
    int timer = 0, count = 0;
    bool done = false;
    explicit TimerStageSwitch(Object& o) { timer = int(float(o.number("Timer", 0)) * 0.5f); }
    void control(World& w, Object& o) override {
        if (done || !w.switchOn(o, "SwitchStart")) {
            return;
        }
        if (++count >= timer) {
            w.turnOn(o, "SwitchTimerOn");
            done = true;
        }
    }
};

inline const Register<TimerStageSwitch> kTimerStageSwitch{"TimerStageSwitch"};

}   // namespace captain_toad::RecoveredStageSwitches
