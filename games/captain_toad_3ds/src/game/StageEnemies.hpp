// =============================================================================================
// StageEnemies.hpp - enemies of a course (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: a masked enemy walks back and forth along a fixed path, stops at each
// end, turns round and walks back. When it spots the protagonist it chases him (StageEnemyChase.hpp)
// and walks back to its path when it loses him. Touching it costs the protagonist a life.
//
// HeyhoRail (creator 0x454018, vtable 0x4cee64, init 0x3a7a8c; archive "Heyho"): the rail
//   variant of the enemy. Sensors Body (EnemyBody, r80 y75) and Attack (EnemyAttack, r40 y80).
//   The init's rail branch (0x3a7a8c: 0x2f6758 the rail direction as its front, 0x2abd0c /
//   0x3117d4 the rail attach) starts Wait at the rail's first point (traced: x 1050 at its first
//   update, run hc7, while its placement is at 1250); with IsColliderOffAtRail it does not collide
//   with the ground while on the rail.
//   Wait 0x409444  "Wait"; after step 44 -> Turn. Spotting the player (0x2c02f0) -> Find.
//   Turn 0x3a6f64  "Turn"; turns toward the rail direction (reversed when walking backwards) by at
//                  most 6 degrees a tick (0x2c7edc); when facing it -> Walk. Spotted -> Find.
//   Walk 0x3a70b4  "Walk"; moves 6 a tick along the rail (0x269a10 / 0x2abd0c); at every rail
//                  point (the rest of the way within 0.001 .. 6: onto the point) -> Wait, then Turn
//                  toward the next segment. An open rail is walked back from its ends; a closed
//                  one (HeyhoGardenStage's squares) goes round. Spotted -> Find.
//   Find 0x3a7628  leaves the rail (0x26d5d0 with 0: the collider on) and runs the chase component
//                  (actor +0x94, started by the nerve's enter 0x3a7ee0); when it ends -> RailWait.
//   RailWait 0x409888 "Wait"; back on the rail (0x26d5d0 with 1); spotted -> Find; after 5 steps
//                  -> Turn.
//   Attack 0x409320 "Attack"; stops and turns to the player by 6 degrees a tick; at the clip's end
//                  the chase resumes (component +0x28) -> Find.
//   Traced (run hc4, the pickaxe route from frame 1190): Find (component Find, then Chase at frame
//   39), Attack at frame 73 (the hit of the route at 1263), Find again at 209, knocked away by the
//   pickaxe at 263.
//   Its texture pattern "Color" is held at frame 0 for an enemy with a rail (init 0x3a7d1c; the
//   other branch, frame 1, is taken without one: observed +0xe4 = 1 in run front-input-a).
//   attackSensor 0x3a71fc: the Attack sensor reaching the player sends the enemy attack (0x2f0914)
//   unless it is attacking already; when he takes it (his reply) the chase's wide sight ends
//   (0x269a64) -> Attack. The player's hit reaction sets the damage flag his Miss transition reads.
//   A thrown turnip knocks it away (thrownHit / blowTick: receiveMsg 0x3a68a0, 0x2f5220, the
//   knock-away component 0x4c7764, nerve 0x4093e8): it flies off, lands, vanishes with the
//   death effect and leaves a coin. Its other hits (press, direct attack, light) are not ported.
// =============================================================================================
#pragma once

#include "game/StageEnemyChase.hpp"
#include "game/Push.hpp"
#include "game/StageObjects.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace captain_toad::RecoveredStageEnemies {

using namespace captain_toad::RecoveredStage;

struct HeyhoRail : Behaviour {
    enum class Nerve { Wait, Turn, Walk, Find, RailWait, Attack, Blow } nerve = Nerve::Wait;
    // The knock-away parameters (0x4ff6b0, built by the static constructor 0x44fe9c through
    // 0x259b50): action "BlowDown", 25 out, 55 up, damping 0.96, gravity 1.75 x 2, minimum
    // steps 20 x 0.5.
    static constexpr float kBlowOut = 25.0f, kBlowUp = 55.0f, kBlowDamp = 0.96f, kBlowGravity = 3.5f;
    static constexpr int kBlowMinSteps = 10;
    using Body = captain_toad::RecoveredStageChase::Body;
    int step = 0;
    captain_toad::RecoveredStageChase::RailPath rail;
    captain_toad::RecoveredStageChase::Chaser chase;
    float s = 0;   // distance along the rail
    bool forward = true;
    bool onRail = true;   // IsColliderOffAtRail: no ground collision while on the rail
    Body body;

    explicit HeyhoRail(Object& o) {
        if (const LinkTarget* r = o.link("Rail")) {
            rail = captain_toad::RecoveredStageChase::RailPath(r->points, r->closed);
        }
        chase = captain_toad::RecoveredStageChase::Chaser(o, o.link("ChaseArea"));
        body.pos = o.translation();
        body.front = {o.placement.m[2], 0, o.placement.m[10]};
        captain_toad::RecoveredVec::normalize(body.front);
        if (!rail.empty()) {   // the rail branch of the init: at the rail's start, facing along it
            s = 0;
            body.pos = rail.at(0);
            body.front = rail.tangent(0);
        }
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
    void find() {   // -> 0x4f4c78; its enter 0x3a7ee0 starts the chase component (0x190990)
        setNerve(Nerve::Find);
        chase.start(body, rail.empty() ? nullptr : &rail);
    }
    // 0x26d5d0 with 1 / 0x2c0204's 0x3200bc: back on the rail at the nearest point.
    void backOnRail() {
        onRail = true;
        if (!rail.empty()) {
            s = rail.nearest(body.pos);
            body.pos = rail.at(s);
        }
        body.stop();
    }

    // receiveMsg 0x3a68a0: a thrown object's attack (0x2f2aec includes the 0x2f2984 test) is
    // accepted unless already knocked away: the item factor of the hit (0x2f55e8), the knock-away
    // start (0x2f5220: hit reaction 吹き飛びヒット by the message, 0x2f4408; velocity: the
    // horizontal direction from the attacker times 25, 55 up; facing back toward the attacker),
    // nerve 0x4093e8 with the knock-away component (vtable 0x4c7764).
    bool thrownHit(World& w, Object& o, const V3& from, bool direct) override {
        if (nerve == Nerve::Blow) {
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
        w.reaction(o, direct ? "吹き飛びヒット[直接ヒット]" : "吹き飛びヒット");   // by the message (0x2f4408)
        setNerve(Nerve::Blow);
        return true;
    }
    // The component's flight nerve 0x1c5ef8: "BlowDown"; in the air gravity and damping
    // (0x2fa2bc, 0x2fa28c), on the ground the velocity stops (0x2f9718); done when the action
    // ends or, past the minimum steps, against a wall. Then nerve 0x4093e8 ends the enemy: the
    // death (0x2f4060, reaction 死亡 with the effect Die), the item with timing 死亡 (0x2fa17c:
    // InitItem コインx1[自動取得] for an indirect attack) and the kill.
    void blowTick(World& w, Object& o) {
        if (step == 0) {
            o.startAction("BlowDown");
            body.followWithoutCollision();   // collision on, the collider reset (0x2f9fec)
        }
        const bool ground = body.onGround();   // 0x2f9bf4 with 0 (0x1c5f24)
        V3& vel = body.vel;
        if (!ground) {
            vel.y -= kBlowGravity;
            vel = captain_toad::RecoveredVec::scale(vel, kBlowDamp);
        } else {
            vel = {0, 0, 0};
        }
        // Done (0x1c5ef8, in the nerve before this tick's movement): the action has ended
        // (0x2fa1b8), or past the minimum steps against a wall (0x2c05e4: has a collider, 0x2f14d4:
        // wall contact of the last movement). On the ground it only stops. (Traced: the original
        // stops at its position of 1487.)
        if ((o.isActionEnd() && o.hasClip("BlowDown")) || (step > kBlowMinSteps && body.collider.wall().present())) {
            w.reaction(o, "死亡");
            w.counters.coins += 1;
            o.visible = false;
            o.alive = false;
            nerve = Nerve::Wait;
            return;
        }
        body.integrate(w);
        place(o);
        step++;
    }

    // attackSensor 0x3a71fc: the Attack sensor against the player's contact sensors. The sensor
    // phase finds the overlapping pairs at the end of a tick and delivers them at the start of the
    // next, before the enemy's update (traced, route pullup-pickaxe-a: the contact from the
    // positions of 1261 is delivered at 1263, where the push is sent and Attack runs).
    // The push (0x2ef700): attackSensor sends it for its Attack sensor (type 7, 0x2f404c) against
    // the player's contact sensors (Foot, Body, Head: kind 1, 0x2fb1e0), before the attack, and
    // between two EnemyBody sensors (type 6) only. The player takes it (receiveMsg 0x1574dc
    // returns 1; traced for all 17 sends of that route, frames 1263-1439): his branch 0x157e80
    // hands it to 0x2db770 with strength 20, which pushes him away from the enemy's sensor with
    // the current positions (horizontal, at most 20, none once apart; Push.hpp) through his
    // collider's push accumulator (no push back on the enemy: its Attack sensor is no player
    // sensor, 0x2fb1e0). Then the enemy, when it moves toward his sensor, loses that part of its
    // velocity: it presses against him instead of running through him.
    std::vector<int> contacts;   // the player sensors the Attack sensor overlapped at the last tick's end

    void recordContacts(const World& w, const Object& o) {
        contacts.clear();
        const SensorDecl* atk = o.sensor("Attack");
        if (!atk) {
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

    void deliverContacts(World& w, const Object& o) {
        const SensorDecl* atk = o.sensor("Attack");
        if (!atk || contacts.empty() || nerve == Nerve::Blow) {
            return;
        }
        const V3 c = captain_toad::RecoveredVec::add(body.pos, V3{0, atk->y, 0});
        const auto& sensors = playerSensors(w.player.mini);
        for (int k : contacts) {   // 0x2ef700 per pair
            const V3 pc = captain_toad::RecoveredVec::add(
                w.player.pos, captain_toad::RecoveredVec::scale(w.player.up, sensors[k].up));
            V3 push;
            if (captain_toad::RecoveredPush::push(pc, sensors[k].radius, c, atk->radius,
                                                  captain_toad::RecoveredVec::scale(w.player.up, -1.0f), 20.0f, push)) {
                w.playerPushes.push_back(push);   // 0x2db770
            }
            V3 d = captain_toad::RecoveredVec::sub(pc, c);
            if (captain_toad::RecoveredVec::dot(d, d) < 1e-6f) {
                d = {0, 0, 1};   // 0x550188: the unit Z constant (in memory: X 0x550170, Y 0x55017c, Z 0x550188)
            } else {
                captain_toad::RecoveredVec::normalize(d);
            }
            const float toward = captain_toad::RecoveredVec::dot(body.vel, d);
            if (toward > 0) {
                body.vel = captain_toad::RecoveredVec::sub(body.vel, captain_toad::RecoveredVec::scale(d, toward));
            }
        }
        // The attack (0x2f0914): the player's reply decides; Attack then runs in this tick.
        if (nerve != Nerve::Attack) {
            w.playerAttacked = true;
            if (w.playerDamageable) {
                chase.sight.wide = false;   // 0x269a64
                setNerve(Nerve::Attack);
            }
        }
    }

    bool patterned = false;
    void control(World& w, Object& o) override {
        if (!patterned) {
            o.setPatternFrame("Color", rail.empty() ? 1.0f : 0.0f);
            patterned = true;
        }
        if (nerve == Nerve::Blow) {
            blowTick(w, o);
            if (getenv("DIORAMA_CHASE_LOG")) {
                fprintf(stderr, "chase %s frame %d blow step %d pos %.1f %.1f %.1f\n", o.id.c_str(), w.researchFrame,
                        step, body.pos.x, body.pos.y, body.pos.z);
            }
            return;
        }
        deliverContacts(w, o);
        nerveTick(w, o);
        if (!onRail) {
            body.integrate(w);
        } else {
            body.followWithoutCollision();   // IsColliderOffAtRail: the collider is off on the rail
        }
        place(o);
        recordContacts(w, o);
        if (getenv("DIORAMA_CHASE_LOG")) {   // research: nerve and position per tick
            fprintf(stderr,
                    "chase %s frame %d nerve %d/%d step %d pos %.1f %.1f %.1f ground %d sight %d%d%d player %.0f %.0f "
                    "%.0f\n",
                    o.id.c_str(), w.researchFrame, int(nerve), int(chase.nerve()), step, body.pos.x, body.pos.y,
                    body.pos.z, int(body.onGround()), int(chase.sight.target), int(chase.sight.inView),
                    int(chase.sight.visible), w.player.pos.x, w.player.pos.y, w.player.pos.z);
        }
    }

    void nerveTick(World& w, Object& o) {
        const captain_toad::RecoveredStageChase::RailPath* r = rail.empty() ? nullptr : &rail;
        switch (nerve) {
        case Nerve::Blow:   // handled in control
            return;
        case Nerve::Wait:
            if (step == 0) {
                o.startAction("Wait");
            }
            if (chase.spotted(w, body)) {
                find();
                return;
            }
            if (step > 0x2c) {
                setNerve(Nerve::Turn);
                return;
            }
            break;
        case Nerve::Turn: {
            if (step == 0) {
                o.startAction("Turn");
            }
            if (chase.spotted(w, body)) {
                find();
                return;
            }
            if (body.turnToward(rail.heading(s, forward), 6.0f)) {
                setNerve(Nerve::Walk);
                return;
            }
            break;
        }
        case Nerve::Walk:
            if (step == 0) {
                o.startAction("Walk");
            }
            if (chase.spotted(w, body)) {
                find();
                return;
            }
            {
                // 0x269a10: the distance to the next rail point; within one step (0.001 .. 6) the enemy
                // steps onto the point and waits there (0x2abd0c, -> Wait 0x4f4c7c), else it moves 6.
                // An open rail is walked back from its ends; a closed one goes round.
                const float rest = rail.toNextPoint(s, forward);
                if (rest >= 0.001f && rest <= 6.0f) {
                    s = rail.wrap(s + (forward ? rest : -rest));
                    body.pos = rail.at(s);
                    if (!rail.closed && (s >= rail.total - 1e-3f || s <= 1e-3f)) {
                        forward = !forward;
                    }
                    setNerve(Nerve::Wait);
                    return;
                }
                s = rail.wrap(s + (forward ? 6.0f : -6.0f));
                body.pos = rail.at(s);
                break;
            }
        case Nerve::Find:   // 0x3a7628
            if (step == 0) {
                onRail = false;   // 0x26d5d0 with 0
                // 0x3a7628 at step 0 with IsColliderOffAtRail: collision on and the collider reset
                // (0x2f9fec: off the ground until the next move; traced: the restart after the
                // attack falls one tick at 1401 before the run accelerates).
                body.followWithoutCollision();
            }
            chase.tick(w, o, body, r);
            if (chase.ended) {
                backOnRail();
                setNerve(Nerve::RailWait);
                return;
            }
            break;
        case Nerve::RailWait:   // 0x409888
            if (step == 0) {
                o.startAction("Wait");
                backOnRail();
            }
            if (chase.spotted(w, body)) {
                find();
                return;
            }
            if (step >= 5) {
                setNerve(Nerve::Turn);
                return;
            }
            break;
        case Nerve::Attack:   // 0x409320
            if (step == 0) {
                o.startAction("Attack");
                body.stop();
            }
            body.turnToward(captain_toad::RecoveredVec::sub(w.player.pos, body.pos), 6.0f);
            if (step > 0 && o.isActionEnd()) {
                chase.resume = true;
                find();
                return;
            }
            break;
        }
        step++;
    }
};

inline const Register<HeyhoRail> kHeyhoRail{"HeyhoRail"};

}   // namespace captain_toad::RecoveredStageEnemies
