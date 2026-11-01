// =============================================================================================
// StageEnemyChase.hpp - an enemy's chase: sight, run, path memory and the way back (StageEnemies.hpp)
// =============================================================================================
//
// What the player sees: an enemy that spots the protagonist stops, looks at him ("Find") and runs
// after him. It stops at ledges instead of running off them. When it loses him it waits, or after
// a long chase shrugs ("GiveUp"), then walks back the way it came and steps back onto its rail.
//
// The chase component (vtable 0x4c33dc, start 0x190990) is shared by the enemies that chase; the
// actor runs it from one of its nerves (HeyhoRail: 0x3a7628) and reads its end. Its fields (comp):
//   +0x14 sight (vtable at 0x2dcfd8 / 0x2cf5c0), +0x18 the ChaseArea link, +0x1c the run mover,
//   +0x20 the return mover, +0x2c the memory count (MemoryNum + 2), +0x38 the memory points (16
//   bytes: position, fixed flag), +0x3c the point walked to, +0x40 the memory index, +0x44 the
//   chase steps, +0x34 the return clock, +0x24 a search wait, +0x28 resume (after an attack),
//   +0x2a fresh (the next start records a new home), +0x30 a fixed home (point-route enemies).
// Parameters (traced at 0x33289248 for obj979): Heyho's blocks 0x4ff70c (run), 0x4ff780 (return),
// 0x4ff6ec / 0x4ff6fc (physics), MemoryNum 5, MemoryDist 300 (placement).
//
// Nerves (keeper comp+4; exec addresses):
//   Find 0x41f730       "Find"; stops; turns to the player by 6 degrees a tick; at the clip's end
//                       -> Chase.
//   Chase 0x190c0c      runs the run mover (per-nerve component 0x33289314, enter 0x19175c) and
//                       records the path (memory, below). Ends with the mover, or when the player
//                       leaves the ChaseArea while the enemy is outside it too: more than 150 chase
//                       steps -> GiveUpStart, otherwise -> Wait.
//   Wait 0x41f828       "Wait"; spotted -> Find; after step 24 -> Return.
//   GiveUpStart 0x41f99c "GiveUpStart" -> at its end GiveUp 0x41f944 ("GiveUp", 25 steps) ->
//                       GiveUpEnd 0x41f8b8 ("GiveUpEnd"; spotted -> Find; at its end -> Return).
//   Return 0x1912c0     every 10 steps tries the next memory point; the first one in sight (a ray
//                       5 above the enemy) -> TurnTo. Runs the return mover (0x426104).
//   TurnTo 0x190814     "Turn"; turns to the point by 6 degrees a tick -> WalkTo. Past step 60
//                       (or near it / near the rail) it counts as arrived.
//   WalkTo 0x191100     "Walk"; walks to the point (0x2bff70: turn 6, accel 0.13, gravity 3 in the
//                       air, damping 0.98); spotted -> Find; within 50 (or within 50 of the rail)
//                       -> Settle.
//   Settle 0x1906bc     moves the rest of the way, pos += (point - pos) * step / 50, for 50 steps;
//                       spotted -> Find. At the home point the component ends (0x2c0204: back on
//                       the rail at the home point); otherwise the point is dropped -> Return.
// Memory (Chase): point 0 is where the chase began (on the rail). Point 1 follows the enemy while
// point 0 is in sight from it (or the enemy is within 150 of the rail), then stays. Each further
// point follows the enemy until the previous one goes out of sight or lies more than MemoryDist
// away (horizontally), then stays; at most MemoryNum of them.
//
// Spotting (0x2c02f0): the sight holds the player in view, he is inside the ChaseArea, and a ray
// from 50 above the enemy to 50 above him hits nothing.
// =============================================================================================
#pragma once

#include "game/ActorCollider.hpp"

#include "game/StageObjects.hpp"

#include <cfloat>
#include <cmath>

namespace captain_toad::RecoveredStageChase {

using namespace captain_toad::RecoveredStage;

constexpr float kDegree = 0.0174532924f;
inline const V3 kUp{0, 1, 0};
inline const V3 kGravity{0, -1, 0};   // the pose's gravity direction (pose vtable +0xc: 0x536e30)

inline V3 horizontal(V3 v) {
    v.y = 0;
    return v;
}
inline float length(const V3& v) {
    return std::sqrt(captain_toad::RecoveredVec::dot(v, v));
}

// A rail link: its points, positions along it and the nearest point to a position (0x2c3018).
struct RailPath {
    std::vector<V3> points;             // a closed rail repeats its first point at the end
    std::vector<float> segmentLength;   // [i]: of the segment from points[i - 1] to points[i]
    std::vector<float> start;           // start[i]: the distance of points[i] along the rail
    float total = 0;
    bool closed = false;

    explicit RailPath(std::vector<V3> p = {}, bool isClosed = false) : points(std::move(p)) {
        closed = isClosed && points.size() > 2;
        if (closed) {
            points.push_back(points.front());
        }
        segmentLength.assign(points.size(), 0.0f);
        start.assign(points.size(), 0.0f);
        for (size_t i = 1; i < points.size(); i++) {
            segmentLength[i] =
                captain_toad::RecoveredStageChase::length(captain_toad::RecoveredVec::sub(points[i], points[i - 1]));
            total += segmentLength[i];
            start[i] = total;
        }
    }
    bool empty() const { return points.empty(); }
    // A distance along a closed rail taken round the loop.
    float wrap(float d) const {
        if (!closed || total <= 0) {
            return d;
        }
        d = std::fmod(d, total);
        return d < 0 ? d + total : d;
    }
    // The segment holding distance d (the last for distances beyond the end) and d within it.
    size_t segment(float& d) const {
        d = wrap(d);
        for (size_t i = 1; i < points.size(); i++) {
            if (d <= segmentLength[i] || i + 1 == points.size()) {
                return i;
            }
            d -= segmentLength[i];
        }
        return 0;
    }
    V3 at(float d) const {
        const size_t i = segment(d);
        if (i == 0) {
            return points.empty() ? V3{} : points.back();
        }
        const float l = segmentLength[i];
        const float t = l > 0 ? std::min(std::max(d / l, 0.0f), 1.0f) : 0.0f;
        return captain_toad::RecoveredVec::add(
            points[i - 1],
            captain_toad::RecoveredVec::scale(captain_toad::RecoveredVec::sub(points[i], points[i - 1]), t));
    }
    V3 tangent(float d) const {
        const size_t i = segment(d);
        if (i == 0) {
            return {0, 0, 1};
        }
        V3 t = captain_toad::RecoveredVec::sub(points[i], points[i - 1]);
        captain_toad::RecoveredVec::normalize(t);
        return t;
    }
    // The direction of travel from d: the segment ahead (forward) or behind, reversed.
    V3 heading(float d, bool forward) const {
        const float eps = 0.01f;
        if (forward) {
            return tangent(closed ? d + eps : std::min(d + eps, total));
        }
        return captain_toad::RecoveredVec::scale(tangent(closed ? d - eps : std::max(d - eps, 0.0f)), -1.0f);
    }
    // The distance from d to the next rail point in the direction of travel (the rail mover's
    // 0x3fc678 result as Walk 0x3a70b4 reads it).
    float toNextPoint(float d, bool forward) const {
        d = wrap(d);
        if (forward) {
            for (float c : start) {
                if (c > d + 1e-3f) {
                    return c - d;
                }
            }
            return closed && start.size() > 1 ? total - d + start[1] : 0.0f;
        }
        for (size_t k = start.size(); k-- > 0;) {
            if (start[k] < d - 1e-3f) {
                return d - start[k];
            }
        }
        return closed && start.size() > 1 ? d + total - start[start.size() - 2] : 0.0f;
    }
    // The distance along the rail of the point nearest to p.
    float nearest(const V3& p) const {
        float best = 0, bestD = 1e30f, acc = 0;
        for (size_t i = 1; i < points.size(); i++) {
            const V3 a = points[i - 1], ab = captain_toad::RecoveredVec::sub(points[i], a);
            const float l2 = captain_toad::RecoveredVec::dot(ab, ab), l = std::sqrt(l2);
            float t = l2 > 0 ? captain_toad::RecoveredVec::dot(captain_toad::RecoveredVec::sub(p, a), ab) / l2 : 0;
            t = std::min(std::max(t, 0.0f), 1.0f);
            const float d = length(captain_toad::RecoveredVec::sub(
                p, captain_toad::RecoveredVec::add(a, captain_toad::RecoveredVec::scale(ab, t))));
            if (d < bestD) {
                bestD = d;
                best = acc + t * l;
            }
            acc += l;
        }
        return best;
    }
    V3 nearestPoint(const V3& p) const { return at(nearest(p)); }
};

// The pose of a moving enemy: position, facing (horizontal), velocity and its collider.
struct Body {
    V3 pos;
    V3 front{0, 0, 1};
    V3 vel;
    static constexpr float kColliderRadius = 80.0f, kColliderUp = 80.0f;   // InitCollider (Heyho)
    captain_toad::RecoveredActorCollider::ActorCollider collider{kColliderRadius, kColliderUp};

    float yawDegrees() const { return std::atan2(front.x, front.z) / kDegree; }
    void stop() { vel = {0, 0, 0}; }   // 0x2f9718

    // 0x28b804: turn the front toward dir (both about the gravity axis) by at most maxDegrees;
    // true once it faces dir.
    bool turnToward(V3 dir, float maxDegrees) {
        dir = horizontal(dir);
        if (captain_toad::RecoveredVec::dot(dir, dir) < 1e-6f) {
            return false;
        }
        captain_toad::RecoveredVec::normalize(dir);
        V3 f = horizontal(front);
        if (captain_toad::RecoveredVec::dot(f, f) < 1e-6f) {
            f = captain_toad::RecoveredVec::scale(dir, -1.0f);
        }
        captain_toad::RecoveredVec::normalize(f);
        // Facing once the remaining angle is below the step (dot > cos, strict; c <= -1 always faces).
        const float c = std::cos(maxDegrees * kDegree), s = std::sin(maxDegrees * kDegree);
        if (c <= -1.0f || captain_toad::RecoveredVec::dot(f, dir) > c) {
            front = dir;
            return true;
        }
        V3 side = captain_toad::RecoveredVec::sub(
            dir, captain_toad::RecoveredVec::scale(f, captain_toad::RecoveredVec::dot(f, dir)));
        if (captain_toad::RecoveredVec::dot(side, side) < 1e-6f) {
            // Straight behind: a nudge of 0.02 along f x gravity (0x2fd3ec), not a full step. This
            // makes a 180-degree turn at 6 a tick take 31 ticks (traced, run hc6).
            side = captain_toad::RecoveredVec::cross(f, kGravity);
            captain_toad::RecoveredVec::normalize(side);
            front = captain_toad::RecoveredVec::add(f, captain_toad::RecoveredVec::scale(side, 0.02f));
            captain_toad::RecoveredVec::normalize(front);
            return false;
        }
        captain_toad::RecoveredVec::normalize(side);
        front = captain_toad::RecoveredVec::add(captain_toad::RecoveredVec::scale(f, c),
                                                captain_toad::RecoveredVec::scale(side, s));
        captain_toad::RecoveredVec::normalize(front);
        return false;
    }

    // 0x2f9bf4: on the ground by the collider (with `graceTicks` in the air), not moving off it.
    bool onGround(unsigned graceTicks = 0) const { return collider.onGround(vel, graceTicks); }

    // 0x2dd288 with a physics block {gravity, air damping, ground damping}: on the ground only the
    // damping, plus gravity while standing on an edge or corner (0x327d28); in the air gravity
    // then damping.
    void physics(float gravity, float airDamping, float groundDamping) {
        if (!onGround()) {
            vel = captain_toad::RecoveredVec::add(vel, captain_toad::RecoveredVec::scale(kGravity, gravity));
            vel = captain_toad::RecoveredVec::scale(vel, airDamping);
        } else {
            if (collider.onFloorEdge()) {
                vel = captain_toad::RecoveredVec::add(vel, captain_toad::RecoveredVec::scale(kGravity, gravity));
            }
            vel = captain_toad::RecoveredVec::scale(vel, groundDamping);
        }
    }

    // The actor movement 0x2a9124: the collider's move for the velocity (ActorCollider.hpp).
    void integrate(World& w) {
        if (!w.sphereContacts) {
            pos = captain_toad::RecoveredVec::add(pos, vel);
            return;
        }
        pos = captain_toad::RecoveredVec::add(pos, collider.move(w.sphereContacts, pos, kUp, vel));
    }
    // Moving without collision (on the rail): the collider follows (0x2a9124's other path, 0x2f9fec).
    void followWithoutCollision() { collider.reset(pos, kUp); }

    // 0x2c0048: does the course block the line from h above the enemy to h above the point?
    bool lineBlocked(const World& w, const V3& point, float h) const {
        const V3 d = captain_toad::RecoveredVec::sub(point, pos);
        if (captain_toad::RecoveredVec::dot(d, d) < 1e-6f || !w.segmentHits) {
            return false;
        }
        return w.segmentHits(captain_toad::RecoveredVec::add(pos, captain_toad::RecoveredVec::scale(kUp, h)), d);
    }

    // 0x1da250 (through 0x2b4200, ahead 50, up 150, down 150): moving horizontally and no ground
    // within 150 above or below the point 50 ahead.
    bool ledgeAhead(const World& w) const {
        V3 h = horizontal(vel);
        if (captain_toad::RecoveredVec::dot(h, h) < 1e-6f || !w.segmentHits) {
            return false;
        }
        captain_toad::RecoveredVec::normalize(h);
        const V3 ahead = captain_toad::RecoveredVec::add(pos, captain_toad::RecoveredVec::scale(h, 50.0f));
        return !w.segmentHits(captain_toad::RecoveredVec::add(ahead, V3{0, 150.0f, 0}), V3{0, -300.0f, 0});
    }
};

// The enemy's sight (0x2dcfd8 / search 0x2cf5c0 / measure 0x2cf858). A negative height limit
// is no limit (0x2dcfd8 tests them only when >= 0); a negative keep distance never keeps.
struct Sight {
    struct Params {   // Heyho's block 0x4ff6c8 by default
        float view = 800, facingDegrees = 70, elevationDegrees = 85;
        float closeRange = 750, maxAbove = 500, maxBelow = 100, keep = 1500;
    };
    Params p;

    bool target = false;    // +4: the player is the target
    bool inView = false;    // +8
    bool visible = false;   // +9
    bool found = false;     // +10: a search found the target this update
    bool wide = false;      // +0xb: the chase skips the facing test
    int cooldown = 0;       // +0x20: updates skipped before the next search

    struct Measure {
        V3 d;   // the player relative to the enemy
        float dist = 0;
        float facing = 0;      // [4]
        float elevation = 0;   // [5]
    };
    // 0x2cf858: h is the gravity direction with the front's part removed; elevation = |sin|
    // of the angle between h and the direction to the player (the cosine of the elevation);
    // facing = front . normalize(dir - h * elevation).
    static bool measure(const Body& b, const V3& at, Measure& m) {
        m.d = captain_toad::RecoveredVec::sub(at, b.pos);
        const float d2 = captain_toad::RecoveredVec::dot(m.d, m.d);
        if (d2 < 0.001f) {
            return false;
        }
        m.dist = std::sqrt(d2);
        V3 h = captain_toad::RecoveredVec::sub(
            kGravity, captain_toad::RecoveredVec::scale(b.front, captain_toad::RecoveredVec::dot(b.front, kGravity)));
        if (captain_toad::RecoveredVec::dot(h, h) < 1e-6f) {
            return false;
        }
        captain_toad::RecoveredVec::normalize(h);
        const V3 dn = captain_toad::RecoveredVec::scale(m.d, 1.0f / m.dist);
        const float c = captain_toad::RecoveredVec::dot(h, dn);
        m.elevation = std::fabs(std::fabs(c) - 1.0f) < 0.001f ? 0.0f : std::sqrt(std::max(0.0f, 1.0f - c * c));
        V3 p = captain_toad::RecoveredVec::sub(dn, captain_toad::RecoveredVec::scale(h, m.elevation));
        if (captain_toad::RecoveredVec::dot(p, p) < 1e-6f) {
            m.facing = 0;
        } else {
            captain_toad::RecoveredVec::normalize(p);
            m.facing = captain_toad::RecoveredVec::dot(b.front, p);
        }
        return true;
    }
    bool anglesPass(const Measure& m) const {
        if (!wide && m.facing < std::cos(p.facingDegrees * kDegree)) {
            return false;
        }
        if (m.elevation < std::cos(p.elevationDegrees * kDegree)) {
            return false;
        }
        if (m.d.y < 0) {
            return p.maxBelow < 0 || -m.d.y <= p.maxBelow;
        }
        return p.maxAbove < 0 || m.d.y <= p.maxAbove;
    }
    void update(const World& w, const Body& b) {
        if (cooldown > 0) {
            cooldown--;
            return;
        }
        found = false;
        Measure m;
        if (target && !w.playerDown && measure(b, w.player.pos, m) && anglesPass(m)) {
            if (m.dist <= p.view) {
                inView = true;
                visible = true;
                return;
            }
            if (m.dist <= p.keep) {
                inView = false;
                visible = true;
                return;
            }
        }
        search(w, b);
    }
    // 0x2cf5c0: the nearest player in view, or within the close range out of view (target but
    // not in view); without one the next search waits distance x 0.005 updates.
    void search(const World& w, const Body& b) {
        float best = FLT_MAX;
        bool any = false, anyInView = false;
        Measure m;
        if (!w.playerDown && measure(b, w.player.pos, m)) {   // a missing player is skipped (0x2d76d0)
            if (m.dist <= p.view && anglesPass(m)) {
                best = m.dist;
                any = anyInView = true;
            } else if (m.dist < p.closeRange) {
                best = m.dist;
                any = true;
            } else {
                best = m.dist;
            }
        }
        target = any;
        if (any) {
            found = true;
        }
        if (anyInView) {
            inView = visible = true;
            cooldown = 0;
        } else {
            inView = visible = false;
            if (best < FLT_MAX) {
                cooldown = int(best * 0.005f);
            }
        }
    }
};

// The ChaseArea link (AreaCubeBase, 0x2f62d4): x / z centred, y from the base.
struct Area {
    bool present = false;
    V3 translate, scale{1, 1, 1};
    explicit Area(const LinkTarget* t = nullptr) {
        if (!t) {
            return;
        }
        present = true;
        translate = t->translation();
        const M34& m = t->placement;
        scale = {std::sqrt(m.m[0] * m.m[0] + m.m[4] * m.m[4] + m.m[8] * m.m[8]),
                 std::sqrt(m.m[1] * m.m[1] + m.m[5] * m.m[5] + m.m[9] * m.m[9]),
                 std::sqrt(m.m[2] * m.m[2] + m.m[6] * m.m[6] + m.m[10] * m.m[10])};
    }
    bool contains(const V3& p) const {
        return std::fabs(p.x - translate.x) <= 500 * scale.x && p.y >= translate.y &&
               p.y <= translate.y + 1000 * scale.y && std::fabs(p.z - translate.z) <= 500 * scale.z;
    }
};

class Chaser {
public:
    enum class Nerve { Idle, Find, Chase, Wait, GiveUpStart, GiveUp, GiveUpEnd, Return, TurnTo, WalkTo, Settle };

    // The run mover (vtable 0x4cae30) and its parameters (0x4ff70c, physics 0x4ff6ec).
    static constexpr float kRunAccel = 1.35f, kRunTurn = 5.0f;
    static constexpr int kRunLostSteps = 60, kRunMaxSteps = 500, kStopSteps = 5;
    static constexpr float kFall = 3.0f, kAirDamping = 0.98f, kGroundDamping = 0.9f;
    static constexpr float kReturnGroundDamping = 0.85f;           // the return mover's block 0x4ff6fc
    static constexpr int kReturnMoverSteps = 30;                   // 0x4ff780 [0]
    static constexpr float kWalkAccel = 0.13f, kWalkTurn = 6.0f;   // 0x191274 -> 0x2bff70
    static constexpr float kTurn = 6.0f;                           // Find, TurnTo, the actor's Attack
    static constexpr int kGiveUpChaseSteps = 150;

    Sight sight;
    Area area;
    bool resume = false;   // +0x28: the next start goes straight back to Chase (after an attack)
    bool ended = false;    // the component finished (vtable +0x10)

    Chaser() = default;
    Chaser(const Object& o, const LinkTarget* chaseArea) : area(chaseArea) {
        auto num = o.numbers.find("MemoryNum");
        auto dist = o.numbers.find("MemoryDist");
        memoryNum_ = num != o.numbers.end() ? int(num->second) : 5;
        memoryDist_ = dist != o.numbers.end() ? float(dist->second) : 300.0f;
        memory_.assign(size_t(memoryNum_ + 2), {});
    }
    Nerve nerve() const { return nerve_; }

    // 0x2c02f0.
    bool spotted(const World& w, const Body& b) {
        sight.update(w, b);
        if (!sight.target || !sight.inView) {
            return false;
        }
        if (area.present && !area.contains(w.player.pos)) {
            return false;
        }
        return !b.lineBlocked(w, w.player.pos, 50.0f);
    }

    // 0x190990.
    void start(const Body& b, const RailPath* rail) {
        ended = false;
        if (resume) {
            resume = false;
            setNerve(Nerve::Chase);
            return;
        }
        if (fresh_) {
            memoryIndex_ = 0;
            for (auto& m : memory_) {
                m.fixed = false;
            }
            if (rail && !rail->empty()) {
                memory_[0].pos = b.pos;
                memory_[0].fixed = true;
            }
            memory_[1].pos = memory_[0].pos;
            fresh_ = false;
        }
        setNerve(Nerve::Find);
    }

    // One step of the component (run by the actor's chase nerve).
    void tick(World& w, Object& o, Body& b, const RailPath* rail) {
        switch (nerve_) {
        case Nerve::Idle:
            return;
        case Nerve::Find:   // 0x41f730
            if (step_ == 0) {
                o.startAction("Find");
                b.stop();
            }
            b.turnToward(captain_toad::RecoveredVec::sub(w.player.pos, b.pos), kTurn);
            if (step_ > 0 && o.isActionEnd()) {
                setNerve(Nerve::Chase);
                return;
            }
            break;
        case Nerve::Chase:
            if (chaseTick(w, o, b, rail)) {
                return;
            }
            break;
        case Nerve::Wait:   // 0x41f828
            if (step_ == 0) {
                o.startAction("Wait");
                b.stop();
                searchWait_ = 0;
            }
            searchWait_--;
            if (spotted(w, b)) {
                setNerve(Nerve::Find);
                return;
            }
            if (step_ > 24) {
                setNerve(Nerve::Return);
                return;
            }
            break;
        case Nerve::GiveUpStart:   // 0x41f99c
            if (step_ == 0) {
                o.startAction("GiveUpStart");
                b.stop();
                searchWait_ = 0;
            }
            searchWait_--;
            if (step_ > 0 && o.isActionEnd()) {
                setNerve(Nerve::GiveUp);
                return;
            }
            break;
        case Nerve::GiveUp:   // 0x41f944
            if (step_ == 0) {
                o.startAction("GiveUp");
            }
            searchWait_--;
            if (step_ > 24) {
                setNerve(Nerve::GiveUpEnd);
                return;
            }
            break;
        case Nerve::GiveUpEnd:   // 0x41f8b8
            if (step_ == 0) {
                o.startAction("GiveUpEnd");
            }
            searchWait_--;
            if (spotted(w, b)) {
                setNerve(Nerve::Find);
                return;
            }
            if (step_ > 0 && o.isActionEnd()) {
                setNerve(Nerve::Return);
                return;
            }
            break;
        case Nerve::Return:
            if (returnTick(w, o, b, rail)) {
                return;
            }
            break;
        case Nerve::TurnTo:   // 0x190814
            if (chaseSteps_ == 1 || step_ > 60) {
                bool arrived = false;
                if (rail && !rail->empty()) {
                    const V3 p = rail->nearestPoint(b.pos);
                    if (length(captain_toad::RecoveredVec::sub(b.pos, p)) < 50.0f) {
                        memory_[0].pos = p;
                        target_ = 0;
                        arrived = true;
                    }
                }
                if (!arrived && target_ >= 0 &&
                    length(captain_toad::RecoveredVec::sub(b.pos, memory_[size_t(target_)].pos)) < 50.0f) {
                    arrived = true;
                }
                if (arrived) {
                    if (target_ != 0) {
                        dropTarget();
                        setNerve(Nerve::Return);
                    } else {
                        finish(b);
                    }
                    return;
                }
            }
            if (step_ == 0) {
                o.startAction("Turn");
            }
            b.stop();
            if (b.turnToward(captain_toad::RecoveredVec::sub(targetPos(), b.pos), kTurn)) {
                setNerve(Nerve::WalkTo);
                return;
            }
            break;
        case Nerve::WalkTo:   // 0x191100
            if (step_ == 0) {
                b.stop();
                o.startAction("Walk");
            }
            if (spotted(w, b)) {
                setNerve(Nerve::Find);
                return;
            }
            if (rail && !rail->empty()) {
                const V3 p = rail->nearestPoint(b.pos);
                if (length(captain_toad::RecoveredVec::sub(b.pos, p)) < 50.0f) {
                    memory_[0].pos = p;
                    target_ = 0;
                    setNerve(Nerve::Settle);
                    return;
                }
            }
            if (length(captain_toad::RecoveredVec::sub(b.pos, targetPos())) < 50.0f) {
                setNerve(Nerve::Settle);
                return;
            }
            walkStep(b, captain_toad::RecoveredVec::sub(targetPos(), b.pos));
            break;
        case Nerve::Settle: {   // 0x1906bc
            if (step_ == 0) {
                b.stop();
            }
            const float t = std::min(std::max(float(step_) / 50.0f, 0.0f), 1.0f);
            const V3 p = captain_toad::RecoveredVec::add(
                b.pos, captain_toad::RecoveredVec::scale(captain_toad::RecoveredVec::sub(targetPos(), b.pos), t));
            if (spotted(w, b)) {
                setNerve(Nerve::Find);
                return;
            }
            if (step_ < 50) {
                b.pos = p;
                break;
            }
            if (target_ == 0) {
                finish(b);
                return;
            }
            b.pos = targetPos();
            dropTarget();
            setNerve(Nerve::Return);
            return;
        }
        }
        step_++;
    }

private:
    struct Memory {
        V3 pos;
        bool fixed = false;
    };
    enum class Mover { None, Run, Stop, Return };

    Nerve nerve_ = Nerve::Idle;
    int step_ = 0;
    int memoryNum_ = 5;
    float memoryDist_ = 300;
    std::vector<Memory> memory_ = std::vector<Memory>(7);
    int target_ = -1;       // +0x3c
    int memoryIndex_ = 0;   // +0x40
    int chaseSteps_ = 0;    // +0x44
    int returnClock_ = 0;   // +0x34
    int searchWait_ = 0;    // +0x24
    bool fresh_ = true;     // +0x2a
    // The run mover's state.
    Mover mover_ = Mover::None;
    int moverStep_ = 0;
    bool moverEnded_ = false;
    int lostSteps_ = 0;        // [9]
    bool following_ = false;   // [11]: steering along a wall
    V3 wallNormal_;            // [12..14]
    V3 steer_;                 // [15..17]

    void setNerve(Nerve n) {
        nerve_ = n;
        step_ = 0;
    }
    V3 targetPos() const { return target_ >= 0 ? memory_[size_t(target_)].pos : V3{}; }
    void dropTarget() {
        if (target_ >= 0) {
            memory_[size_t(target_)].fixed = false;
        }
        target_ = -1;
    }
    // 0x2c0204: back at the home point (on the rail there), the memory cleared.
    void finish(Body& b) {
        b.pos = memory_[0].pos;
        for (auto& m : memory_) {
            m.fixed = false;
        }
        memoryIndex_ = 0;
        target_ = -1;
        fresh_ = true;
        ended = true;
        setNerve(Nerve::Idle);
    }

    // 0x31f01c with turn 6, acceleration 0.13, gravity 3 and damping 0.98 (block 0x4ff6ec).
    static void walkStep(Body& b, const V3& dir) {
        b.turnToward(dir, kWalkTurn);
        b.vel = captain_toad::RecoveredVec::add(b.vel, captain_toad::RecoveredVec::scale(b.front, kWalkAccel));
        if (!b.onGround(3)) {   // 0x2f9bf4 with 3 ticks of grace (0x31f16c)
            b.vel = captain_toad::RecoveredVec::add(
                b.vel, captain_toad::RecoveredVec::scale(captain_toad::RecoveredStageChase::kGravity, kFall));
        }
        b.vel = captain_toad::RecoveredVec::scale(b.vel, kAirDamping);
    }

    // 0x190c0c. True when the nerve changed.
    bool chaseTick(World& w, Object& o, Body& b, const RailPath* rail) {
        if (step_ == 0) {
            b.stop();
            sight.wide = true;
            chaseSteps_ = 0;
            startMover(Mover::Run);
        }
        chaseSteps_++;
        recordPath(w, b, rail);
        bool over = moverEnded_;   // as the mover left it last tick
        if (!over && area.present && sight.target && sight.visible && !area.contains(w.player.pos) &&
            !area.contains(b.pos)) {
            over = true;   // the player left the area and the enemy is outside it (MoveLimit not ported)
        }
        if (over) {
            sight.wide = false;
            setNerve(chaseSteps_ > kGiveUpChaseSteps ? Nerve::GiveUpStart : Nerve::Wait);
            return true;
        }
        moverTick(w, o, b);
        return false;
    }

    void recordPath(const World& w, const Body& b, const RailPath* rail) {
        bool nearRail = false;
        if (rail && !rail->empty()) {
            V3 p = rail->nearestPoint(b.pos);
            p.y = b.pos.y;
            nearRail = length(captain_toad::RecoveredVec::sub(b.pos, p)) < 150.0f;
        }
        if (!memory_[1].fixed) {
            if (nearRail || !b.lineBlocked(w, memory_[0].pos, 5.0f)) {
                memory_[1].pos = b.pos;
            } else {
                memory_[1].fixed = true;
            }
            return;
        }
        if (memoryIndex_ >= memoryNum_) {
            return;
        }
        Memory& slot = memory_[size_t(memoryIndex_ + 2)];
        if (nearRail) {
            slot.pos = b.pos;
            return;
        }
        if (b.lineBlocked(w, memory_[size_t(memoryIndex_ + 1)].pos, 5.0f)) {
            slot.fixed = true;
            memoryIndex_++;
            return;
        }
        const float toLast =
            length(horizontal(captain_toad::RecoveredVec::sub(b.pos, memory_[size_t(memoryIndex_ + 1)].pos)));
        if (toLast <= memoryDist_) {
            slot.pos = b.pos;
            return;
        }
        const float toBefore =
            length(horizontal(captain_toad::RecoveredVec::sub(b.pos, memory_[size_t(memoryIndex_)].pos)));
        if (memoryIndex_ <= 1 || toBefore > memoryDist_) {
            slot.fixed = true;
            memoryIndex_++;
        } else {
            slot.pos = b.pos;
        }
    }

    // 0x1912c0. True when the nerve changed.
    bool returnTick(World& w, Object& o, Body& b, const RailPath* rail) {
        const int count = int(memory_.size());
        if (step_ == 0) {
            b.stop();
            returnClock_ = 0;
            memory_[0].fixed = true;
            target_ = 0;
            startMover(Mover::Return);
        }
        if (rail && !rail->empty() && returnClock_ % 10 == 0) {
            int i = returnClock_ / 10;
            if (!memory_[size_t(i)].fixed) {
                returnClock_ = 0;
                i = 0;
            }
            if (i >= count) {
                i = 0;
            }
            target_ = i;
            if (!b.lineBlocked(w, memory_[size_t(i)].pos, 5.0f)) {
                for (int j = i + 1; j < count; j++) {
                    memory_[size_t(j)].fixed = false;
                }
                if (i < 2) {
                    memoryIndex_ = 0;
                } else if (i < count - 1) {
                    memoryIndex_ = i - 1;
                }
                setNerve(Nerve::TurnTo);
                return true;
            }
            if (i == 0 && !memory_[1].fixed) {
                memory_[0].pos = rail->nearestPoint(b.pos);
                memory_[0].fixed = true;
            }
        }
        if (++returnClock_ >= count * 10) {
            returnClock_ = 0;
        }
        searchWait_--;
        if (moverEnded_ && searchWait_ < 1 && sight.target && sight.inView) {
            if (!b.lineBlocked(w, w.player.pos, 50.0f) && (!area.present || area.contains(w.player.pos))) {
                setNerve(Nerve::Find);
                return true;
            }
            sight.target = false;
        }
        moverTick(w, o, b);
        return false;
    }

    void startMover(Mover m) {
        mover_ = m;
        moverStep_ = 0;
        moverEnded_ = false;
        if (m == Mover::Run) {   // enter 0x1f0294
            lostSteps_ = 0;
        }
    }

    // The per-nerve components (traced order, run ord: actor keeper, chase keeper with its nerve,
    // then the mover's keeper): a nerve reads the end its mover reported the tick before, and the
    // mover runs after the nerve unless the nerve changed (0x2380cc ends it at the keeper boundary).
    // The run mover (0x1f02b0, then its stop 0x42d560) and the return
    // mover (0x426104; its wandering after 30 steps, 0x1863c4, is not ported).
    void moverTick(World& w, Object& o, Body& b) {
        switch (mover_) {
        case Mover::None:
            return;
        case Mover::Run:
            if (runTick(w, o, b)) {
                return;
            }
            break;
        case Mover::Stop:   // 0x42d560
            if (moverStep_ == 0) {
                o.startAction("Wait");
            }
            b.physics(kFall, kAirDamping, kGroundDamping);
            if (moverStep_ > kStopSteps) {
                moverEnded_ = true;
                mover_ = Mover::None;
                return;
            }
            break;
        case Mover::Return:
            b.physics(kFall, kAirDamping, kReturnGroundDamping);
            if (moverStep_ > kReturnMoverSteps) {
                moverEnded_ = true;
                mover_ = Mover::None;
                return;
            }
            break;
        }
        moverStep_++;
    }
    void moverStop() {
        mover_ = Mover::Stop;
        moverStep_ = 0;
    }

    // 0x1f02b0 (path flag set). True when the mover changed.
    bool runTick(World& w, Object& o, Body& b) {
        if (moverStep_ == 0) {
            o.startAction("Run");
            following_ = false;
            wallNormal_ = {0, 0, 0};
        }
        sight.update(w, b);
        if (sight.found) {
            moverStop();
            return true;
        }
        if (!sight.target || !sight.visible) {
            if (++lostSteps_ >= 9) {
                moverStop();
                return true;
            }
        } else {
            steerToPlayer(w, b);
            if (b.onGround()) {   // 0x2f9bf4 with 0 (0x1f071c)
                b.vel = captain_toad::RecoveredVec::add(b.vel, captain_toad::RecoveredVec::scale(b.front, kRunAccel));
            }
            lostSteps_ = 0;
        }
        b.physics(kFall, kAirDamping, kGroundDamping);
        if (b.ledgeAhead(w)) {   // chase parameter +0x15: stop the forward motion at a ledge
            b.vel = captain_toad::RecoveredVec::sub(
                b.vel, captain_toad::RecoveredVec::scale(b.front, captain_toad::RecoveredVec::dot(b.front, b.vel)));
        }
        if (moverStep_ > kRunMaxSteps || (moverStep_ > kRunLostSteps && !sight.target)) {
            moverStop();
            return true;
        }
        return false;
    }

    // 0x2a28d0 probes 100 along the direction from 5 above the enemy; with a wall in the way the
    // run follows it (its normal turned by 90 degrees about the up axis, the side toward the
    // player) until a probe toward the wall finds none.
    bool probe(const World& w, const Body& b, const V3& dir, V3& normal) const {
        if (!w.segmentFirst) {
            return false;
        }
        float t = 0;
        return w.segmentFirst(captain_toad::RecoveredVec::add(b.pos, captain_toad::RecoveredVec::scale(kUp, 5.0f)),
                              captain_toad::RecoveredVec::scale(dir, 100.0f), normal, t);
    }
    void steerToPlayer(const World& w, Body& b) {
        V3 toPlayer = captain_toad::RecoveredVec::sub(w.player.pos, b.pos);
        captain_toad::RecoveredVec::normalize(toPlayer);
        V3 n;
        bool wall = probe(w, b, toPlayer, n);
        if (wall && following_) {
            wall = probe(w, b, captain_toad::RecoveredVec::scale(wallNormal_, -1.0f), n);
        }
        if (!wall) {
            wallNormal_ = {0, 0, 0};
            b.turnToward(captain_toad::RecoveredVec::sub(w.player.pos, b.pos), kRunTurn);
            following_ = false;
            return;
        }
        if (!following_ && length(captain_toad::RecoveredVec::sub(n, wallNormal_)) > 0.001f) {
            steer_ = {n.z, n.y, -n.x};   // the normal turned by 90 degrees about +Y
            if (captain_toad::RecoveredVec::dot(steer_, toPlayer) < 0) {
                steer_ = captain_toad::RecoveredVec::scale(steer_, -1.0f);
            }
            wallNormal_ = n;
        }
        b.turnToward(steer_, kRunTurn);
        following_ = true;
    }
};

}   // namespace captain_toad::RecoveredStageChase
