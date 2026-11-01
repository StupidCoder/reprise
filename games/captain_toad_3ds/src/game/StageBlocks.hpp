// =============================================================================================
// StageBlocks.hpp - blocks: the POW block, breakable bricks and hard blocks (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: a wall of bricks with a POW block in its middle. Tapping the POW block on
// the touch screen makes it jump and explode: the bricks around it burst into pieces and the POW
// block is gone, opening the way to what was behind the wall. A single brick can also be tapped
// away. The invincible pickaxe breaks bricks and hard blocks.
//
// Touch screen: the touch manager (0x18e084 / 0x18dc94) picks the nearest touch point
// (InitScreenPoint) along the touched ray and sends it the touch through the host's vtable slot
// +0x68 (0x2c0c1c; the touch check is 0x2f9930, message type tag 0x4fdea0). See World::screenTouch.
//
// BlockPow (creator 0x454380, vtable 0x4d008c, init 0x3c696c). Sensors Body (MapObj r70 y50) and
// Explosion (Eye, id 9, r0 y50).
//   Wait 0x40d9b0      "Wait".
//   touch 0x3c6858     in Wait (and with EnableKnockSwitch, only once that switch is on):
//                      "Reaction", the Body sensor is invalidated, SwitchKockOn turns on (sic,
//                      the parameter's name) -> Explode.
//   receiveMsg 0x3c63c4 in Wait: a block hit (the 0x2fb1f8 family; among them the explosion
//                      0x2f0a88, e.g. of a turnip bursting against the block; a thrown turnip's
//                      attack 0x2f2984 is not one, so the turnip flies on into the block and
//                      bursts) -> the hit reaction 壊れ (0x2ca1cc), "Reaction" ("ReactionHipDrop"
//                      for a hip drop), the Body sensor invalidated, SwitchKockOn on -> Explode.
//                      Player hits (0x2fb068 / 0x2c9f6c / 0x2c9af0) set +0xb0, which makes Gone
//                      play 爆発振動 instead of 爆発振動[タッチ]; the player's hits are not ported.
//   Explode 0x3c6570   step 0: the Explosion sensor becomes valid with radius 200 and the hit
//                      reaction 爆発 plays (爆発[水中] in a WaterArea, +0xad 0x2f9f50); every tick the
//                      radius grows by 30 while below 600 (underwater: 20, 900); at the end of
//                      "Reaction" -> Gone.
//   Gone 0x3c6250      step 0: the model is hidden (0x2f0880), hit reaction 爆発振動[タッチ]
//                      (爆発振動 after a block hit); the radius keeps growing; at the maximum the
//                      block is killed (IsAppearBlockEmpty: an empty block appears instead,
//                      0x40d9d4; not ported).
//   attackSensor 0x3c66a8: outside Wait, the Explosion sensor (id 9) overlapping a sensor that is
//                      not the player's sends the explosion (0x2ec0d4, tag 0x4fdcbc).
//   Observed (run touchB, research/inputs/touch-pow-a.tsv: a tap on the POW block at frame 902):
//   the three touch-manager messages reach the POW block at frame 903, the bricks receive the
//   explosion at frame 905 and burst at once; at frame 970 the POW block and the wall are gone.
//
// BlockBrickBreakable (creator 0x453744, vtable 0x4c8184, init 0x1cea00; model BlockBrick).
// Sensors Body (MapObj r70 y50), UpperPunch (Eye r50 y50, invalid until hit).
//   Wait 0x428e80      "Wait".
//   receiveMsg 0x1ce3e4: a block hit (0x2fb1f8 family: the explosion, the pickaxe 0x2fc6d0, ...)
//                      -> 壊れ and break (0x2ab7ac); a hit from the player's sensors (0x2fb1e0:
//                      kinds 1 and 2; not 0x2fc748 / 0x2ab8b8) while he is Mini (0x2ec288 == 1)
//                      only bumps the brick ("Reaction"; the bump's UpperPunch sensor and
//                      sendMsgNextBlock are not ported).
//   touch 0x1ce7a0     outside the break nerves: break (0x2ab7ac with 0).
//   break 0x2ab7ac     the model is hidden and the collision invalidated; with IsAppearBreakModel
//                      the break model "<model>Break" (made at init) appears where the brick is
//                      and plays "Break"; -> Broken 0x428ea4: at step 5 the brick is killed.
//   sendMsgNextBlock and the hip-drop / upper-punch bumps (0x428ec8, 0x428f08, 0x428f2c) are not
//   ported.
//
// BlockHard (creator 0x4545c0, vtable 0x4d0b78, init 0x3d51d0; model BlockHard or, with
// IsGhostPlayerModel, BlockHardMaze). Sensor Body (MapObj r200 y105). The init makes the pieces
// BlockHardBreak with the action "Break".
//   receiveMsg 0x3d4d1c: only to the Body sensor (id 5), not from a player's Eye (id 7); the
//                      attacker's sphere must lie in the box 0x262680 (+ (0, 120, 0), half size 210
//                      on each axis; for 0x2ee3f4: + (0, 100, 0), half 115), and the message must
//                      be one of 0x2ced8c / 0x2fc6d0 (the pickaxe) / 0x2ceb44 / 0x2656f0 / 0x2f0b78
//                      / 0x2cea54 / 0x2ee3f4 -> vt+0x18 (0x3d53cc: SwitchBreakOn, the kill), 壊れ,
//                      the pieces appear (0x265490; turned by a random angle from
//                      the shared generator, BlockBrickBreak::appear). The turnip's explosion
//                      skips hard blocks (StageRadish.hpp).
//
// BlockBrickBreak: the break model (the exporter's object "<brick>/break", spawned_by the brick).
// Hidden until the brick breaks; then plays "Break" and disappears at its end (hypothesis: the
// owner of the break model's lifetime after the brick dies is not decoded).
// =============================================================================================
#pragma once

#include "game/StageBirds.hpp"
#include "game/StageObjects.hpp"

#include <cmath>

namespace captain_toad::RecoveredStageBlocks {

using namespace captain_toad::RecoveredStage;

// Blocks show the colour animation named after the stage, held at frame 0, on all their models
// (brick init 0x1cebd4 / POW init 0x3c6aac: 0x2fb538 with the stage name, when the archive has it;
// run loadC: every brick and its break model at frame 951).
inline void stageTint(World& w, Object& o) {
    o.setColorFrame(w.stageName, 0.0f);
}

struct BlockBrickBreak : Behaviour {
    bool shown = false;
    bool tinted = false;
    explicit BlockBrickBreak(Object& o) { o.visible = false; }
    // The break (0x2ab7ac for bricks, 0x265490 for hard blocks): the pieces appear, turned about
    // their Y axis by a random angle, (unit() x 360) degrees from the shared game generator
    // (0x4f8064, 0x2f934c) added to their rotation (0x2ab5a4: fmod(y + angle + 360, 360)).
    void appear(Object& o) {
        shown = true;
        o.visible = true;
        const float a = captain_toad::RecoveredStageBirds::birdRng().unit() * 360.0f * float(M_PI / 180.0);
        const float c = std::cos(a), s = std::sin(a);
        auto& m = o.world.m;
        for (int r = 0; r < 3; r++) {   // world rotation x RotY(a)
            const float x = m[r * 4 + 0], z = m[r * 4 + 2];
            m[r * 4 + 0] = x * c - z * s;
            m[r * 4 + 2] = x * s + z * c;
        }
        o.startAction("Break");
    }
    void control(World& w, Object& o) override {
        if (!tinted) {
            stageTint(w, o);
            tinted = true;
        }
        if (shown && o.isActionEnd()) {
            o.alive = false;
        }
    }
};

inline const Register<BlockBrickBreak> kBlockBrickBreak{"BlockBrickBreak"};

struct BlockBrickBreakable : Behaviour {
    enum class Nerve { Wait, Broken } nerve = Nerve::Wait;
    int step = 0;
    int pieces = -2;   // the break model's object (-1 none, -2 not looked up)

    explicit BlockBrickBreakable(Object&) {}
    void control(World& w, Object& o) override {
        if (nerve == Nerve::Wait && step == 0) {
            stageTint(w, o);
        }
        switch (nerve) {
        case Nerve::Wait:
            if (step == 0) {
                o.startAction("Wait");
            }
            break;
        case Nerve::Broken:   // 0x428ea4
            if (step == 5) {
                o.alive = false;
                return;
            }
            break;
        }
        step++;
    }
    void breakApart(World& w, Object& o) {   // 0x2ab7ac
        o.visible = false;
        o.collision = false;
        if (pieces == -2) {
            pieces = -1;
            for (size_t i = 0; i < w.objects.size(); i++) {
                if (w.objects[i].string("SpawnedBy", "") == o.id && w.objects[i].cls == "BlockBrickBreak") {
                    pieces = int(i);
                }
            }
        }
        if (pieces >= 0) {
            if (auto* b = dynamic_cast<BlockBrickBreak*>(w.behaviours[size_t(pieces)].get())) {
                b->appear(w.objects[size_t(pieces)]);
            }
        }
        nerve = Nerve::Broken;
        step = 0;
    }
    bool screenTouch(World& w, Object& o) override {   // 0x1ce7a0
        if (nerve != Nerve::Wait) {
            return false;
        }
        breakApart(w, o);
        return true;
    }
    bool explosion(World& w, Object& o) override {   // receiveMsg 0x1ce3e4, a hit not by the player
        if (nerve != Nerve::Wait) {
            return false;
        }
        breakApart(w, o);
        return true;
    }
    // receiveMsg 0x1ce3e4 with the pickaxe (0x2fc6d0): a hit by the player, so a Mini player
    // (0x2ec288 == 1) only bumps the brick ("Reaction"); otherwise 壊れ and the break.
    bool pickaxeHit(World& w, Object& o, const Sphere&, bool attackerMini) override {
        if (nerve != Nerve::Wait) {
            return false;
        }
        if (attackerMini) {
            o.startAction("Reaction");
            return true;
        }
        w.reaction(o, "壊れ");
        breakApart(w, o);
        return true;
    }
};

inline const Register<BlockBrickBreakable> kBlockBrickBreakable{"BlockBrickBreakable"};

// The hard block's pieces (BlockHardBreak, made by the init 0x3d51d0 with the action "Break"):
// the same lifetime as a brick's pieces.
struct BlockHardBreak : BlockBrickBreak {
    using BlockBrickBreak::BlockBrickBreak;
};
inline const Register<BlockHardBreak> kBlockHardBreak{"BlockHardBreak"};

struct BlockHard : Behaviour {
    bool broken = false;
    int pieces = -2;
    explicit BlockHard(Object&) {}
    // receiveMsg 0x3d4d1c for the pickaxe (0x2fc6d0): the attacking sphere must lie in the box
    // 0x262680 (the block's position + (0, 120, 0), half size 210 on each axis, grown by the
    // sphere's radius; constants 0x500bfc / 0x500c08, read at run time). Then vt+0x18 (0x3d53cc:
    // SwitchBreakOn and the kill), 壊れ and 0x265490 (the pieces appear and play "Break").
    bool pickaxeHit(World& w, Object& o, const Sphere& at, bool) override {
        if (broken) {
            return false;
        }
        const V3 p = captain_toad::RecoveredVec::sub(
            at.centre, captain_toad::RecoveredVec::add(o.translation(), V3{0.0f, 120.0f, 0.0f}));
        const float h = 210.0f + at.radius;
        if (std::fabs(p.x) > h || std::fabs(p.y) > h || std::fabs(p.z) > h) {
            return false;
        }
        broken = true;
        w.turnOn(o, "SwitchBreakOn");
        o.visible = false;
        o.collision = false;
        o.alive = false;
        w.reaction(o, "壊れ");
        if (pieces == -2) {
            pieces = -1;
            for (size_t i = 0; i < w.objects.size(); i++) {
                if (w.objects[i].string("SpawnedBy", "") == o.id && w.objects[i].cls == "BlockHardBreak") {
                    pieces = int(i);
                }
            }
        }
        if (pieces >= 0) {
            if (auto* b = dynamic_cast<BlockBrickBreak*>(w.behaviours[size_t(pieces)].get())) {
                b->appear(w.objects[size_t(pieces)]);
            }
        }
        return true;
    }
};
inline const Register<BlockHard> kBlockHard{"BlockHard"};

struct BlockPow : Behaviour {
    enum class Nerve { Wait, Explode, Gone } nerve = Nerve::Wait;
    int step = 0;
    float radius = 0;          // the Explosion sensor's radius
    bool underwater = false;   // +0xad: in a WaterArea (no water areas in the ported courses)

    explicit BlockPow(Object&) {}
    float maxRadius() const { return underwater ? 900.0f : 600.0f; }
    float growth() const { return underwater ? 20.0f : 30.0f; }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    void control(World& w, Object& o) override {
        switch (nerve) {
        case Nerve::Wait:   // 0x40d9b0
            if (step == 0) {
                stageTint(w, o);
                o.startAction("Wait");
            }
            break;
        case Nerve::Explode:   // 0x3c6570
            if (step == 0) {
                radius = 200.0f;
                w.reaction(o, underwater ? "爆発[水中]" : "爆発");
            }
            if (radius < maxRadius()) {
                radius += growth();
            }
            if (o.isActionEnd()) {
                setNerve(Nerve::Gone);
                attack(w, o);
                return;
            }
            break;
        case Nerve::Gone:   // 0x3c6250
            if (step == 0) {
                o.visible = false;
                o.collision = false;
                w.reaction(o, "爆発振動[タッチ]");
            }
            if (radius >= maxRadius()) {
                o.alive = false;
                return;
            }
            radius += growth();
            break;
        }
        attack(w, o);
        step++;
    }
    // attackSensor 0x3c66a8: the explosion against every other object's sensors (the player's
    // sensors are not objects here).
    void attack(World& w, Object& o) {
        if (nerve == Nerve::Wait) {
            return;
        }
        const SensorDecl* ex = o.sensor("Explosion");
        const V3 c = captain_toad::RecoveredVec::add(o.translation(),
                                                     captain_toad::RecoveredVec::scale(o.up(), ex ? ex->y : 50.0f));
        for (size_t i = 0; i < w.objects.size(); i++) {
            Object& t = w.objects[i];
            if (&t == &o || !t.alive || !w.behaviours[i]) {
                continue;
            }
            for (const SensorDecl& s : t.sensors) {
                if (s.type == "Eye" || s.type == "CollisionParts") {   // invalid or not contact sensors
                    continue;
                }
                const V3 d = captain_toad::RecoveredVec::sub(World::sensorCentre(t, s), c);
                const float r = radius + s.radius;
                if (captain_toad::RecoveredVec::dot(d, d) < r * r) {
                    w.behaviours[i]->explosion(w, t);
                    break;
                }
            }
        }
    }
    bool explosion(World& w, Object& o) override {   // receiveMsg 0x3c63c4 (0x2f0a88)
        if (nerve != Nerve::Wait) {
            return false;
        }
        w.reaction(o, "壊れ");
        o.startAction("Reaction");
        w.turnOn(o, "SwitchKockOn");
        setNerve(Nerve::Explode);
        return true;
    }
    bool screenTouch(World& w, Object& o) override {   // 0x3c6858
        if (o.link("EnableKnockSwitch") && !w.switchOn(o, "EnableKnockSwitch")) {
            return false;
        }
        if (nerve != Nerve::Wait) {
            return false;
        }
        o.startAction("Reaction");
        w.turnOn(o, "SwitchKockOn");
        setNerve(Nerve::Explode);
        return true;
    }
};

inline const Register<BlockPow> kBlockPow{"BlockPow"};

}   // namespace captain_toad::RecoveredStageBlocks
