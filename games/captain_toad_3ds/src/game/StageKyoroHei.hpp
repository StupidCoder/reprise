// =============================================================================================
// StageKyoroHei.hpp - the spiked stone block enemy (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: a tall stone block with spikes slides back and forth between two marks,
// a random distance each time, easing in and out, and pauses at each stop. Walking into it hurts.
//
// KyoroHei (creator 0x4544d0, vtable 0x4d0784, init 0x3cf3d0; archive KyoroHei):
//   Placement: the start point (+0xac) and MoveNext (+0xb8), the distance between them (+0xc8),
//   StartPosRate (clamped to [0, 1]; the block starts that far along, +0xc4 = rate x distance),
//   IsMovePlus (+0xd9: the next move goes towards MoveNext), IsNoHide (+0xda). +0xd8: the model's
//   X axis points along start -> MoveNext (dot >= 0); it picks the Left / Right clips.
//   BodyBox.byml (+0x94 min, +0xa0 max): the hurting box in the block's local frame.
//   Nerves (table 0x4f4ed0..0x4f4eec):
//     0x4f4ed0 HideWait 0x3cf0b0, 0x4f4ed4 Appear 0x40eab0, 0x4f4ed8 Move 0x3cf8e4,
//     0x4f4edc Stop 0x40e9e0, 0x4f4ee0 AttackSuccess 0x40eb80, 0x4f4ee4 Reaction 0x40eb18,
//     0x4f4ee8 (a reaction handler's wait) 0x40ebe8, 0x4f4eec Hide 0x40e984.
//   With IsNoHide (every KyoroHei of RevolverRuinsStage) the block appears at init and starts in
//   Move; the hide / appear nerves (the player more than 400 away) are not ported.
//   Move 0x3cf8e4, step 0: the clip MoveLeft (d9 == d8) or MoveRight; from = the position along
//   the line; a travel of 250 + unit x 550 (the shared generator 0x4f8064, 0x2f934c; literals
//   0x3cfbd0, 0x3cfbcc), negated unless IsMovePlus; target = clamp(from + travel, 0, distance);
//   duration = int(|target - from| x 0.05) steps. Every step: t = step / duration (1 without a
//   duration), clamped to [0, 1]; ease = (3 - 2t) t^2 clamped; along = (1 - ease) from + ease
//   target; the block's position = start + (MoveNext - start) x along / distance. When the step
//   reaches the duration: the clip StopLeft / StopRight (by the old direction), the direction
//   flips and the nerve becomes Stop.
//   Stop 0x40e9e0: when the clip has ended, Move again.
//   attackSensor 0x3cf244: a player sensor overlapping the Attack sensor (r300, 100 up) whose
//   centre lies inside BodyBox (in the block's frame) gets the attack (0x2f0914), unless the block
//   is hidden; when the player takes it the nerve becomes AttackSuccess 0x40eb80 ("AttackSuccess";
//   at its end the direction flips and Move starts again).
//   receiveMsg 0x3cead4: an attack (0x2f0bf0 / 0x2f29fc / 0x2fc6d0 families, the thrown turnip
//   among them) whose sensor centre lies in BodyBox (the block's frame), widened in X and Z by
//   1.3 x the attacker's radius (literals 0x3cefc4, 0x3cefc8, 0x3cefcc), kills the block: the
//   reply, the attacker's 無敵アタックヒット for the invincible family, the items of timing 死亡
//   (InitItem コインx3[自動取得], 0x2fa17c), the break model 壊れモデル appears (KyoroHeiBreak), the
//   reaction 死亡 200 above the base (0x2ee17c: effect Die, sound HrBreak) and the kill.
//   Not ported: the attacks that kill without the box test (0x2fc748 / 0x2fb418 / 0x2f0a10), the
//   three coins' pop-up visuals (counted only), the touch reaction (撫でる, a coin), the Needle
//   floor on its top, the pickaxe (the session tests only EnemyBody / MapObj sensors).
// =============================================================================================
#pragma once

#include "game/StageBirds.hpp"
#include "game/StageSubActors.hpp"
#include "game/StageObjects.hpp"

#include <algorithm>
#include <cmath>

namespace captain_toad::RecoveredStageKyoroHei {

using namespace captain_toad::RecoveredStage;

struct KyoroHei : Behaviour {
    enum class Nerve { Move, Stop, AttackSuccess } nerve = Nerve::Move;
    int step = 0;
    V3 start{}, next{};
    float distance = 0, along = 0, from = 0, target = 0;
    int duration = 0;
    bool plus = true;        // +0xd9
    bool sideAlong = true;   // +0xd8
    V3 boxMin{}, boxMax{};

    explicit KyoroHei(Object& o) {
        start = o.translation();
        next = start;
        if (const LinkTarget* t = o.link("MoveNext")) {
            next = t->translation();
        }
        const V3 d = captain_toad::RecoveredVec::sub(start, next);
        distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const V3 side{o.placement.m[0], o.placement.m[4], o.placement.m[8]};
        const V3 dir = captain_toad::RecoveredVec::sub(next, start);
        sideAlong = side.x * dir.x + side.y * dir.y + side.z * dir.z >= 0.0f;
        float rate = float(o.number("StartPosRate", 0));
        rate = rate < 0 ? 0 : (rate > 1 ? 1 : rate);
        along = rate * distance;
        place(o, rate);
        plus = o.number("IsMovePlus", 1) != 0;
        boxMin = {float(o.number("BodyBoxMinX", 0)), float(o.number("BodyBoxMinY", 0)),
                  float(o.number("BodyBoxMinZ", 0))};   // the archive's BodyBox.byml (exporter)
        boxMax = {float(o.number("BodyBoxMaxX", 0)), float(o.number("BodyBoxMaxY", 0)),
                  float(o.number("BodyBoxMaxZ", 0))};
    }

    void place(Object& o, float rate) const {
        o.world = o.placement;
        o.world.m[3] = start.x + (next.x - start.x) * rate;
        o.world.m[7] = start.y + (next.y - start.y) * rate;
        o.world.m[11] = start.z + (next.z - start.z) * rate;
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    const char* side(const char* left, const char* right) const { return plus == sideAlong ? left : right; }

    // attackSensor 0x3cf244 for the player's sensors (the overlap of the previous tick).
    void attack(World& w, const Object& o) {
        const SensorDecl* atk = o.sensor("Attack");
        if (!atk || nerve == Nerve::AttackSuccess) {
            return;
        }
        const auto& m = o.world.m;
        const V3 c{m[3] + m[1] * atk->y, m[7] + m[5] * atk->y, m[11] + m[9] * atk->y};
        for (const auto& ps : playerSensors(w.player.mini)) {
            const V3 pc =
                captain_toad::RecoveredVec::add(w.player.pos, captain_toad::RecoveredVec::scale(w.player.up, ps.up));
            const V3 d = captain_toad::RecoveredVec::sub(pc, c);
            const float r = ps.radius + atk->radius;
            if (captain_toad::RecoveredVec::dot(d, d) >= r * r) {
                continue;
            }
            // The sensor's centre in the block's frame (0x263c98: rotation transposed).
            const V3 q{pc.x - m[3], pc.y - m[7], pc.z - m[11]};
            const V3 l{m[0] * q.x + m[4] * q.y + m[8] * q.z, m[1] * q.x + m[5] * q.y + m[9] * q.z,
                       m[2] * q.x + m[6] * q.y + m[10] * q.z};
            if (l.x < boxMin.x || l.x > boxMax.x || l.y < boxMin.y || l.y > boxMax.y || l.z < boxMin.z ||
                l.z > boxMax.z) {
                continue;
            }
            w.playerAttacked = true;   // 0x2f0914: the player's reply decides
            if (w.playerDamageable) {
                setNerve(Nerve::AttackSuccess);
            }
            return;
        }
    }

    bool thrownHit(World& w, Object& o, const V3& from, bool direct) override {
        const Sphere a = w.attacker.radius > 0 ? w.attacker : Sphere{from, 0, 0};
        const auto& m = o.world.m;
        const V3 q{a.centre.x - m[3], a.centre.y - m[7], a.centre.z - m[11]};
        const V3 l{m[0] * q.x + m[4] * q.y + m[8] * q.z, m[1] * q.x + m[5] * q.y + m[9] * q.z,
                   m[2] * q.x + m[6] * q.y + m[10] * q.z};
        const float grow = a.radius * 1.3f;
        auto inside = [&](float v, float lo, float hi) {
            const float w2 = hi - lo, c = (lo + hi) * 0.5f;
            const float half = w2 != 0 ? w2 * ((w2 + grow * 2.0f) / w2) * 0.5f : 0.0f;
            return v > c - half && v < c + half;
        };
        if (!inside(l.x, boxMin.x, boxMax.x) || !(l.y > boxMin.y && l.y < boxMax.y) ||
            !inside(l.z, boxMin.z, boxMax.z)) {
            return false;
        }
        w.counters.coins += 3;                            // InitItem コインx3[自動取得] at 死亡 (the pop-ups not shown)
        for (size_t i = 0; i < w.objects.size(); i++) {   // 壊れモデル
            Object& sub = w.objects[i];
            if (sub.string("SpawnedBy", "") == o.id && sub.cls == "BreakModel") {
                if (auto* b = dynamic_cast<captain_toad::RecoveredStageSubActors::BreakModel*>(w.behaviours[i].get())) {
                    b->appear(sub, o);
                }
            }
        }
        const V3 at{m[3], m[7] + 200.0f, m[11]};   // literal 0x3cefc0
        w.reaction(o, "死亡", &at);
        o.alive = false;
        (void)direct;
        return true;
    }

    void control(World& w, Object& o) override {
        attack(w, o);
        switch (nerve) {
        case Nerve::Move: {   // 0x3cf8e4
            if (step == 0) {
                o.startAction(side("MoveLeft", "MoveRight"));
                from = along;
                float travel = 250.0f + captain_toad::RecoveredStageBirds::birdRng().unit() * 550.0f;
                if (!plus) {
                    travel = -travel;
                }
                const float to = along + travel;
                target = to < 0 ? 0.0f : (to > distance ? distance : to);
                const float span = std::fabs(target - from);
                duration = int(span * 0.05f);
                if (getenv("DIORAMA_DEBUG")) {
                    fprintf(stderr, "kyorohei %s tick %ld move %.1f -> %.1f in %d\n", o.id.c_str(), w.tickCount, from,
                            target, duration);
                }
            }
            float t = 1.0f;
            if (duration > 0) {
                t = float(step) / float(duration);
                t = t < 0 ? 0.0f : (t > 1 ? 1.0f : t);
            }
            float ease = (3.0f - t * 2.0f) * t * t;
            ease = ease < 0 ? 0.0f : (ease > 1 ? 1.0f : ease);
            along = (1.0f - ease) * from + target * ease;
            place(o, distance > 0 ? along / distance : 0.0f);
            if (step < duration) {
                step++;
                return;
            }
            o.startAction(side("StopLeft", "StopRight"));
            plus = !plus;
            setNerve(Nerve::Stop);
            return;
        }
        case Nerve::Stop:   // 0x40e9e0
            if (o.isActionEnd() && step > 0) {
                setNerve(Nerve::Move);
                return;
            }
            step++;
            return;
        case Nerve::AttackSuccess:   // 0x40eb80
            if (step == 0) {
                o.startAction("AttackSuccess");
            }
            if (o.isActionEnd() && step > 0) {
                plus = !plus;
                setNerve(Nerve::Move);
                return;
            }
            step++;
            return;
        }
    }
};

inline const Register<KyoroHei> kKyoroHei{"KyoroHei"};

}   // namespace captain_toad::RecoveredStageKyoroHei
