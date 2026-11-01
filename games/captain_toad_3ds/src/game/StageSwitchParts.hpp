// =============================================================================================
// StageSwitchParts.hpp - map parts and helpers driven by stage switches (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: in the second course a bridge stands in front of the start; when the
// protagonist has almost crossed it, it breaks apart under him, he waves his arms in the air and
// falls into the water below. A stone cover at the far wall vanishes while he stands close to it.
//
// BreakMapPartsSwitchOnly (creator 0x452df8, vtable 0x4c0208, init 0x16e56c): the
//   BreakMapParts family with break type 5 (the class name selects it; type 5 takes no attack
//   message, receiveMsg 0x16e1d8). DelayFrame (+0xa0). Optional sub-actors 壊れモデル (break
//   model) and 残留モデル (trace model); the bridge has none. Its SwitchKill listener (functor
//   0x16e520) moves the Wait nerve to Delay (DelayFrame > 0, 0x419d44: waits DelayFrame steps)
//   or Break. Nerves (table 0x4f5588): Wait 0x419d40, Delay 0x419d44, Break 0x16e874.
//   Break 0x16e874: step 0 shows the break / trace models and plays the hit reaction 破壊
//   (0x2fb304 -> 0x289d64; the bridge: effect Break, sound Break, a camera shake); step 1 kills
//   the part (vtable +0x18: model and collision gone).
//
// PlayerBindAnimObj (creator 0x4535e4, vtable 0x4c6908, init 0x1bc554): hidden until its
//   SwitchAppear switch is on (0x2f2e50: appear at once when it already is). Nerves (0x4f5efc):
//   Wait 0x425884 asks the player to be bound (0x2ec564); the player's answer (receiveMsg 0x1bc464,
//   0x2ec3c8) stores him (+0x90); then the bind handle (0x2ec38c) and nerve Bound 0x4257f4:
//   step 0 plays PlayerAnimName (+0x98, e.g. "FallSign") on the bound player (0x2ebcbc); when
//   the clip has ended (0x2ebbdc) the player is released (0x336620, mode 5) and the object dies.
//   Hypotheses: the player accepts the request in any free state (walking / waiting); he keeps
//   his position and front while bound; release mode 5 lets him fall (no walk-on).
//
// AllDeadWatcher (creator 0x452fa8, vtable 0x4c1508, init 0x178198): creates its WatchTargetEnemy
//   links as actors (0x2d2398; the exporter places them) and, when all of them are dead, turns
//   its SwitchDeadOn switch on after SwitchOnDelayStep steps (+0x98). Its own SwitchKill kills
//   it. Hypothesis: the watch runs in its nerve each tick (the nerve table 0x178330 not decoded).
//
// VisibleSwitchMapParts (creator 0x453a44, vtable 0x4cad80, init 0x1eff94): with SwitchDisappear
//   (0x2ebdd8: two listeners) the part disappears (model and collision) while the switch is on
//   and appears again when it goes off; with SwitchAppear the other way round.
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

namespace captain_toad::RecoveredStageSwitchParts {

using namespace captain_toad::RecoveredStage;

struct BreakMapPartsSwitchOnly : Behaviour {
    enum class Nerve { Wait, Delay, Break } nerve = Nerve::Wait;
    int step = 0;
    int delay = 0;
    explicit BreakMapPartsSwitchOnly(Object& o) { delay = int(o.number("DelayFrame", 0)); }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    void control(World& w, Object& o) override {
        switch (nerve) {
        case Nerve::Wait:   // the SwitchKill listener (0x16e520)
            if (w.switchOn(o, "SwitchKill")) {
                setNerve(delay > 0 ? Nerve::Delay : Nerve::Break);
                control(w, o);   // the new nerve runs in the same update (keeper 0x2c8700)
            }
            return;
        case Nerve::Delay:   // 0x419d44
            if (step >= delay) {
                setNerve(Nerve::Break);
                control(w, o);
                return;
            }
            step++;
            return;
        case Nerve::Break:   // 0x16e874
            if (step == 0) {
                w.reaction(o, "破壊");
                step++;
                return;
            }
            o.alive = false;
            return;
        }
    }
};

inline const Register<BreakMapPartsSwitchOnly> kBreakMapPartsSwitchOnly{"BreakMapPartsSwitchOnly"};

struct PlayerBindAnimObj : Behaviour {
    enum class Nerve { Hidden, Wait, Bound, Done } nerve = Nerve::Hidden;
    int step = 0;
    bool accepted = false;   // +0x90: the player answered the bind request
    std::string anim;
    explicit PlayerBindAnimObj(Object& o) {
        anim = o.string("PlayerAnimName", "");
        o.visible = false;
    }
    // Traced (exec 0x425884 / 0x4257f4 / 0x1bc464, run revolver-west-east): the switch turns on in
    // tick t; Wait runs from t + 1 and its request is answered there (the player stands still from
    // that tick, at his position of its start); Wait sees the answer at t + 2 and moves to Bound;
    // Bound's step 0 at t + 3 starts the clip; the clip's end is seen one tick after its last
    // frame (52-frame FallSign: step 28), then the player is released.
    void control(World& w, Object& o) override {
        switch (nerve) {
        case Nerve::Hidden:
            if (!w.switchOn(o, "SwitchAppear")) {
                return;
            }
            nerve = Nerve::Wait;
            [[fallthrough]];
        case Nerve::Wait:   // 0x425884
            if (accepted) {
                nerve = Nerve::Bound;
                step = 0;
                return;
            }
            if (w.bind.active || !w.playerFree) {
                return;
            }
            accepted = true;   // the player's reply (receiveMsg 0x1bc464 -> 0x158630)
            w.bind.active = true;
            w.bind.binder = w.indexOf(o);
            w.bind.pos = w.playerPrevPos;
            w.bind.front = w.player.front;
            w.bind.clip = anim;
            w.bind.frame = 0;
            w.bind.walkOn = false;
            w.bind.fall = false;
            return;
        case Nerve::Bound: {   // 0x4257f4
            const float frames = w.playerClipFrames ? w.playerClipFrames(anim) : 0.0f;
            const float shown = step > 0 ? float(step - 1) * 2.0f : 0.0f;   // the player advances it
            if (step > 0 && float(step - 2) * 2.0f >= frames) {             // 0x2ebbdc: the clip has ended
                w.bind.active = false;   // released (0x336620, mode 5): he falls from where he is
                w.bind.walkOn = false;
                w.bind.fall = true;
                nerve = Nerve::Done;
                o.alive = false;
                return;
            }
            w.bind.frame = shown < frames ? shown : frames;
            step++;
            return;
        }
        case Nerve::Done:
            return;
        }
    }
};

inline const Register<PlayerBindAnimObj> kPlayerBindAnimObj{"PlayerBindAnimObj"};

struct AllDeadWatcher : Behaviour {
    int countdown = -1;
    bool done = false;
    explicit AllDeadWatcher(Object&) {}
    void control(World& w, Object& o) override {
        if (done || w.switchOn(o, "SwitchKill")) {
            return;
        }
        if (countdown < 0) {
            auto it = o.links.find("WatchTargetEnemy");
            if (it == o.links.end()) {
                return;
            }
            for (const LinkTarget& t : it->second) {
                for (const Object& other : w.objects) {
                    if (other.id == t.id && other.alive) {
                        return;
                    }
                }
            }
            countdown = int(o.number("SwitchOnDelayStep", 0));
        }
        if (countdown-- <= 0) {
            w.turnOn(o, "SwitchDeadOn");
            done = true;
        }
    }
};

inline const Register<AllDeadWatcher> kAllDeadWatcher{"AllDeadWatcher"};

struct VisibleSwitchMapParts : Behaviour {
    explicit VisibleSwitchMapParts(Object&) {}
    void control(World& w, Object& o) override {
        bool shown = true;
        if (o.link("SwitchDisappear")) {
            shown = !w.switchOn(o, "SwitchDisappear");
        } else if (o.link("SwitchAppear")) {
            shown = w.switchOn(o, "SwitchAppear");
        }
        o.visible = shown;
        o.collision = shown;
    }
};

inline const Register<VisibleSwitchMapParts> kVisibleSwitchMapParts{"VisibleSwitchMapParts"};

// FixMapParts (creator 0x4547d4, ctor 0x220368, vtable 0x4d2ab4, init 0x2b2224): a still map part.
//   With a SwitchAppear link (0x2ce368 -> 0x285e64) it starts dead (vt+0x1c), appears when the
//   switch turns on (vt+0x10) and dies when it turns off (vt+0x18); otherwise it appears
//   (vt+0x14) and a SwitchKill link (0x2b75ec -> 0x325de8) kills it while the switch is on (on:
//   vt+0x18, off: vt+0x10). E.g. DoubleCherryRuinsStage swaps its course model StepALower for
//   StepA when its first switch pair's timers run out (obj361 / obj348).
struct FixMapParts : Behaviour {
    explicit FixMapParts(Object&) {}
    void control(World& w, Object& o) override {
        bool shown = true;
        if (o.link("SwitchAppear")) {
            shown = w.switchOn(o, "SwitchAppear");
        } else if (o.link("SwitchKill")) {
            shown = !w.switchOn(o, "SwitchKill");
        }
        o.visible = shown;
        o.collision = shown;
    }
};

inline const Register<FixMapParts> kFixMapParts{"FixMapParts"};

// ConveyerMapParts (creator 0x454954, ctor 0x247ad4, vtable 0x4d3c98, init 0x247698): copies of
//   the archive's collision travelling along the key path from the placement to KeyMoveNext
//   (0x24eaf8; a straight line here), e.g. the moving floor of RevolverRuinsStage's water channel.
//   Init: speed (+0xa8) = MoveSpeed x 2 (literal 0x2479b8: per game tick); interval (+0xac) =
//   PartsInterval, at least 10; count = int(length / interval) + 1 parts (actors 0x222b54, each
//   with its own collision); loop (+0xb0) = interval x count; offset (+0xa4) = interval x
//   StartRate + interval, wrapped (0x2fe398). With a SwitchStart-type link (0x2bfc74) it waits in
//   nerve 0x4f7bb8; otherwise Move (0x4f7bbc, exec 0x428804): offset = wrap(offset + speed + loop)
//   (IsRideOnlyMove scales the speed by the riding count, +0xb8 / +0xbc, not ported), then every
//   part (0x2228c0 -> 0x222b08): along = wrap(offset + its own place), placed there on the path;
//   a part beyond the path's end is hidden (0x2cdaa4) and shown again (0x2cd9d8) when it comes
//   back. Traced: the protagonist standing on the channel floor moves 3.0 along +z per tick (run
//   revolver-west-east, MoveSpeed 1.5). Hypothesis: the parts' own places are k x interval.
struct ConveyerMapParts : Behaviour {
    V3 from{}, dir{};
    float length = 0, speed = 0, interval = 10, loop = 10, offset = 0;
    int count = 1;
    explicit ConveyerMapParts(Object& o) {
        from = o.translation();
        V3 to = from;
        if (const LinkTarget* t = o.link("KeyMoveNext")) {
            to = t->translation();
        }
        const V3 d = captain_toad::RecoveredVec::sub(to, from);
        length = std::sqrt(captain_toad::RecoveredVec::dot(d, d));
        dir = length > 0 ? captain_toad::RecoveredVec::scale(d, 1.0f / length) : V3{0, 0, 0};
        speed = float(o.number("MoveSpeed", 0)) * 2.0f;
        interval = float(o.number("PartsInterval", 0));
        if (interval <= 10.0f) {
            interval = 10.0f;
        }
        count = int(length / interval) + 1;
        loop = interval * float(count);
        offset = wrap(interval * float(o.number("StartRate", 0)) + interval);
        o.partWorlds.assign(size_t(count), o.placement);
        o.partShown.assign(size_t(count), true);
        place(o);
    }
    float wrap(float x) const {
        x = std::fmod(x, loop);
        return x < 0 ? x + loop : x;
    }
    void place(Object& o) const {
        for (int k = 0; k < count; k++) {
            const float along = wrap(offset + float(k) * interval);
            M34& m = o.partWorlds[size_t(k)];
            m = o.placement;
            m.m[3] = from.x + dir.x * along;
            m.m[7] = from.y + dir.y * along;
            m.m[11] = from.z + dir.z * along;
            o.partShown[size_t(k)] = along <= length;
        }
    }
    void control(World&, Object& o) override {
        offset = wrap(offset + speed + loop);
        place(o);
    }
};

inline const Register<ConveyerMapParts> kConveyerMapParts{"ConveyerMapParts"};

// ClockMapParts (creator 0x454834, ctor 0x22b3f8, vtable 0x4d30d8, init 0x22afac): a map part
//   that turns by ClockAngle about its RotateAxis (0 X, 1 Y, 2 Z: 0x550170 / 0x55017c / 0x550188)
//   every cycle, e.g. RevolverRuinsStage's turning stones. Init: base rotation (+0x90), the times
//   in game ticks (x 0.5, literal 0x22b1fc): DelayTime (+0xac), RotateTime (+0xb0), WaitTime
//   (+0xb4), the "MiddleSign" clip's length (+0xb8, 0 without it); +0xbc = sign + rotate + 1,
//   +0xc0 = wait + +0xbc + 1 (the cycle). Named nerves (0x2cdb24, table 0x535a64: StandBy, Delay,
//   RotateSign, Rotate, Wait, AssistStop...), starting in Rotate (Delay with a DelayTime).
//   Rotate 0x421404: angle = wrap((turns + (counter - +0xb8) / (+0xbc - +0xb8 - 1)) x ClockAngle
//   + 360) (0x2fe398), pose = base x turn about the axis (0x2a84ec); counter++; at +0xbc: turns++,
//   Wait, and the action "RotateEnd" for its effects and sound (0x2de6b0).
//   Wait 0x42131c: counter++; at the cycle: counter -= cycle, Rotate (RotateSign with a MiddleSign).
//   Children (ChildStep links, 0x29fdb0): actors attached to the part (0x23c2e0), their offset kept
//   in the part's frame (init 0x330c80). Hypothesis: they follow the part's position only (the
//   steps stay level). Not ported: SwitchStop (the goal demo stops the turning), the assist stop
//   nerves, StandBy.
struct ClockMapParts : Behaviour {
    enum class Nerve { Delay, Rotate, Wait } nerve = Nerve::Rotate;
    int step = 0, counter = 0, turns = 0;
    int delay = 0, rotateTime = 0, waitTime = 0, sign = 0, rotateEnd = 0, cycle = 0;
    int axis = 1;
    float clockAngle = 0;
    struct Child {
        size_t index;
        V3 offset;   // in the part's frame
    };
    std::vector<Child> children;
    bool childrenFound = false;
    explicit ClockMapParts(Object& o) {
        clockAngle = float(o.number("ClockAngle", 0));
        axis = int(o.number("RotateAxis", 1));
        delay = int(float(o.number("DelayTime", 0)) * 0.5f);
        waitTime = int(float(o.number("WaitTime", 0)) * 0.5f);
        rotateTime = int(float(o.number("RotateTime", 0)) * 0.5f);
        sign = 0;   // no "MiddleSign" clip in the course's models
        rotateEnd = sign + rotateTime + 1;
        cycle = waitTime + rotateEnd + 1;
        if (delay > 0) {
            nerve = Nerve::Delay;
        }
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    void pose(Object& o, float degrees) const {
        const float a = degrees * float(M_PI / 180.0), c = std::cos(a), sn = std::sin(a);
        float r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};   // the turn about the local axis
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        r[u][u] = c;
        r[u][v] = -sn;
        r[v][u] = sn;
        r[v][v] = c;
        o.world = o.placement;
        for (int i = 0; i < 3; i++) {   // base x turn
            for (int j = 0; j < 3; j++) {
                o.world.m[i * 4 + j] = o.placement.m[i * 4 + 0] * r[0][j] + o.placement.m[i * 4 + 1] * r[1][j] +
                                       o.placement.m[i * 4 + 2] * r[2][j];
            }
        }
    }
    void findChildren(World& w, const Object& o) {
        childrenFound = true;
        auto it = o.links.find("ChildStep");
        if (it == o.links.end()) {
            return;
        }
        for (const LinkTarget& t : it->second) {
            for (size_t i = 0; i < w.objects.size(); i++) {
                if (w.objects[i].id != t.id) {
                    continue;
                }
                const V3 d = captain_toad::RecoveredVec::sub(w.objects[i].translation(), o.translation());
                const auto& m = o.placement.m;   // into the part's frame (rotation transposed)
                children.push_back({i,
                                    {m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z,
                                     m[2] * d.x + m[6] * d.y + m[10] * d.z}});
            }
        }
    }
    void moveChildren(World& w, const Object& o) {
        for (const Child& c : children) {
            Object& child = w.objects[c.index];
            const auto& m = o.world.m;
            child.world = child.placement;
            child.world.m[3] = m[3] + m[0] * c.offset.x + m[1] * c.offset.y + m[2] * c.offset.z;
            child.world.m[7] = m[7] + m[4] * c.offset.x + m[5] * c.offset.y + m[6] * c.offset.z;
            child.world.m[11] = m[11] + m[8] * c.offset.x + m[9] * c.offset.y + m[10] * c.offset.z;
        }
    }
    void control(World& w, Object& o) override {
        if (!childrenFound) {
            findChildren(w, o);
        }
        switch (nerve) {
        case Nerve::Delay:   // 0x421394
            if (step >= delay - 1) {
                setNerve(Nerve::Rotate);
                control(w, o);
                return;
            }
            step++;
            return;
        case Nerve::Rotate: {   // 0x421404
            const float progress = float(counter - sign) / float(rotateEnd - sign - 1);
            float a = std::fmod((float(turns) + progress) * clockAngle + 360.0f, 360.0f);
            if (a < 0) {
                a += 360.0f;
            }
            pose(o, a);
            moveChildren(w, o);
            counter++;
            if (counter >= rotateEnd) {
                turns++;
                setNerve(Nerve::Wait);
                o.startAction("RotateEnd");
            }
            return;
        }
        case Nerve::Wait:   // 0x42131c
            counter++;
            if (counter >= cycle) {
                counter -= cycle;
                setNerve(Nerve::Rotate);
            }
            return;
        }
    }
};

inline const Register<ClockMapParts> kClockMapParts{"ClockMapParts"};

}   // namespace captain_toad::RecoveredStageSwitchParts
