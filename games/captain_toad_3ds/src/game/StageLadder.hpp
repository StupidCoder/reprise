// =============================================================================================
// StageLadder.hpp - ladders (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: walking into the foot of a ladder, the protagonist steps up to it and
// climbs rung by rung while the stick is held up; at the top he climbs over the edge and walks on.
// Walking off a ledge onto the top of a ladder, he turns round, takes hold of it and can climb down.
//
// Ladder (creator 0x454138, vtable 0x4cf410, init 0x3b29d8): spans from its placement to its
// LadderLink point (swapped when the link is lower); rungs every 50. Sensors (Ladder archive):
// BindBottom and BindTop (BindableAllPlayer, r100, y50), PlayerDummy. SwitchValidate: usable only
// once that switch is on. MtxConnectType: a matrix connector (0x2fce8c) attached after placement
// (0x3b24fc) to the collision a segment hits, which the ladder then follows: type 1 from the
// LadderLink point + 50 up, 100 down; type 2 (a ladder mounted on a wall) from LadderLink + 50 up
// + 50 along the ladder's Z axis F, along -100 F, and if that finds nothing from the placement
// - 50 up + 50 F along -100 F (constants 0x3b2748..0x3b2754). Lift A's ladder (type 2) is on the
// lift's side: it rides up out of the ground with the lift.
// The ladder binds the player (receiveMsg 0x3b0b70, attackSensor 0x3b11d4) and drives him with its
// nerves: Wait 0x40b0a8, Attach 0x3b21d0 ("Move", interpolated onto the ladder; after step 4 ->
// ClimbWait), ClimbWait 0x3b1fbc ("LadderClimbWait"; the stick within 40 degrees of up -> ClimbUp,
// of down -> ClimbDown, 0x2682ec), ClimbUp 0x3b13c0 (one rung, "LadderClimbUp"; over the top
// -> Exit 0x3b0690), ClimbDown 0x3b1b04 ("LadderClimbDown"; at the foot -> Exit, with dash
// first DashLand 0x3b0e84), Exit 0x3b0690 ("Move", 3 steps onto the ledge or off the foot,
// then the player is released, 0x2cf080).
//
// Checked against the original (run ladA: research/inputs/pullup-ladder-a.tsv from front-input-a,
// obj950; 60 Hz frames, one tick = 2 frames), which shows the nerves' timing and distances:
//   contact with BindBottom (the player's contact sensors against r100 at the bottom + 50 up,
//   walking toward the ladder, no button): the player is held for 2 ticks, then interpolated in 5
//   ticks onto the ladder (axis + front * 65, at the bottom height), held 2 ticks (end of Attach
//   and the first ClimbWait); each rung: 5 ticks of +10, then 2 held ticks; the last rung is
//   followed at once by the exit: 3 ticks, linearly to the top + 50 up and 5 beyond the axis.
// The descent, the foot's exit, the dash variants, the stick lock and PlayerDummy are ported
// from the decompilation and not traced (docs/level-pullup.md, "Ladder: bottom exit ...").
// The grab (receiveMsg 0x3b0b70, nerve table: static constructor 0x43b89c, nerve objects
// 0x4f5ca0.. = Wait, Attach, ClimbWait, ClimbUp, ClimbDown, DashLand, Exit, cooldown): in Wait,
// with the player's sensor on BindBottom or BindTop and the direction tests of grabTest()
// (0x268620, 0x268520), the request is taken; the bind start then runs 0x3b2758, which picks the
// nearer ladder end (first piece + F 65, or last piece - 50 up + F 65) as the Attach target, and
// Attach (0x3b21d0) interpolates position and facing in 5 steps, then ClimbWait. So walking off
// the ledge onto a ladder's top attaches one rung below the top, turned round to face it; the
// stick down then climbs down. Hypothesis: the last piece sits at the LadderLink point (BindTop
// centre = ledge height; the traced top exit ends at the same height).
// =============================================================================================
#pragma once

#include "game/StageObjects.hpp"

#include <cmath>

namespace captain_toad::RecoveredStageLadder {

using namespace captain_toad::RecoveredStage;

// The ladder's stick reading (0x2682ec, run by Attach, ClimbWait, ClimbUp and ClimbDown each
// tick). Without a lock the stick within 40 degrees of up asks to climb up, within 40 degrees of
// down to climb down. A lock (set at the grab, 0x3b10b4) keeps the direction the player was
// pushing when he took hold of the ladder meaning "on" (up after a bottom grab, down after a top
// grab) while the stick stays within an allowed angle of the canonical direction; the allowed
// angle shrinks toward the stick (angle + 10, never growing) and the lock ends once it is 40 or
// less, after 5 ticks outside it, after 5 ticks of a released stick, or at once when the stick
// turns against the recorded direction.
struct StickLock {
    int mode = 0;               // +0x13c: 0 none, 1 up, 2 down
    int result = 0;             // +0x130: 0 none, 1 climb up, 2 climb down
    int grace = 0;              // +0x140
    float recX = 0, recY = 0;   // +0x134 / +0x138: the recorded stick
    float allowed = 0;          // +0x144: degrees

    static float deg(float c) { return std::acos(std::max(-1.0f, std::min(1.0f, c))) * 57.2958f; }
    // 0x3b10b4 (the bind start): record the stick of the grab.
    void record(float sx, float sy, bool fromBottom) {
        const float len = std::sqrt(sx * sx + sy * sy);
        if (len < 0.1f) {
            grace = 0;
            result = 0;
            mode = 0;
            return;
        }
        mode = fromBottom ? 1 : 2;
        grace = 5;
        recX = sx;
        recY = sy;
        const float c = (sy / len) * (fromBottom ? 1.0f : -1.0f);
        allowed = std::max(0.0f, std::min(180.0f, deg(c) + 10.0f));
    }
    void update(float sx, float sy) {   // 0x2682ec
        const float len = std::sqrt(sx * sx + sy * sy);
        if (len < 0.1f) {
            grace--;
            result = 0;
            if (grace <= 0) {
                mode = 0;
            }
            return;
        }
        if (grace > 0) {
            grace = 5;
        }
        if (mode != 0 && recX * sx + recY * sy < 0.0f) {   // turned against the recorded stick
            grace = 0;
            mode = 0;
            result = 0;
        }
        const float ny = sy / len;
        if (mode == 0) {
            result = ny >= std::cos(0.698132f) ? 1 : ny <= std::cos(2.44346f) ? 2 : 0;
            return;
        }
        result = mode;
        const float a = deg(ny * (mode == 1 ? 1.0f : -1.0f));
        if (a < allowed) {
            allowed = std::min(allowed, std::max(0.0f, std::min(180.0f, a + 10.0f)));
            if (allowed > 40.0f) {
                return;
            }
            mode = 0;
            return;
        }
        if (grace > 0) {
            grace--;
            return;
        }
        result = 0;
        mode = 0;
    }
};

struct Ladder : Behaviour {
    enum class Nerve { Wait, Hold, Attach, ClimbWait, ClimbUp, ClimbDown, DashLand, Exit } nerve = Nerve::Wait;
    int step = 0;
    V3 bottom, top, front;   // front: the side the player climbs on (placement Z axis)
    float base = 0;          // the player's height on the ladder, relative to the bottom
    V3 from, to;             // interpolation ends (Attach, Exit)
    V3 faceFrom, faceTo;
    float clipFrame = 0;
    std::string clip;
    int connected = -2;   // MtxConnectType: the object it stands on (-1 none, -2 not looked up)
    V3 connectedStart;
    bool usesConnect = false;
    bool fromTop = false;   // the bind came through BindTop
    float attachBase = 0;   // the rung height Attach ends on
    bool dash = false;      // +0xe8: the dash button was held (LadderClimb*Dash, faster)
    StickLock lock;
    // PlayerDummy (the ladder's MapObj sensor standing in for the player's Foot sensor while he
    // is bound; init radius 0 from the archive, set to the Foot radius by Attach).
    bool dummyValid = false;
    float dummyRadius = 0;
    V3 dummyPos;
    static constexpr float kRung = 50, kOffset = 65, kBottomStep = 70;

    explicit Ladder(Object& o) {
        bottom = o.translation();
        const LinkTarget* l = o.link("LadderLink");
        top = l ? l->translation() : bottom;
        if (top.y < bottom.y) {
            std::swap(top, bottom);
        }
        front = {o.placement.m[2], 0, o.placement.m[10]};
        captain_toad::RecoveredVec::normalize(front);
        usesConnect = o.number("MtxConnectType", 0) != 0;
        if (const SensorDecl* d = o.sensor("PlayerDummy")) {
            dummyRadius = d->radius;
        }
    }
    float height() const { return top.y - bottom.y; }
    V3 axisAt(float h) const { return {bottom.x, bottom.y + h, bottom.z}; }
    V3 climbPos(float h) const {
        return captain_toad::RecoveredVec::add(axisAt(h), captain_toad::RecoveredVec::scale(front, kOffset));
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    void play(const std::string& name) {
        if (clip != name) {
            clip = name;
            clipFrame = 0;
        }
    }
    void bindPlayer(World& w, Object& o, const V3& pos, const V3& face) {
        w.bind.active = true;
        w.bind.binder = w.indexOf(o);
        w.bind.pos = pos;
        w.bind.front = face;
        w.bind.clip = clip;
        w.bind.frame = clipFrame;
        w.bind.walkOn = false;
        clipFrame += 2;   // one 30 Hz tick of a 60 Hz clip
        dummyPos = captain_toad::RecoveredVec::add(
            pos, V3{0, playerSensors(w.player.mini)[0].up, 0});   // the Foot sensor's centre
    }
    // The player is let go (0x2cf080 / 0x2cf044); the session places him once more.
    void release(World& w, const V3& pos, const V3& face, bool walkOn) {
        w.bind.active = false;
        w.bind.pos = pos;
        w.bind.front = face;
        w.bind.walkOn = walkOn;
        dummyValid = false;
    }
    static V3 lerp(const V3& a, const V3& b, float t) {
        return captain_toad::RecoveredVec::add(
            a, captain_toad::RecoveredVec::scale(captain_toad::RecoveredVec::sub(b, a), t));
    }
    static float yaw(const V3& v) { return std::atan2(v.x, v.z); }
    // 0x2681a0: when the two facings are nearly opposite (dot below -0.99), the start facing is
    // replaced by the end facing turned by 3.1241393 (179 degrees) about the ladder's up axis,
    // which fixes the side of the half turn.
    static V3 fixHalfTurn(const V3& a, const V3& b) {
        if (a.x * b.x + a.y * b.y + a.z * b.z >= -0.99f) {
            return a;
        }
        const float r = yaw(b) + 3.1241393f;
        return {std::sin(r), 0, std::cos(r)};
    }
    // The facing during Attach and Exit: the quaternion slerp (0x2b554c) of two horizontal
    // facings is the turn about the up axis by the fraction t of the angle between them.
    static V3 turn(const V3& a, const V3& b, float t) {
        const float f = yaw(a);
        float d = yaw(b) - f;
        while (d > float(M_PI)) {
            d -= 2.0f * float(M_PI);
        }
        while (d <= -float(M_PI)) {
            d += 2.0f * float(M_PI);
        }
        const float r = f + d * t;
        return {std::sin(r), 0, std::cos(r)};
    }
    // The grab tests of the bind request. F is the ladder's Z axis; the player climbs on its +F
    // side. The direction is the player's horizontal velocity (vtable +0x24), at least 0.001 long.
    //   bottom (0x268620 / 0x268520 with 0): moving within 75 degrees of -F, on the +F side;
    //   top (with 1): moving within 45 degrees of +F, not below the ladder's position, behind the
    //   ladder plane by less than 15 (on the ledge, at its edge).
    // The id mapping (the ladder sensor ids 0x1e / 0x1d to bottom / top) follows from these
    // geometric tests (hypothesis; the id table itself is not located).
    bool grabTest(const World& w, const Object& o, bool fromTheTop) const {
        const V3 F{o.placement.m[2], o.placement.m[6], o.placement.m[10]};
        const V3 v{w.player.vel.x, 0, w.player.vel.z};
        const float len = std::sqrt(v.x * v.x + v.z * v.z);
        if (len < 0.001f) {
            return false;
        }
        const float c = std::max(-1.0f, std::min(1.0f, captain_toad::RecoveredVec::dot(F, v) / len));
        const float deg = std::acos(fromTheTop ? c : -c) * float(180.0 / M_PI);
        if (deg >= (fromTheTop ? 45.0f : 75.0f)) {
            return false;
        }
        const V3 at{o.world.m[3], o.world.m[7], o.world.m[11]};   // the ladder actor's position
        const V3 d = captain_toad::RecoveredVec::sub(w.player.pos, at);
        const float side = d.x * F.x + d.z * F.z;
        if (!fromTheTop) {
            return side >= 0.0f;
        }
        return w.player.pos.y >= at.y && side < 0.0f && side >= -15.0f;
    }
    bool valid(World& w, const Object& o) const { return !o.link("SwitchValidate") || w.switchOn(o, "SwitchValidate"); }

    // MtxConnectType (0x3b24fc): attach once to the collision the decoded segments hit.
    void follow(World& w, Object& o) {
        if (!usesConnect) {
            return;
        }
        if (connected == -2) {
            connected = -1;
            const int type = int(o.number("MtxConnectType", 0));
            const LinkTarget* l = o.link("LadderLink");
            const V3 link = l ? l->translation() : o.translation();
            auto hit = [&](const V3& from, const V3& dir) {
                const int k = w.ray ? w.ray(from, dir) : -1;
                return k == w.indexOf(o) ? -1 : k;
            };
            using captain_toad::RecoveredVec::add;
            using captain_toad::RecoveredVec::scale;
            if (type == 1) {
                connected = hit(add(link, V3{0, 50, 0}), V3{0, -100, 0});
            } else if (type == 2) {
                V3 f{o.placement.m[2], o.placement.m[6], o.placement.m[10]};
                const V3 back = scale(f, -100.0f);
                connected = hit(add(add(link, V3{0, 50, 0}), scale(f, 50.0f)), back);
                if (connected < 0) {
                    connected =
                        hit(add(captain_toad::RecoveredVec::sub(o.translation(), V3{0, 50, 0}), scale(f, 50.0f)), back);
                }
            }
            if (connected >= 0) {
                connectedStart = w.objects[size_t(connected)].translation();
            }
            if (getenv("DIORAMA_DEBUG")) {
                fprintf(stderr, "ladder %s connects to %s\n", o.id.c_str(),
                        connected >= 0 ? w.objects[size_t(connected)].id.c_str() : "-");
            }
        }
        if (connected >= 0) {
            const auto& c = w.objects[size_t(connected)];
            const V3 d = captain_toad::RecoveredVec::sub(V3{c.world.m[3], c.world.m[7], c.world.m[11]}, connectedStart);
            const V3 b0 = o.translation();
            const float h = height();
            bottom = captain_toad::RecoveredVec::add(b0, d);
            if (o.link("LadderLink") && o.link("LadderLink")->translation().y < b0.y) {
                bottom = captain_toad::RecoveredVec::add(o.link("LadderLink")->translation(), d);
            }
            top = {bottom.x, bottom.y + h, bottom.z};
            o.world = o.placement;   // the model rides along (translation only, as the ported parts)
            o.world.m[3] += d.x;
            o.world.m[7] += d.y;
            o.world.m[11] += d.z;
        }
    }

    // attackSensor 0x3b11d4: during Attach and ClimbDown, PlayerDummy touching an enemy body
    // (sensor kind 6, EnemyBody) at or below the player's feet sends that object the step-on
    // message (0x338f14, type 0x4fe37c); when the object refuses it the ladder lets the player go
    // where he is (he falls) and waits again.
    bool dummyCheck(World& w, Object& o) {
        if (!dummyValid || (nerve != Nerve::Attach && nerve != Nerve::ClimbDown)) {
            return false;
        }
        for (size_t i = 0; i < w.objects.size(); i++) {
            Object& e = w.objects[i];
            if (!e.alive || &e == &o) {
                continue;
            }
            for (const SensorDecl& s : e.sensors) {
                if (s.type != "EnemyBody") {
                    continue;
                }
                const V3 c = captain_toad::RecoveredVec::add(
                    V3{e.world.m[3], e.world.m[7], e.world.m[11]},
                    captain_toad::RecoveredVec::scale(V3{e.world.m[1], e.world.m[5], e.world.m[9]}, s.y));
                const V3 d = captain_toad::RecoveredVec::sub(c, dummyPos);
                const float r = s.radius + dummyRadius;
                if (captain_toad::RecoveredVec::dot(d, d) >= r * r || c.y > dummyPos.y) {
                    continue;
                }
                Behaviour* b = w.behaviours[i].get();
                if (b && b->stepOn(w, e)) {
                    continue;
                }
                release(w, w.bind.pos, w.bind.front, false);
                setNerve(Nerve::Wait);
                return true;
            }
        }
        return false;
    }
    // Enter the exit move (0x3b0690) toward `target` facing `face` (lock mode as the original sets
    // it). Its step 0 (t = 0, the start pose) falls on the switch tick: the first shown pose is a
    // third of the way, right after the climb's last one (the port's positions then agree with the
    // original's top exit, run hc1). Which keeper order puts step 0 there is not decoded.
    void startExit(World& w, Object& o, const V3& start, const V3& target, const V3& face) {
        from = start;
        to = target;
        faceFrom = fixHalfTurn(w.bind.front, face);
        faceTo = face;
        play("Move");   // at step 0
        setNerve(Nerve::Exit);
        step = 1;
        exitTick(w, o);
    }
    // 0x3b0690: position and facing at t = clamp(step / 3) (0x3eaaaaab); the lock ends when the
    // stick turns against the recorded one; after the last pose the player is released and walks
    // on (0x335d38) while the lock still holds and the move is not 75 to 105 degrees off his
    // facing. (The original releases in its step-3 tick; the port one step later. The positions
    // agree with the trace, run hc1.)
    void exitTick(World& w, Object& o) {
        if (lock.mode != 0 && lock.recX * w.stickX + lock.recY * w.stickY <= 0.0f) {
            lock.mode = 0;
        }
        if (step < 4) {
            const float t = std::min(1.0f, float(step) / 3.0f);
            bindPlayer(w, o, lerp(from, to, t), turn(faceFrom, faceTo, t));
            step++;
            return;
        }
        bool walkOn = false;
        if (lock.mode != 0) {
            V3 m{to.x - from.x, 0, to.z - from.z};
            const float len = std::sqrt(m.x * m.x + m.z * m.z);
            float a = 0;
            if (len > 1.0e-6f) {
                const float sn = std::fabs(m.x / len * faceTo.z - m.z / len * faceTo.x);
                a = std::asin(std::min(1.0f, sn)) * 57.2958f;
            }
            walkOn = !(a >= 75.0f && a <= 105.0f) && w.stickX * w.stickX + w.stickY * w.stickY >= 1.0e-6f;
        }
        release(w, to, faceTo, walkOn);
        setNerve(Nerve::Wait);   // the cooldown nerve 0x40b130 follows (5 ticks, not ported)
    }

    void control(World& w, Object& o) override {
        follow(w, o);
        const V3 toLadder = captain_toad::RecoveredVec::scale(front, -1.0f);   // the player faces the ladder
        if (dummyCheck(w, o)) {
            return;
        }
        switch (nerve) {
        case Nerve::Wait: {
            if (!valid(w, o) || w.bind.active || !w.playerFree || !w.playerOnGround) {
                return;
            }
            // The bind request (receiveMsg 0x3b0b70): BindBottom follows the first ladder piece,
            // BindTop the last one (init 0x3b29d8, 0x331578), both 50 up, r100.
            const SensorDecl* sb = o.sensor("BindBottom");
            const SensorDecl* st = o.sensor("BindTop");
            const V3 centreBottom = captain_toad::RecoveredVec::add(bottom, V3{0, sb ? sb->y : 50.0f, 0});
            const V3 centreTop = captain_toad::RecoveredVec::add(top, V3{0, st ? st->y : 50.0f, 0});
            int end = -1;   // 0 bottom, 1 top
            if (World::playerTouches(w.player, centreBottom, sb ? sb->radius : 100.0f) && grabTest(w, o, false)) {
                end = 0;
            } else if (World::playerTouches(w.player, centreTop, st ? st->radius : 100.0f) && grabTest(w, o, true)) {
                end = 1;
            }
            if (end >= 0) {
                from = w.player.pos;
                faceFrom = w.player.front;
                // 0x3b2758: attach at the nearer end: the first piece + F 65, or the last piece
                // - 50 up + F 65 (one rung below the top).
                fromTop = end == 1;
                attachBase = fromTop ? std::max(0.0f, height() - kRung) : 0.0f;
                play("Move");
                setNerve(Nerve::Hold);
                bindPlayer(w, o, from, faceFrom);
            }
            return;
        }
        case Nerve::Hold:   // the bind takes hold (2 ticks)
            bindPlayer(w, o, from, faceFrom);
            if (++step >= 2) {
                to = climbPos(attachBase);
                faceTo = toLadder;
                faceFrom = fixHalfTurn(faceFrom, faceTo);
                lock.record(w.stickX, w.stickY, !fromTop);   // the bind start (0x3b10b4)
                setNerve(Nerve::Attach);
            }
            return;
        case Nerve::Attach: {   // 0x3b21d0
            if (step == 0) {
                if (lock.mode == 2) {   // a top grab with the stick held
                    dummyValid = true;
                }
                dummyRadius = playerSensors(w.player.mini)[0].radius;
            }
            lock.update(w.stickX, w.stickY);
            const float t = float(step + 1) / 5.0f;
            bindPlayer(w, o, lerp(from, to, t), turn(faceFrom, faceTo, t));
            if (++step > 4) {
                dummyValid = false;
                base = attachBase;
                setNerve(Nerve::ClimbWait);
                step = -1;   // one more held tick before the first ClimbWait decision
            }
            return;
        }
        case Nerve::ClimbWait:   // 0x3b1fbc
            lock.update(w.stickX, w.stickY);
            if (step <= 0 && clip != "LadderClimbWait" && lock.result == 0) {
                play("LadderClimbWait");
            }
            bindPlayer(w, o, climbPos(base), toLadder);
            dash = w.dashHeld;   // 0x268180: the pad's dash button (pad vtable +0x50)
            if (step++ < 0) {
                return;
            }
            if (lock.result == 1 || lock.result == 2) {
                if (lock.mode == 0) {
                    lock.recX = w.stickX;
                    lock.recY = w.stickY;
                }
                setNerve(lock.result == 1 ? Nerve::ClimbUp : Nerve::ClimbDown);
            }
            return;
        case Nerve::ClimbUp: {   // 0x3b13c0: 5 steps a rung, 3 with dash
            lock.update(w.stickX, w.stickY);
            const int n = dash ? 3 : 5;
            if (step == 0) {
                clip.clear();
                play(dash ? "LadderClimbUpDash" : "LadderClimbUp");
            }
            if (step < n) {
                bindPlayer(w, o, climbPos(base + kRung * float(step + 1) / float(n)), toLadder);
                step++;
                return;
            }
            base += kRung;
            if (base >= height() - 0.001f) {   // over the top: the exit onto the ledge
                lock.mode = 1;
                startExit(w, o, climbPos(base),
                          captain_toad::RecoveredVec::add(axisAt(height() + kRung),
                                                          captain_toad::RecoveredVec::scale(toLadder, 5.0f)),
                          toLadder);
                return;
            }
            bindPlayer(w, o, climbPos(base), toLadder);
            setNerve(Nerve::ClimbWait);
            return;
        }
        case Nerve::ClimbDown: {   // 0x3b1b04: 4 steps a rung, 1 (in two halves) with dash
            lock.update(w.stickX, w.stickY);
            if (step == 0) {
                if (!dash) {
                    clip.clear();
                    play("LadderClimbDown");
                } else {
                    play("LadderClimbDownDash");   // not restarted while it plays
                }
                dummyValid = true;
            }
            const int n = dash ? 1 : 4;
            float dy;
            if (dash) {
                dy = step < n ? -kRung / float(n + 1) * float(step + 1) : -kRung;
            } else {
                dy = -kRung * std::max(0.0f, std::min(1.0f, float(step) / float(n)));
            }
            const V3 at = climbPos(base + dy);
            bindPlayer(w, o, at, toLadder);
            if (at.y <= bottom.y) {   // at the foot: the exit (dash: the landing first)
                base = 0;
                lock.mode = 2;
                const V3 target =
                    captain_toad::RecoveredVec::add(climbPos(0), captain_toad::RecoveredVec::scale(front, kBottomStep));
                if (dash) {
                    from = at;
                    to = target;
                    faceFrom = fixHalfTurn(toLadder, front);
                    faceTo = front;
                    setNerve(Nerve::DashLand);
                } else {
                    startExit(w, o, at, target, front);
                }
                return;
            }
            if (step < n) {
                step++;
                return;
            }
            base += dy;
            if (w.dashHeld && lock.result != 1) {   // keep sliding down
                dash = true;
                setNerve(Nerve::ClimbDown);
                return;
            }
            dash = false;
            dummyValid = false;
            setNerve(Nerve::ClimbWait);
            return;
        }
        case Nerve::DashLand: {   // 0x3b0e84
            if (step == 0) {
                // Ground below the feet (pos + 10 up, 20 down)? Otherwise let go at once.
                const V3 p = w.bind.pos;
                if (w.segmentHits && !w.segmentHits(captain_toad::RecoveredVec::add(p, V3{0, 10, 0}), V3{0, -20, 0})) {
                    release(w, p, w.bind.front, false);
                    setNerve(Nerve::Wait);
                    return;
                }
                play("Land");
            }
            const float len = std::sqrt(w.stickX * w.stickX + w.stickY * w.stickY);
            if (len < 0.1f) {
                lock.mode = 0;
            } else if (lock.mode != 0 && step > 5) {
                startExit(w, o, from, to, faceTo);
                return;
            }
            bindPlayer(w, o, w.bind.pos, w.bind.front);
            const float frames = w.playerClipFrames ? w.playerClipFrames("Land") : 0.0f;
            if (clipFrame - 2.0f >= frames) {   // the clip has ended
                if (lock.mode == 0) {
                    release(w, w.bind.pos, w.bind.front, false);
                    setNerve(Nerve::Wait);
                    return;
                }
                startExit(w, o, from, to, faceTo);
                return;
            }
            step++;
            return;
        }
        case Nerve::Exit:
            exitTick(w, o);
            return;
        }
    }
};

inline const Register<Ladder> kLadder{"Ladder"};

}   // namespace captain_toad::RecoveredStageLadder
