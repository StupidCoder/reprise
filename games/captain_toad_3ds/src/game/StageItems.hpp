// =============================================================================================
// StageItems.hpp - collectable items of a course (recovered behaviours, StageObjects.hpp)
// =============================================================================================
//
// What the player sees: walking into a coin makes it vanish with a sparkle and a chime, and the
// coin counter goes up.
//
// CoinStack (creator 0x454620, vtable 0x4d0d10; init 0x3d5ff0): a column of CoinStackNum coins
//   (at least 1). Each is a Coin actor (constructor 0x170214, variant "Coin Stack", vtable
//   0x4c03b8) built from the CoinStack archive, so its sensor is that archive's Body (radius 60,
//   30 above the coin). Coin i stands i * 80 along the stack's up (Coin init 0x16ff24 sets the
//   height +0x90 = 80; the stack places its coins in 0x3d5e9c, downwards when the stack is upside
//   down; memory of front-input-a: the second coin of a two-coin stack 80 above the first). The
//   stack's own receiveMsg 0x3d5cb8 (message tag 0x4fe0fc) re-places the remaining coins
//   (hypothesis: the coins above a taken one fall back, CoinStack clips Fall / Land); not
//   ported: a taken coin leaves a gap.
// Coin receiveMsg 0x16fd2c: while alive and not yet taken (+0x8f), any of six message families
//   (0x2f290c: the player touch 0x4fde08 among them; 0x30fecc, 0x2ce550, 0x2fb418, 0x2cec34,
//   0x2d0d90: other attackers, not recovered) is accepted: hit reaction "取得" (get: effect
//   CoinGet / CoinStack "Get", sound HrGet, ActorHitReactionCtrl.byml), kill (0x2fd968), taken.
//   Where the coin counter is increased is not traced yet; the port counts one coin per kill.
// Coin control 0x2ce5c8: the shared coin rotation (0x2922f4, all coins turn together); not ported
//   (the CoinStack model has no visible spin).
// =============================================================================================
#pragma once

#include "game/StageBirds.hpp"
#include "game/StageObjects.hpp"

#include <algorithm>

namespace captain_toad::RecoveredStageItems {

using namespace captain_toad::RecoveredStage;

struct CoinStack : Behaviour {
    static constexpr float kCoinHeight = 80;   // Coin +0x90 (Coin init 0x16ff24: 80.0 at 0x16ff64)
    std::vector<bool> taken;                   // +0x8f of each coin
    bool upsideDown = false;                   // +0xac: the stack's up points downwards (init 0x3d5ff0)

    // Each coin's placement (0x262418) moves its base by -5 + unit x 10 in world X, then in Z
    // (two draws of the shared generator 0x4f8064, 0x2f934c; literals 0x2624fc, 0x2624f8), so the
    // coins of a stack are not exactly above each other (memory, run revolver-start-a: obj496's
    // coins at (-2.05, -4.43) and (-3.62, -3.20) from the stack).
    std::vector<V3> jitter;

    explicit CoinStack(Object& o) {
        taken.assign(size_t(std::max(1, int(o.number("CoinStackNum", 1)))), false);
        upsideDown = o.placement.m[5] < 0;
        for (size_t i = 0; i < taken.size(); i++) {
            const float x = -5.0f + captain_toad::RecoveredStageBirds::birdRng().unit() * 10.0f;
            const float z = -5.0f + captain_toad::RecoveredStageBirds::birdRng().unit() * 10.0f;
            jitter.push_back({x, 0, z});
        }
        layout(o);
    }

    // Coin i along the stack's up (0x3d5e9c) plus its jitter; only the coins not taken are drawn.
    V3 coinOffset(const Object& o, size_t i) const {
        const float h = upsideDown ? -(float(i) * kCoinHeight) : float(i) * kCoinHeight;
        return captain_toad::RecoveredVec::add(captain_toad::RecoveredVec::scale(o.up(), h), jitter[i]);
    }
    void layout(Object& o) const {
        o.draws.clear();
        for (size_t i = 0; i < taken.size(); i++) {
            if (!taken[i]) {
                M34 m = o.placement;
                const V3 d = coinOffset(o, i);
                m.m[3] += d.x;
                m.m[7] += d.y;
                m.m[11] += d.z;
                o.draws.push_back(m);
            }
        }
    }

    // The coin's nerve (0x4f56d4, exec 0x419fb0) starts "Wait" on all its animation players at
    // step 0: the skeletal clip and the material clips (Wait.cmcla flashes the sparkle's alpha:
    // 1 -> 0 over 24 frames at a few points of the 600-frame loop).
    bool waiting = false;
    void control(World&, Object& o) override {
        if (!waiting) {
            o.startAction("Wait");
            waiting = true;
        }
    }

    void contactSpheres(const Object& o, std::vector<Sphere>& out) const override {
        const SensorDecl* body = o.sensor("Body");
        if (!body) {
            return;
        }
        for (size_t i = 0; i < taken.size(); i++) {
            if (!taken[i]) {
                const V3 c = captain_toad::RecoveredVec::add(World::sensorCentre(o, *body), coinOffset(o, i));
                out.push_back({c, body->radius, int(i)});
            }
        }
    }

    // Coin receiveMsg 0x16fd2c for the player's touch.
    bool touch(World& w, Object& o, int coin) override {
        if (coin < 0 || size_t(coin) >= taken.size() || taken[size_t(coin)]) {
            return false;
        }
        // Each coin of the stack is an actor of its own in the original: its reaction happens there.
        const V3 at = captain_toad::RecoveredVec::add(o.translation(), coinOffset(o, size_t(coin)));
        w.reaction(o, "取得", &at);
        taken[size_t(coin)] = true;
        w.counters.coins += 1;
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "coin %s/%d taken (coins %d)\n", o.id.c_str(), coin, w.counters.coins);
        }
        layout(o);
        o.alive = std::find(taken.begin(), taken.end(), false) != taken.end();
        return true;
    }
};

inline const Register<CoinStack> kCoinStack{"CoinStack"};

// The coins' shared turn (Wait 0x26cc64 / control 0x2ce690): the base pose times a turn about its
// Y axis by the scene's sync angle, 6 degrees per game tick (traced, see Coin).
inline M34 coinSpin(const M34& base, long tick) {
    const float a = std::fmod(6.0f * float(tick), 360.0f) * float(M_PI / 180.0);
    const float c = std::cos(a), sn = std::sin(a);
    M34 w = base;
    for (int r = 0; r < 3; r++) {   // base x RotY(a)
        const float x = base.m[r * 4 + 0], z = base.m[r * 4 + 2];
        w.m[r * 4 + 0] = x * c - z * sn;
        w.m[r * 4 + 2] = x * sn + z * c;
    }
    return w;
}

// The pose 70 along its own up (0x2f97d8(actor, 1, 70.0) in the coin inits 0x391390 / 0x16f8d4).
inline M34 coinLifted(const Object& o) {
    M34 b = o.placement;
    const V3 up = o.up();
    b.m[3] += up.x * 70.0f;
    b.m[7] += up.y * 70.0f;
    b.m[11] += up.z * 70.0f;
    return b;
}

// Coin, a single placed coin (creator 0x453ec4, vtable 0x4ce8d4, init 0x391390; archive Coin):
//   ShadowType selects a variant init (1 DepthShadowOnly, 2 ShadowMaskOnly); IsConnectToCollision
//   (0x2f7040, +0x90) ties it to the map part below; nerves 0x4f5634 Wait (0x26cc64) and
//   0x4f5638 (0x408348). receiveMsg 0x390f70: the player's touch (0x2fa018; 0x2f3094 for coins
//   placed in a pipe route) takes the coin: 0x2fa044 (the get: reaction 取得, effect CoinGet,
//   sound HrGet), sensors off (0x2fd968), killed (vtable +0x18). Sensor: Body r80 at the Coin
//   joint. Init: 0x2f97d8(coin, 1, 70.0) moves the pose 70 along its own Y axis (memory of run
//   revolver-start-a: placement y 900, pose y 970). Wait 0x26cc64: the pose is the base rotation
//   (+0x94) times a turn about Y by the scene's sync angle (0x2f9d64; 0x286220 builds the
//   quaternion of angle / 2); the coins share one pose per frame (0x4f5624). Traced (exec 0x26cda0,
//   run revolver-start-a): the angle advances 6 degrees per game tick (204, 210, 216, ...).
//   Hypothesis: the angle is 6 x the ticks since the course started (its origin is not traced).
struct Coin : Behaviour {
    M34 base{};
    bool hidden = false;   // a transparent coin's real coin, dead until the reveal (0x16f8d4)
    explicit Coin(Object& o) {
        base = coinLifted(o);
        o.world = base;
        if (!o.string("SpawnedBy", "").empty()) {
            hidden = true;
            o.visible = false;
        }
    }
    // 0x16f2f0: the transparent coin puts its coin where it is and makes it alive (vtable +0x14).
    void appear(Object& o, const M34& at) {
        base = at;
        o.world = at;
        hidden = false;
        o.visible = true;
    }
    CollisionConnector connector;   // IsConnectToCollision (0x2f7040, +0x90)
    void control(World& w, Object& o) override {
        if (hidden) {
            return;
        }
        if (connector.connectBelow(w, o, base)) {
            base = connector.follow(w, base);
        }
        o.world = coinSpin(base, w.tickCount);
    }
    void contactSpheres(const Object& o, std::vector<Sphere>& out) const override {
        if (hidden) {
            return;
        }
        for (size_t k = 0; k < o.sensors.size(); k++) {   // Body on the Coin joint: the posed coin
            if (o.sensors[k].type == "MapObj") {
                out.push_back({{o.world.m[3], o.world.m[7], o.world.m[11]}, o.sensors[k].radius, int(k)});
            }
        }
    }
    bool touch(World& w, Object& o, int) override {
        w.reaction(o, "取得");
        w.counters.coins += 1;
        o.alive = false;
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "coin %s taken (coins %d)\n", o.id.c_str(), w.counters.coins);
        }
        return true;
    }
};

inline const Register<Coin> kCoin{"Coin"};

// The coin an item brings out and collects at once (InitItem コインx1[自動取得]): the item
// generator 0x3e5c1c takes a pooled Coin actor of the CountUp variant (vtable 0x4bdad8, init
// 0x150f20), puts it at the item position, makes it appear (0x150fac: rotation zero, nerve
// 0x4154b0) and plays PgGet on its sound keeper. Nerve 0x4154b0: velocity (0, 24, 0) and the hit
// reaction 出現 (Appear effect) on step 0; every step the velocity's Y drops by 1.8 and the coin
// turns 9 degrees (0x2ab5a4); then it dies with the hit reaction 消滅 (CoinGet effect,
// ActorHitReactionCtrlCountUp). The CountUp coin runs in the item update category: one step per
// 60 Hz frame, two per game tick (the code's constants reproduce run sfxA exactly that way: CoinGet
// at 18 steps, rotated 162 degrees, 124.2 above the appear point, frames 167 -> 185).
struct ItemCoinCountUp : Behaviour {
    bool flying = false;
    int steps = 0;
    float velocity = 0, yaw = 0;
    V3 pos;
    explicit ItemCoinCountUp(Object& o) { o.visible = false; }
    void appear(World& w, Object& o, const V3& at) {
        pos = at;
        yaw = 0;
        velocity = 24.0f;
        steps = 0;
        flying = true;
        o.visible = true;
        place(o);
        w.reaction(o, "出現");
    }
    void place(Object& o) const {
        const float r = yaw * float(M_PI / 180.0), c = std::cos(r), s = std::sin(r);
        const float m[12] = {c, 0, s, pos.x, 0, 1, 0, pos.y, -s, 0, c, pos.z};
        for (int k = 0; k < 12; k++) {
            o.world.m[k] = m[k];
        }
    }
    void control(World& w, Object& o) override {
        for (int sub = 0; sub < 2 && flying; sub++) {
            velocity -= 1.8f;
            pos.y += velocity;
            yaw += 9.0f;
            place(o);
            if (++steps >= 18) {
                flying = false;
                w.reaction(o, "消滅");
                o.visible = false;
            }
        }
    }
};

inline const Register<ItemCoinCountUp> kItemCoinCountUp{"ItemCoinCountUp"};

// CollectItem, the course's gems (creator 0x452a78, vtable 0x4bdbb0, init 0x15151c):
//   Sensors (CollectItem archive): Body r100 and Reaction r70 at the CollectItem joint, y 70.
//   Nerves: Wait 0x41570c ("Wait", looping 250), PopUp 0x415770, spin reactions 0x4157f4 /
//   0x415868 (hit reaction 回転開始 "start spinning", then 0x415924; not ported), Got 0x4156a8.
//   receiveMsg 0x151088: on the Reaction sensor (id 0xd) a passing player starts the spin; on the
//   Body sensor (id 5) the player's touch (0x3e055c -> 0x2f2fa8: the 0x4fde08 family; refused
//   during PopUp and early in the appear nerves) takes the gem (0x151964): CollectItemId recorded
//   (0x335fdc), sensors off (0x2fd968), the "Got" sound (by the gems taken before, 0x337b9c,
//   through 0x2f8994) and with the third gem "Complete" (0x2de6b0), hit reaction 取得 on the player,
//   nerve Got.
//   Got 0x4156a8: step 0 starts GotHigh / GotLow / GotDokan by the placement's GotAnimation (name
//   table 0x4f5718, read from memory); when the action ends the gem is killed (vtable +0x18).
struct CollectItem : Behaviour {
    enum class Nerve { Hidden, PopUp, Wait, Got } nerve = Nerve::Wait;
    int step = 0;
    int id = 0, gotAnimation = 0;
    std::string popUpClip = "PopUp";

    explicit CollectItem(Object& o) {
        id = int(o.number("CollectItemId", 0));
        gotAnimation = int(o.number("GotAnimation", 0));
        if (!o.string("SpawnedBy", "").empty()) {   // brought out by another object (StageNpc.hpp)
            nerve = Nerve::Hidden;
            o.visible = false;
        }
    }
    // 0x151370: the item appears at `pos` with the pop-up clip (PopUp 0x415770: the clip, then Wait).
    void popUp(Object& o, const V3& pos, const std::string& clip) {
        o.visible = true;
        o.placement.m[3] = o.world.m[3] = pos.x;
        o.placement.m[7] = o.world.m[7] = pos.y;
        o.placement.m[11] = o.world.m[11] = pos.z;
        popUpClip = clip;
        setNerve(Nerve::PopUp);
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    void control(World&, Object& o) override {
        switch (nerve) {
        case Nerve::Hidden:
            return;
        case Nerve::PopUp:
            if (step == 0) {
                o.startAction(popUpClip);
            } else if (o.isActionEnd()) {
                setNerve(Nerve::Wait);
                return;
            }
            break;
        case Nerve::Wait:
            if (step == 0) {
                o.startAction("Wait");
            }
            break;
        case Nerve::Got:
            if (step == 0) {
                static const char* const kGot[] = {"GotHigh", "GotLow", "GotDokan"};
                o.startAction(kGot[std::min(std::max(gotAnimation, 0), 2)]);
            } else if (o.isActionEnd()) {
                o.alive = false;
            }
            break;
        }
        step++;
    }
    void contactSpheres(const Object& o, std::vector<Sphere>& out) const override {
        if (nerve == Nerve::Wait) {
            Behaviour::contactSpheres(o, out);
        }
    }
    bool touch(World& w, Object& o, int tag) override {
        if (nerve != Nerve::Wait || o.sensors[size_t(tag)].name != "Body") {
            return false;   // the Reaction sensor's spin is not ported
        }
        // 0x151964: "Got" with the gems taken before this one (0x337b9c) as its local variable 0,
        // which picks the script's tone (SeSyCollectItemGot: a choice of three pitches, rising;
        // hypothesis: the variable is the choice); with two taken before also "Complete".
        const int before = int(w.counters.collectItemIds.size());
        w.sound(o, "Got", std::min(before, 2));
        if (before == 2) {
            w.sound(o, "Complete");
        }
        w.counters.collectItems += 1;
        w.counters.collectItemIds.insert(id);
        w.reaction(o, "取得");
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "collect item %s (id %d) taken (%d)\n", o.id.c_str(), id, w.counters.collectItems);
        }
        setNerve(Nerve::Got);
        return true;
    }
};

inline const Register<CollectItem> kCollectItem{"CollectItem"};

// KinokoTreasure, the gold mushroom treasure (creator 0x453098, vtable 0x4c1b54, init 0x17e354):
//   Body r75 at y 60. Wait 0x41bf60 plays "Wait" (looping 240). receiveMsg 0x17dcb8: the player's
//   touch (0x2fd99c: the 0x4fde08 / 0x4fdfb8 families) gives hit reaction 取得, a coin count-up of
//   50 (0x2dd43c -> item keeper 0x1610a0; the port adds the 50 coins at once), sets the course
//   flag +0xe (0x1a2158) and kills the treasure. Hits from attacks squash it (0x17e630 scale;
//   not ported).
struct KinokoTreasure : Behaviour {
    int step = 0;
    bool hidden = false;   // a plant's treasure (お宝キノコ[地形追従]), out of sight until pulled
    explicit KinokoTreasure(Object& o) {
        if (!o.string("SpawnedBy", "").empty()) {
            hidden = true;
            o.visible = false;
        }
    }
    // The plant's item (0x185880): at the item position, "PopUp" (hypothesis: as the mushroom's
    // [真上出現] start), then "Wait".
    void appear(Object& o, const V3& at) {
        o.world = o.placement;
        o.world.m[3] = at.x;
        o.world.m[7] = at.y;
        o.world.m[11] = at.z;
        o.visible = true;
        hidden = false;
        step = 0;
        o.startAction(o.hasClip("PopUp") ? "PopUp" : "Wait");
    }
    void control(World&, Object& o) override {
        if (hidden) {
            return;
        }
        if (step++ == 0 && o.action.empty()) {
            o.startAction("Wait");
        }
        if (o.action == "PopUp" && o.isActionEnd()) {
            o.startAction("Wait");
        }
    }
    void contactSpheres(const Object& o, std::vector<Sphere>& out) const override {
        if (!hidden) {
            Behaviour::contactSpheres(o, out);
        }
    }
    bool touch(World& w, Object& o, int) override {
        if (hidden) {
            return false;
        }
        w.reaction(o, "取得");
        w.counters.coins += 50;
        w.counters.treasureTaken = true;
        o.alive = false;
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "treasure %s taken (coins %d)\n", o.id.c_str(), w.counters.coins);
        }
        return true;
    }
};

inline const Register<KinokoTreasure> kKinokoTreasure{"KinokoTreasure"};

// CoinChameleon (units CoinTransparent; creator 0x452e28, vtable 0x4c0308, init 0x16f8d4; archive
// "CoinChameleon"): a coin that cannot be seen. Sensors AttackAppear (r95, y50) and Get (r80).
//   receiveMsg 0x16eef8: an attack or touch on AttackAppear (sensor id 0xe; 0x2f3094 / 0x2f2984 /
//   0x2ee3f4 families, the player's touch among them) invalidates AttackAppear and starts the
//   appear nerve 0x16f540; from step 13 of it (or of 0x16f2f0) a touch takes the coin (0x2fa044,
//   sensors off, killed). A flash within 500 (0x2ce8f8 / 0x2f02d4 families: the headlamp's light)
//   makes it briefly visible (control 0x16fb60). The flash: the player's FlashRange sensor (table
//   0x4f687c: radius 400 at (-50, 200, 500) in his frame, ahead along the headlamp) touching one of
//   the coin's sensors sends the 0x2ce8f8 family; within 500 of the player (the literal 0x16f228
//   compared as float bits) the coin is in range this tick (+0xc6); entering the range (not in it
//   on the previous tick, +0xc7) while not flashing starts the flash (+0xc5): alpha (+0xc8) 255,
//   minus 5 each tick, drawn with that alpha (0x220f30), hidden again at 0.
struct CoinChameleon : Behaviour {
    // Nerves: Wait (hidden, flashes in the headlamp), Appear 0x16f540 and Reveal 0x16f2f0.
    enum class Nerve { Wait, Appear, Reveal } nerve = Nerve::Wait;
    bool appeared = false;   // in Appear or Reveal
    int step = 0;
    float rise = 0, velocity = 0;   // height above the base and vertical velocity (+0x30 -> +0x1c)
    int realCoin = -2;              // "<id>/coin" (-2: not looked up)
    bool flashing = false, inRange = false, wasInRange = false;
    int flash = 0;
    M34 base{};
    explicit CoinChameleon(Object& o) {
        o.visible = false;
        base = coinLifted(o);   // init 0x16f8d4: 0x2f97d8 with 70.0
        o.world = base;
    }
    V3 sensorAt(const Object& o, const SensorDecl& s) const {
        const auto& m = o.world.m;   // the posed coin
        return {m[3] + m[1] * s.y, m[7] + m[5] * s.y, m[11] + m[9] * s.y};
    }
    // The player's FlashRange contact (attackSensor 0x15838c, id 10 -> 0x15a344): sent only with
    // the headlamp's flash flags set (+0x2b2, +0x2b0; hypothesis: the headlamp is on), when the
    // target lies 0..200 below the FlashRange centre (FlashRangeAttackHeightMax 0 / Min -200) and
    // in the headlamp's cone: half angle 33 (global 0x4f65f8 +0x2c) x FlashRangeAttackDegreeScale
    // 0.7, out to the beam length minus FlashRangeAttackLengthOffset 100 (at least 1) plus the
    // target's radius; outside the cone the target must touch the cone's edge ray.
    bool flashRangeTouches(const World& w, const Object& o) const {
        if (!w.headlamp.on) {
            return false;
        }
        const PlayerView& p = w.player;
        const V3 side = captain_toad::RecoveredVec::cross(p.up, p.front);
        V3 c = captain_toad::RecoveredVec::add(p.pos, captain_toad::RecoveredVec::scale(side, -50.0f));
        c = captain_toad::RecoveredVec::add(c, captain_toad::RecoveredVec::scale(p.up, 200.0f));
        c = captain_toad::RecoveredVec::add(c, captain_toad::RecoveredVec::scale(p.front, 500.0f));
        const float cone = 33.0f * 0.7f * float(M_PI / 180.0);
        const float reach = std::max(w.headlamp.length - 100.0f, 1.0f);
        for (const SensorDecl& s : o.sensors) {
            if (s.radius <= 0) {
                continue;
            }
            const V3 t = sensorAt(o, s);
            V3 d = captain_toad::RecoveredVec::sub(t, c);
            const float r = 400.0f + s.radius;
            if (captain_toad::RecoveredVec::dot(d, d) >= r * r) {
                continue;   // the sensors do not overlap
            }
            const float dy = t.y - c.y;
            if (dy > 0.0f || dy < -200.0f) {
                continue;
            }
            d = captain_toad::RecoveredVec::sub(t, w.headlamp.pos);
            const float dist2 = captain_toad::RecoveredVec::dot(d, d);
            if (dist2 < 1.0e-6f) {
                continue;
            }
            const float dist = std::sqrt(dist2);
            const V3 dh = captain_toad::RecoveredVec::scale(d, 1.0f / dist);
            const V3& b = w.headlamp.dir;
            const float cosA = captain_toad::RecoveredVec::dot(dh, b);
            if (cosA >= std::cos(cone)) {
                if (dist < reach + s.radius) {
                    return true;
                }
                continue;
            }
            V3 perp = captain_toad::RecoveredVec::sub(
                dh, captain_toad::RecoveredVec::scale(b, cosA));   // the cone's edge ray
            const float pl = std::sqrt(captain_toad::RecoveredVec::dot(perp, perp));
            if (pl < 1.0e-6f) {
                continue;
            }
            perp = captain_toad::RecoveredVec::scale(perp, 1.0f / pl);
            const V3 e = captain_toad::RecoveredVec::add(captain_toad::RecoveredVec::scale(b, std::cos(cone)),
                                                         captain_toad::RecoveredVec::scale(perp, std::sin(cone)));
            const float along = captain_toad::RecoveredVec::dot(e, d);
            if (along <= 0.0f) {
                if (dist < s.radius) {
                    return true;
                }
                continue;
            }
            const V3 q =
                captain_toad::RecoveredVec::sub(d, captain_toad::RecoveredVec::scale(e, std::min(along, reach)));
            if (captain_toad::RecoveredVec::dot(q, q) < s.radius * s.radius) {
                return true;
            }
        }
        return false;
    }
    // 0x2ce830, run twice per tick by both reveal nerves: gravity 2.4 (0x2ce8bc) while the coin is
    // above its base height (+0xac), else it rests there.
    void fall() {
        if (0.0f < rise + velocity) {
            velocity -= 2.4f;
        } else {
            rise = 0.0f;
            velocity = 0.0f;
        }
    }
    void nerveTick(World& w, Object& o) {
        if (nerve == Nerve::Appear) {
            if (step == 0) {
                // 0x16f540 step 0: "Get", the sound PgTransparentAppear, the jump (0, 44, 0)
                // unless placed in a pipe route, and the flash restarted at 255.
                o.startAction("Get");
                w.sound(o, "PgTransparentAppear");
                if (o.number("IsPlacementInRouteDokan", 0) == 0) {
                    velocity = 44.0f;
                }
                flashing = true;
                flash = 255;
            }
            fall();
            fall();
            if (step > 12) {
                nerve = Nerve::Reveal;
                step = -1;
            }
        } else if (nerve == Nerve::Reveal) {
            // 0x16f2f0: from step 3, once the coin rests (vertical velocity 0), it dies with the
            // hit reaction 可視 and its real coin comes alive at its place.
            fall();
            fall();
            if (step > 2 && velocity == 0.0f) {
                if (realCoin == -2) {
                    realCoin = -1;
                    for (size_t i = 0; i < w.objects.size(); i++) {
                        if (w.objects[i].string("SpawnedBy", "") == o.id && w.objects[i].cls == "Coin") {
                            realCoin = int(i);
                        }
                    }
                }
                w.reaction(o, "可視");
                o.alive = false;
                if (getenv("DIORAMA_DEBUG")) {
                    fprintf(stderr, "transparent coin %s revealed (tick %ld, coin %d)\n", o.id.c_str(), w.tickCount,
                            realCoin);
                }
                if (realCoin >= 0) {
                    if (auto* c = dynamic_cast<Coin*>(w.behaviours[size_t(realCoin)].get())) {
                        c->appear(w.objects[size_t(realCoin)], base);
                    }
                }
                return;
            }
        }
        rise += velocity;   // hypothesis: the actor's movement applies the velocity once per tick
        step++;
    }
    void control(World& w, Object& o) override {
        if (appeared) {
            nerveTick(w, o);
            if (!o.alive) {
                return;
            }
        }
        // control 0x16fb60 ends with the shared turn (0x2f9d64, 0x2ce690) outside the reveal
        // nerves; hypothesis: the reveal keeps turning with the shared angle.
        o.world = coinSpin(base, w.tickCount);
        const V3 up = o.up();
        o.world.m[3] += up.x * rise;
        o.world.m[7] += up.y * rise;
        o.world.m[11] += up.z * rise;
        if (appeared) {
            flashFade(o);
            return;
        }
        // receiveMsg's flash branch (the messages of this tick), then control 0x16fb60.
        if (flashRangeTouches(w, o)) {
            const V3 d = captain_toad::RecoveredVec::sub(o.translation(), w.player.pos);
            if (captain_toad::RecoveredVec::dot(d, d) < 500.0f * 500.0f) {
                inRange = true;
                if (!flashing && !wasInRange) {
                    flashing = true;
                    flash = 255;
                    if (getenv("DIORAMA_DEBUG")) {
                        fprintf(stderr, "transparent coin %s flashes (tick %ld)\n", o.id.c_str(), w.tickCount);
                    }
                }
            }
        }
        flashFade(o);
        wasInRange = inRange;
        inRange = false;
    }
    // control 0x16fb60: while flashing the alpha drops by 5 per tick; at 0 the model hides.
    void flashFade(Object& o) {
        if (flashing) {
            flash -= 5;
            if (flash <= 0) {
                flashing = false;
                flash = 0;
            }
        }
        // The model colour's alpha (0x220f30, 0xffffff | alpha << 24) multiplies the material's own
        // (Object::alpha; the coin's material is a faint glass whose alpha comes from its lighting).
        o.visible = flashing;
        o.alpha = float(flash) / 255.0f;
    }
    void contactSpheres(const Object& o, std::vector<Sphere>& out) const override {
        for (size_t s = 0; s < o.sensors.size(); s++) {
            const auto& d = o.sensors[s];
            if ((!appeared && d.name == "AttackAppear") || (appeared && d.name == "Get")) {
                out.push_back({sensorAt(o, d), d.radius, int(s)});
            }
        }
    }
    // receiveMsg 0x16eef8: the step-on message (0x2efe94) is accepted without further effect
    // (after the branches for other messages; the nerve conditions before it are not ported).
    bool stepOn(World&, Object&) override { return true; }
    bool touch(World& w, Object& o, int tag) override {
        const std::string& name = o.sensors[size_t(tag)].name;
        if (!appeared && name == "AttackAppear") {
            appeared = true;
            nerve = Nerve::Appear;
            step = 0;
            if (getenv("DIORAMA_DEBUG")) {
                fprintf(stderr, "transparent coin %s appears (tick %ld)\n", o.id.c_str(), w.tickCount);
            }
            return false;
        }
        // receiveMsg 0x16eef8: a touch takes the coin in Appear after step 12 or in Reveal.
        if (appeared && name == "Get" && ((nerve == Nerve::Appear && step > 12) || nerve == Nerve::Reveal)) {
            w.reaction(o, "取得");
            w.counters.coins += 1;
            o.alive = false;
            if (getenv("DIORAMA_DEBUG")) {
                fprintf(stderr, "transparent coin %s taken (coins %d)\n", o.id.c_str(), w.counters.coins);
            }
            return true;
        }
        return false;
    }
};

inline const Register<CoinChameleon> kCoinChameleon{"CoinChameleon"};

}   // namespace captain_toad::RecoveredStageItems
