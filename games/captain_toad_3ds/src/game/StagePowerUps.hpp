// =============================================================================================
// StagePowerUps.hpp - the mushroom and the pickaxe a plant brings out (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: after three pulls a mushroom (or a pickaxe) pops up out of the plant and
// stays there; touching it collects it. A mushroom at full power is worth coins; the pickaxe
// makes the protagonist invincible for a while.
//
// Both are pooled items the item generator 0x3e5c1c hands out (PullOutObj InitItem: スーパーキノコ
// -> スーパーキノコ[真上出現], 無敵ツルハシ -> 無敵ツルハシ[真上出現]) at the item position (the
// plant + up x 100). They update in the item category (InitExecutor アイテム), which runs every
// 60 Hz frame: two steps per game tick (the CountUp coin dies at step 19 and run sfxA shows its
// Appear and CoinGet 18 frames apart).
//
// KinokoSuper (vtable 0x4bdfe4, init 0x155800, receiveMsg 0x154ffc; archive KinokoSuper):
//   start 0x155490 ([真上出現]): shown, "PopUp", flags +0xa8 / +0xa9 / +0xb2 clear (no timeout,
//   not running), nerve PopUp.
//   PopUp 0x4164a8   "PopUp"; at step 30 the sensors become valid (0x2dd9bc); at the clip's end
//                    -> Wait (not running; the running variants go to Run 0x154eb0).
//   Wait 0x41643c    "Wait".
//   receiveMsg: the player's touch (0x2fd99c) collects it: the item factor and HrStockGot; at
//   full power (0x2dd50c: the player's power state, +0xd0 -> +0x44 -> [0], is 0 = Super) the
//   generator's give (0x3e87fc) of コインx10[自動取得] (0x260364 -> 0x198fbc adds 10) and the item
//   timing スーパー状態取得 (InitItem: コインx10[自動取得] again, the generator's ten coins, each
//   given as コインx1: +10 more; code-derived, not traced); otherwise (Mini) the timing 取得. Then
//   the give of スーパーキノコ (0x3369d0 -> 0x336f18: the player's power-up event) and the kill.
// PickaxeItem (vtable 0x4be2ac, init 0x1570cc, receiveMsg 0x156cc8; archive PickaxeItem):
//   start 0x156f0c ([真上出現]): shown, "PopUp", nerve PopUp.
//   PopUp 0x416c1c   at step 40 or the clip's end the sensors become valid -> Wait 0x416bd4
//                    ("Wait").
//   receiveMsg: a player's touch: the item factor, the timing 取得, 0x2dc424 (the player's
//   invincible request through his power-up state, +0xd0 -> +0x5c, 0x2a2ee8 with 1) and the
//   kill.
// Not ported: water and death areas (0x2f9da0, 0x2f9ca0), the ten coin actors and the "+10"
// display (0x2dd43c), the score, the running / falling variants.
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

namespace captain_toad::RecoveredStagePowerUps {

using namespace captain_toad::RecoveredStage;

// One popping item: PopUp until its clip ends, then Wait; collectable once its sensors are valid.
struct PopItem : Behaviour {
    enum class Nerve { Hidden, PopUp, Wait } nerve = Nerve::Hidden;
    int step = 0;
    bool sensorsValid = false;
    int validStep = 30;

    explicit PopItem(Object& o) {
        o.visible = false;
        o.collision = false;
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    long startTick = -1;        // the tick of the start: PopUp runs from the next one
    bool facesCamera = false;   // the pickaxe's start turns it toward the camera
    void appear(World& w, Object& o, const V3& at) {
        startTick = w.tickCount;
        o.world = o.placement;
        o.world.m[3] = at.x;
        o.world.m[7] = at.y;
        o.world.m[11] = at.z;
        if (facesCamera) {
            // 0x2dc3c4: front toward the camera position (0x2f73dc, the scene camera [0]) unless
            // it is there; 0x2f7be4 keeps up = (0, 1, 0): X = up x front, Z = X x up (a yaw).
            V3 f = captain_toad::RecoveredVec::sub(w.cameraEye, at);
            if (captain_toad::RecoveredVec::dot(f, f) > 1e-6f) {
                const V3 up{0, 1, 0};
                V3 x = captain_toad::RecoveredVec::cross(up, f);
                if (captain_toad::RecoveredVec::dot(x, x) > 1e-12f) {
                    captain_toad::RecoveredVec::normalize(x);
                    const V3 z = captain_toad::RecoveredVec::cross(x, up);
                    const float m[9] = {x.x, up.x, z.x, x.y, up.y, z.y, x.z, up.z, z.z};
                    for (int r = 0; r < 3; r++) {
                        for (int c = 0; c < 3; c++) {
                            o.world.m[r * 4 + c] = m[r * 3 + c];
                        }
                    }
                }
            }
        }
        o.visible = true;
        o.alive = true;
        sensorsValid = false;
        o.startAction("PopUp");
        setNerve(Nerve::PopUp);
    }
    void stepOnce(Object& o) {
        switch (nerve) {
        case Nerve::Hidden:
            return;
        case Nerve::PopUp:
            if (step == validStep) {
                sensorsValid = true;   // 0x2dd9bc
            }
            if (step > 0 && (o.isActionEnd() || !o.hasClip("PopUp"))) {
                sensorsValid = true;
                setNerve(Nerve::Wait);
                return;
            }
            break;
        case Nerve::Wait:
            if (step == 0) {
                o.startAction("Wait");
            }
            break;
        }
        step++;
    }
    void control(World& w, Object& o) override {
        if (startTick == w.tickCount) {
            return;   // the start (0x156f0c) sets the nerve; the keeper takes it at the next update
        }
        stepOnce(o);   // the item category: two steps per game tick
        stepOnce(o);
    }
    // The player's touch reaches the Body sensor only while it is valid; the sensor follows its
    // joint (InitSensor Joint KinokoSuper / PickaxeItem), which the PopUp clip moves.
    void contactSpheres(const Object& o, std::vector<Sphere>& out) const override {
        if (nerve == Nerve::Hidden || !sensorsValid) {
            return;
        }
        for (size_t k = 0; k < o.sensors.size(); k++) {
            const SensorDecl& s = o.sensors[k];
            if (s.type != "MapObj") {
                continue;
            }
            V3 c;
            if (!o.jointSensorCentre(s, c)) {
                c = World::sensorCentre(o, s);
            }
            out.push_back({c, s.radius, int(k)});
        }
    }
    void vanish(Object& o) {
        o.visible = false;
        o.alive = false;
        nerve = Nerve::Hidden;
    }
};

struct KinokoSuper : PopItem {
    explicit KinokoSuper(Object& o) : PopItem(o) { validStep = 30; }
    bool touch(World& w, Object& o, int) override {   // receiveMsg 0x154ffc
        if (!sensorsValid) {
            return false;
        }
        w.reaction(o, "取得");        // Get + HrStockGot
        if (w.playerPower == 0) {     // Super
            w.counters.coins += 10;   // give コインx10[自動取得]
            w.counters.coins += 10;   // the timing スーパー状態取得: ten coins, each コインx1
        } else {
            w.powerUpRequest = 0;   // the give of スーパーキノコ: back to Super (session)
        }
        vanish(o);
        return true;
    }
};

struct PickaxeItem : PopItem {
    explicit PickaxeItem(Object& o) : PopItem(o) {
        validStep = 40;
        facesCamera = true;   // start 0x156f0c
    }
    bool touch(World& w, Object& o, int) override {   // receiveMsg 0x156cc8
        if (!sensorsValid) {
            return false;
        }
        w.reaction(o, "取得");
        w.invincibleRequest = true;   // 0x2dc424 (session)
        vanish(o);
        return true;
    }
};

// The protagonist's pickaxe (WarpedMtxPartsModel "Pickaxe" on HandR): the session places and
// shows it in the invincible mode.
struct PlayerPickaxe : Behaviour {
    explicit PlayerPickaxe(Object& o) {
        o.visible = false;
        o.collision = false;
    }
};

inline const Register<KinokoSuper> kKinokoSuper{"KinokoSuper"};
inline const Register<PlayerPickaxe> kPlayerPickaxe{"PlayerPickaxe"};
inline const Register<PickaxeItem> kPickaxeItem{"PickaxeItem"};

}   // namespace captain_toad::RecoveredStagePowerUps
