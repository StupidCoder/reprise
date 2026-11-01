// =============================================================================================
// StageMoking.hpp - the diving bird enemy and its flock leader (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: a bird flies back and forth along a rail high above the course. At each
// end it stops, turns round and flies back. When the protagonist walks underneath, it slides
// over him, drops like a stone, lands with a thud and flies back up to its rail. Its talons cost
// him a life.
//
// Moking (constructor 0x3b6698, vtable 0x4cf620, init 0x3b60c8; archive "Moking"): created by its
// MokingLeader (creator 0x452d38, init 0x1692f0: one bird per LinkMoking link). The init attaches
// the bird to its Rail link (facing along it), a collider of radius 70 at 80 up (0x2f987c) and the
// sight 0x2b6e6c with the block 0x500290: view 750, facing 60, elevation 60, close range 750, no
// height limits, no keep distance. IsOffColliderAtRail: no ground collision while on the rail.
// Its fields: +0xc4 speed, +0xc8 awake (set by the leader; 1 from the constructor), +0xc9 in a
// turn, +0xca a turn the leader forced, +0x98 the bird whose dive aims at the player, +0xa8 the
// slide step, +0xac where the slide starts, +0x9c where it ends, +0xb8 the front of the last Fly
// tick, +0xcb / +0xcc the dive hit an object, +0xec lifted (ReturnRail).
// Nerves (table 0x4f4f74):
//   Fly 0x3b5010          "Fly" ("FlyUp" where the rail rises steeply); asleep -> Hover. Speed
//                         (v + 0.7) x 0.98, at most 9.5, along the rail (open rail 0x2d273c: at an
//                         end the direction flips -> Turn; a closed one goes round, 0x2abd0c);
//                         turns toward the direction of travel by 9 degrees a tick; the dive check.
//   Turn 0x3b5244         speed 0; turns to the direction of travel by 9 degrees a tick (a forced
//                         turn: the opposite one, then the direction flips); the dive check while
//                         turning; facing it -> Fly when awake, else Hover.
//   Hover 0x40b4fc        "Fly", stopped; awake -> Turn (in a turn) or Fly.
//   Ready 0x40b6ac        (0x4f4f80) stopped, waiting for the leader.
//   AttackSign 0x3b56f4   "AttackSign"; the collider on; the aiming bird slides x / z from +0xac
//                         to +0x9c over 10 steps; at the clip's end -> Attack.
//   Attack 0x3b6438       "Attack"; vy = min((vy - 40) x 0.995, 50); landed (0x3b5f2c) ->
//                         AttackLand.
//   AttackLand 0x3b5584   "AttackLand" ("AttackGuard" after hitting an object); stopped;
//                         reaction 地面衝突 (radial blur); at the clip's end -> ReturnRail.
//   ReturnRail 0x3b584c   "ReturnRail"; first 50 up; velocity += toward the rail point (where the
//                         bird left the rail) x 0.7, x 0.995, at most 7; once the height left is
//                         below one tick's climb -> ReturnRailEnd.
//   ReturnRailEnd 0x3b5a68 "ReturnRailEnd"; stopped; the aiming bird snaps x / z onto the rail
//                         point; at the clip's end -> Hover (asleep), Turn (in a turn) or Fly.
// The dive check 0x2673a8: the sight has a target (0x2dcfd8) below the bird whose horizontal
// offset lies within 150 both along the bird's front (+0xb8) and across it (0x4381c4). Then the
// slide's end is the target's horizontal direction, at most 80 away (the target itself when right
// below; no ground probe with GroundCheckLength <= 0), and the bird -> Ready with +0x98.
// attackSensor 0x3b5384: the Attack sensor reaching the player's sensors sends the enemy attack
// (0x2f0914; not from AttackGuard).
//
// MokingLeader (vtable 0x4bf9f4; nerves 0x4f4fac..; execs 0x16956c, 0x418a5c, 0x169078):
//   Watch 0x169574    a bird ready to dive (Ready .. ReturnRailEnd): all birds asleep, all to
//                     AttackSign (0x2d12a8; only the first keeps +0x98) -> Dive. A bird on an
//                     open rail turning: all asleep, the others forced to turn (0x3b6620) -> Turn.
//   Dive 0x418a5c     waits until no bird dives; then all awake -> Watch, or -> Turn after a turn.
//   Turn 0x169080     while a bird turns: a dive sends all to AttackSign -> Dive; when none turns
//                     any more: all awake -> Watch.
// Traced (run freshruins-start-a, obj740 alone, frames at 60 Hz): Fly 0 -> Turn 40 -> Hover 82 ->
// Fly 84 (speed 0.69 at 86) -> Turn 214: 65 ticks to cross the 550 rail, 21 to turn.
// Not ported: the touch hold (0x3b5c00), the press (PressDown 0x40b5e0), the death-area fall in
// Attack (0x2f0e24), the landing's quake (0x2c0d34 with 70) and attacks on landed-on objects, the
// push of the player in AttackSign / Attack (0x335d7c), the ground probe (GroundCheckLength > 0),
// the steep-slope landing snap in Attack.
// =============================================================================================
#pragma once

#include "game/Push.hpp"
#include "game/StageEnemyChase.hpp"
#include "game/StageObjects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace captain_toad::RecoveredStageMoking {

using namespace captain_toad::RecoveredStage;
using captain_toad::RecoveredStageChase::Body;
using captain_toad::RecoveredStageChase::RailPath;
using captain_toad::RecoveredStageChase::Sight;

struct Moking : Behaviour {
    enum class Nerve { Fly, Turn, Hover, Ready, AttackSign, Attack, AttackLand, ReturnRail, ReturnRailEnd, Blow };
    static constexpr float kTurn = 9.0f;
    static constexpr float kAccel = 0.7f, kDamping = 0.98f, kMaxSpeed = 9.5f;
    static constexpr float kDiveBox = 150.0f, kSlideMax = 80.0f;
    static constexpr int kSlideSteps = 10;
    static constexpr float kDiveGravity = 40.0f, kDiveDamping = 0.995f, kDiveMaxUp = 50.0f;
    static constexpr float kLift = 50.0f, kReturnAccel = 0.7f, kReturnDamping = 0.995f, kReturnMax = 7.0f;
    // The knock-away (吹き飛び, 0x5002b4): 25 out, 55 up, damping 0.96, gravity 3.5.
    static constexpr float kBlowOut = 25.0f, kBlowUp = 55.0f, kBlowDamp = 0.96f, kBlowGravity = 3.5f;
    static constexpr int kBlowMinSteps = 10;

    Nerve nerve = Nerve::Fly;
    int step = 0;
    RailPath rail;
    float s = 0;                                              // distance along the rail (the rail mover's position)
    bool forward = true;                                      // the rail mover's direction (+0x24 clear)
    float speed = 0;                                          // +0xc4
    bool awake = true, turning = false, forcedTurn = false;   // +0xc8, +0xc9, +0xca
    bool aims = false;                                        // +0x98
    int slideStep = 0;                                        // +0xa8
    V3 slideFrom, slideTo;                                    // +0xac, +0x9c
    V3 front;                                                 // +0xb8
    bool lifted = false;                                      // +0xec
    bool offColliderAtRail = false, colliderOff = false;
    Body body;
    Sight sight;

    explicit Moking(Object& o) {
        body.collider = captain_toad::RecoveredActorCollider::ActorCollider(70.0f, 80.0f);
        offColliderAtRail = o.number("IsOffColliderAtRail", 0) != 0;
        sight.p = {750, 60, 60, 750, -1, -1, -1};
        body.pos = o.translation();
        body.front = {o.placement.m[2], 0, o.placement.m[10]};
        captain_toad::RecoveredVec::normalize(body.front);
        if (const LinkTarget* r = o.link("Rail")) {
            rail = RailPath(r->points, r->closed);
        }
        if (!rail.empty()) {   // attached where it stands, facing along the rail (0x2f6714, 0x2edc80)
            s = rail.nearest(body.pos);
            body.pos = rail.at(s);
            body.front = rail.tangent(s);
        }
        colliderOff = offColliderAtRail;
        body.followWithoutCollision();
        place(o);
    }
    void place(Object& o) const {
        const V3& f = body.front;
        const float m[12] = {f.z, 0, f.x, body.pos.x, 0, 1, 0, body.pos.y, -f.x, 0, f.z, body.pos.z};
        for (int k = 0; k < 12; k++) {
            o.world.m[k] = m[k];
        }
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    bool alive(const Object& o) const { return o.alive && nerve != Nerve::Blow; }   // 0x2d13d8
    // 0x2d12dc: Ready .. ReturnRailEnd.
    bool diving() const {
        return nerve == Nerve::Ready || nerve == Nerve::AttackSign || nerve == Nerve::Attack ||
               nerve == Nerve::AttackLand || nerve == Nerve::ReturnRail || nerve == Nerve::ReturnRailEnd;
    }
    bool turningOnOpenRail() const { return !rail.closed && nerve == Nerve::Turn; }   // 0x2f6704, 0x2d13b0
    // 0x2d12a8: the leader starts the dive.
    void startDive() {
        slideStep = 0;
        setNerve(Nerve::AttackSign);
    }
    // 0x3b6620: the leader stops the bird for another bird's turn.
    void forceTurn() {
        if (!turning && nerve == Nerve::Hover) {
            return;
        }
        if (nerve != Nerve::Turn) {
            forcedTurn = true;
            setNerve(Nerve::Turn);
        }
    }
    V3 travel() const { return rail.heading(s, forward); }   // 0x2f77cc
    V3 railPoint() const { return rail.at(s); }              // 0x2ebdc8

    // 0x2d273c / 0x2b3b80 (open rail): move by the speed; at an end the direction flips.
    bool moveOpen() {
        const float to = s + (forward ? speed : -speed);
        if (to >= rail.total || to <= 0) {
            s = std::min(std::max(to, 0.0f), rail.total);
            forward = !forward;
            return true;
        }
        s = to;
        return false;
    }

    // 0x2673a8 with 0x4381c4: dive when the sight's target is below and within the box.
    bool diveCheck(const World& w) {
        sight.update(w, body);
        if (!sight.target || w.player.pos.y >= body.pos.y) {
            return false;
        }
        const V3 target = w.player.pos;
        const V3 h = captain_toad::RecoveredStageChase::horizontal(captain_toad::RecoveredVec::sub(target, body.pos));
        const float along = captain_toad::RecoveredVec::dot(front, h);
        const float a = std::fabs(along);
        const float b = captain_toad::RecoveredStageChase::length(
            captain_toad::RecoveredVec::sub(h, captain_toad::RecoveredVec::scale(front, along)));
        if (!(a < kDiveBox && b < kDiveBox)) {
            return false;
        }
        slideFrom = body.pos;
        const float len = captain_toad::RecoveredStageChase::length(h);
        if (len < 0.001f) {
            slideTo = target;
        } else {
            slideTo = captain_toad::RecoveredVec::add(
                slideFrom, captain_toad::RecoveredVec::scale(h, std::min(len, kSlideMax) / len));
        }
        aims = true;
        setNerve(Nerve::Ready);
        return true;
    }

    void setCollider(bool off) {
        colliderOff = off;
        body.followWithoutCollision();
    }

    std::vector<int> contacts;   // the player sensors the Attack sensor overlapped at the last tick's end
    void recordContacts(const World& w, const Object& o) {
        contacts.clear();
        const SensorDecl* atk = o.sensor("Attack");
        if (!atk || !alive(o)) {
            return;
        }
        const V3 c = captain_toad::RecoveredVec::add(body.pos, V3{0, atk->y, 0});
        const auto& sensors = playerSensors(w.player.mini);
        for (int k = 0; k < 3; k++) {
            const V3 pc = captain_toad::RecoveredVec::add(
                w.player.pos, captain_toad::RecoveredVec::scale(w.player.up, sensors[k].up));
            if (captain_toad::RecoveredPush::overlaps(pc, sensors[k].radius, c, atk->radius)) {
                contacts.push_back(k);
            }
        }
    }
    // attackSensor 0x3b5384: the enemy attack (0x2f0914), not from AttackGuard.
    void deliverContacts(World& w, const Object& o) {
        if (!contacts.empty() && alive(o) && !(nerve == Nerve::AttackLand && o.action == "AttackGuard")) {
            w.playerAttacked = true;
        }
    }

    // receiveMsg 0x3b4cdc: a thrown object's attack knocks the bird away (0x2f5220, nerve 0x4f4fa0).
    bool thrownHit(World& w, Object& o, const V3& from, bool direct) override {
        if (!alive(o)) {
            return false;
        }
        V3 d{body.pos.x - from.x, 0, body.pos.z - from.z};
        if (d.x == 0 && d.z == 0) {
            d = {0, 0, kBlowOut};
        } else {
            captain_toad::RecoveredVec::normalize(d);
            d = captain_toad::RecoveredVec::scale(d, kBlowOut);
        }
        body.vel = {d.x, kBlowUp, d.z};
        body.front = {-d.x, 0, -d.z};
        captain_toad::RecoveredVec::normalize(body.front);
        w.reaction(o, direct ? "吹き飛びヒット[直接ヒット]" : "吹き飛びヒット");
        setNerve(Nerve::Blow);
        return true;
    }
    // Blow 0x40b584 with the knock-away component: flies off; when it ends the bird dies (0x2f4060)
    // and leaves its coin (0x2fa17c).
    void blowTick(World& w, Object& o) {
        if (step == 0) {
            o.startAction("BlowDown");
            setCollider(false);
        }
        if (!body.onGround()) {
            body.vel.y -= kBlowGravity;
            body.vel = captain_toad::RecoveredVec::scale(body.vel, kBlowDamp);
        } else {
            body.stop();
        }
        if ((o.isActionEnd() && o.hasClip("BlowDown")) || (step > kBlowMinSteps && body.collider.wall().present())) {
            w.reaction(o, "死亡");
            w.counters.coins += 1;
            o.visible = false;
            o.alive = false;
            return;
        }
        body.integrate(w);
        place(o);
        step++;
    }

    void control(World& w, Object& o) override {
        if (rail.empty()) {
            return;
        }
        if (nerve == Nerve::Blow) {
            blowTick(w, o);
            return;
        }
        deliverContacts(w, o);
        nerveTick(w, o);
        if (colliderOff) {
            body.pos = captain_toad::RecoveredVec::add(body.pos, body.vel);
            body.followWithoutCollision();
        } else {
            body.integrate(w);
        }
        place(o);
        recordContacts(w, o);
        if (getenv("DIORAMA_CHASE_LOG")) {
            fprintf(stderr,
                    "moking %s frame %d nerve %d step %d awake %d speed %.2f pos %.1f %.1f %.1f front %.2f %.2f\n",
                    o.id.c_str(), w.researchFrame, int(nerve), step, int(awake), speed, body.pos.x, body.pos.y,
                    body.pos.z, body.front.x, body.front.z);
        }
    }

    void nerveTick(World& w, Object& o) {
        switch (nerve) {
        case Nerve::Fly: {
            if (step == 0) {
                speed = 0;
                body.stop();
                if (offColliderAtRail) {
                    setCollider(true);
                }
            }
            const char* clip = travel().y >= 0.98f ? "FlyUp" : "Fly";
            if (o.action != clip) {
                o.startAction(clip);
            }
            if (!awake) {
                setNerve(Nerve::Hover);
                return;
            }
            speed = std::min((speed + kAccel) * kDamping, kMaxSpeed);
            if (rail.closed) {   // 0x2abd0c
                s = rail.wrap(s + (forward ? speed : -speed));
                body.pos = railPoint();
            } else if (moveOpen()) {
                body.pos = railPoint();
                setNerve(Nerve::Turn);
                return;
            } else {
                body.pos = railPoint();
            }
            body.turnToward(travel(), kTurn);
            front = body.front;
            if (diveCheck(w)) {
                return;
            }
            break;
        }
        case Nerve::Turn: {
            if (step == 0) {
                speed = 0;
                turning = true;
                if (offColliderAtRail) {
                    setCollider(true);
                }
            }
            const V3 dir = forcedTurn ? captain_toad::RecoveredVec::scale(travel(), -1.0f) : travel();
            if (!body.turnToward(dir, kTurn)) {
                if (diveCheck(w)) {
                    return;
                }
                break;
            }
            if (forcedTurn) {
                forward = !forward;   // 0x2f757c
            }
            forcedTurn = turning = false;
            setNerve(awake ? Nerve::Fly : Nerve::Hover);
            return;
        }
        case Nerve::Hover:
            if (step == 0) {
                o.startAction("Fly");
                body.stop();
                if (offColliderAtRail) {
                    setCollider(true);
                }
            }
            if (awake) {
                setNerve(turning ? Nerve::Turn : Nerve::Fly);
                return;
            }
            break;
        case Nerve::Ready:
            if (step == 0) {
                body.stop();
            }
            break;
        case Nerve::AttackSign:
            if (step == 0) {
                o.startAction("AttackSign");
                body.stop();
                if (offColliderAtRail) {
                    setCollider(false);
                }
            }
            if (aims) {
                const float t = std::min(std::max(float(slideStep) / kSlideSteps, 0.0f), 1.0f);
                body.pos.x = (1 - t) * slideFrom.x + slideTo.x * t;
                body.pos.z = (1 - t) * slideFrom.z + slideTo.z * t;
                slideStep++;
            }
            if (o.isActionEnd()) {
                slideStep = 0;
                setNerve(Nerve::Attack);
                return;
            }
            break;
        case Nerve::Attack:
            if (step == 0) {
                o.startAction("Attack");
            }
            if (body.onGround()) {   // 0x3b5f2c
                setNerve(Nerve::AttackLand);
                return;
            }
            body.vel.y = std::min((body.vel.y - kDiveGravity) * kDiveDamping, kDiveMaxUp);
            break;
        case Nerve::AttackLand:
            if (step == 0) {
                o.startAction("AttackLand");
                w.reaction(o, "地面衝突");
                body.stop();
            }
            if (o.isActionEnd()) {
                setNerve(Nerve::ReturnRail);
                return;
            }
            break;
        case Nerve::ReturnRail: {
            if (step == 0) {
                o.startAction("ReturnRail");
                if (!lifted) {
                    body.pos.y += kLift;
                    lifted = true;
                }
            }
            V3 d = captain_toad::RecoveredVec::sub(railPoint(), body.pos);
            V3 n = d;
            if (captain_toad::RecoveredVec::dot(n, n) > 0) {
                captain_toad::RecoveredVec::normalize(n);
            }
            body.vel = captain_toad::RecoveredVec::scale(
                captain_toad::RecoveredVec::add(body.vel, captain_toad::RecoveredVec::scale(n, kReturnAccel)),
                kReturnDamping);
            const float v = captain_toad::RecoveredStageChase::length(body.vel);
            if (v > kReturnMax) {
                body.vel = captain_toad::RecoveredVec::scale(body.vel, kReturnMax / v);
            }
            if (std::fabs(body.vel.y) <= std::fabs(d.y) && captain_toad::RecoveredVec::dot(d, d) >= 1e-6f) {
                break;
            }
            speed = captain_toad::RecoveredStageChase::length(body.vel);
            if (aims) {
                slideFrom = railPoint();
            }
            slideStep = 0;
            lifted = false;
            setNerve(Nerve::ReturnRailEnd);
            return;
        }
        case Nerve::ReturnRailEnd:
            if (step == 0) {
                o.startAction("ReturnRailEnd");
                body.stop();
            }
            if (aims) {
                const float t = std::min(std::max(float(slideStep) / kSlideSteps, 0.0f), 1.0f);
                const V3 r = railPoint();
                body.pos.x = (1 - t) * slideFrom.x + r.x * t;
                body.pos.z = (1 - t) * slideFrom.z + r.z * t;
                slideStep++;
            }
            if (o.isActionEnd()) {
                aims = false;
                setNerve(!awake ? Nerve::Hover : turning ? Nerve::Turn : Nerve::Fly);
                return;
            }
            break;
        case Nerve::Blow:
            return;
        }
        step++;
    }
};

struct MokingLeader : Behaviour {
    enum class Nerve { Watch, Dive, Turn } nerve = Nerve::Watch;
    bool afterTurn = false;   // +0x8f
    std::vector<std::string> ids;
    std::vector<int> birds;   // object indices, resolved on the first tick

    explicit MokingLeader(Object& o) {
        auto it = o.links.find("LinkMoking");
        if (it != o.links.end()) {
            for (const LinkTarget& t : it->second) {
                ids.push_back(t.id);
            }
        }
    }

    template <typename F>
    void each(World& w, F f) {
        for (int i : birds) {
            if (auto* b = dynamic_cast<Moking*>(w.behaviours[size_t(i)].get())) {
                f(*b, w.objects[size_t(i)]);
            }
        }
    }
    // The first loop of 0x169574 / 0x169080: is a bird diving? Only the first keeps its aim.
    bool anyDiving(World& w) {
        bool found = false;
        each(w, [&](Moking& b, Object& o) {
            if (b.alive(o) && b.diving()) {
                if (found) {
                    b.aims = false;
                }
                found = true;
            }
        });
        return found;
    }
    bool anyTurning(World& w) {
        bool found = false;
        each(w, [&](Moking& b, Object& o) { found = found || (b.alive(o) && b.turningOnOpenRail()); });
        return found;
    }
    void startDive(World& w) {
        each(w, [&](Moking& b, Object& o) {
            b.awake = false;
            if (b.alive(o)) {
                b.startDive();
            }
        });
        nerve = Nerve::Dive;
    }
    void wake(World& w) {
        each(w, [&](Moking& b, Object&) { b.awake = true; });
    }

    void control(World& w, Object&) override {
        if (birds.empty()) {
            for (size_t i = 0; i < w.objects.size(); i++) {
                if (std::find(ids.begin(), ids.end(), w.objects[i].id) != ids.end()) {
                    birds.push_back(int(i));
                }
            }
        }
        switch (nerve) {
        case Nerve::Watch:   // 0x169574
            if (anyDiving(w)) {
                startDive(w);
            } else if (anyTurning(w)) {
                each(w, [&](Moking& b, Object& o) {
                    b.awake = false;
                    if (b.alive(o)) {
                        b.forceTurn();
                    }
                });
                afterTurn = true;
                nerve = Nerve::Turn;
            }
            break;
        case Nerve::Dive: {   // 0x418a5c
            bool diving = false;
            each(w, [&](Moking& b, Object& o) { diving = diving || (b.alive(o) && b.diving()); });
            if (diving) {
                break;
            }
            if (!afterTurn) {
                wake(w);
                nerve = Nerve::Watch;
            } else {
                nerve = Nerve::Turn;
            }
            break;
        }
        case Nerve::Turn:   // 0x169080
            if (anyTurning(w)) {
                if (anyDiving(w)) {
                    startDive(w);
                }
                break;
            }
            wake(w);
            afterTurn = false;
            nerve = Nerve::Watch;
            break;
        }
    }
};

inline const Register<Moking> kMoking{"Moking"};
inline const Register<MokingLeader> kMokingLeader{"MokingLeader"};

}   // namespace captain_toad::RecoveredStageMoking
