// =============================================================================================
// ActorCollider.hpp - the generic actor collider (enemies, turnips; StageObjects.hpp)
// =============================================================================================
//
// What it does: an actor with an InitCollider (Radius, offset Y) is a sphere that moves through
// the course. Each tick the actor's movement (0x2a9124) asks the collider for the move it may make
// (0x2a9348) and adds it to its position; afterwards the actor's logic reads what the sphere
// touched: the deepest floor, wall and ceiling contact, how long it has been in the air, the
// last ground normal.
//
// The move (0x2a9348), recovered:
//   - The sphere's centre is the position plus the offset along up (0x28d348). The collider
//     remembers last tick's centre and radius (+0x210, +0x21c; set at creation and by the
//     reset 0x2f9fec).
//   - Catch-up (0x32de84): when the actor was moved (its centre is no longer where the collider
//     left it) or its radius changed, the sphere sweeps from the old centre to the new one in steps
//     of at most min(0.9 r, 0.9 r_old, 35); each step that touches the course resolves every
//     contact collected so far and keeps the push.
//   - The move itself: a sweep along the velocity in steps of at most min(0.9 r_old, 35)
//     (0x32e028). At the first step that touches the course the contacts are resolved into one
//     push (0x281dd4), the sphere moves there plus the push, and the rest of the move slides along
//     the push direction (its part against it is removed); a rest that runs against the velocity
//     ends the move. The sweep continues with the rest until it reaches its end; contacts found at
//     the very end are resolved once more.
//   - Resolving (0x281dd4): every contact gives a push (0x3ef7a0): a face contact (kind 1) along
//     the face normal by its depth; an edge or corner contact along the direction from the contact
//     point to the centre (the face push projected onto it), unless a face contact of the same
//     class is among the new contacts (0x3ef87c: then the face normal). The pushes are combined per
//     axis: the largest positive plus the most negative component.
//   - Classes (0x281874 / 0x281804, against the gravity direction, threshold 0x3eaf1d44 = 0.342):
//     floor when the normal points up by more than 0.342 (cos 70), wall when |n . g| < 0.342,
//     ceiling otherwise. The deepest contact of each class is kept (0x281938; depths start at
//     -99999: "no contact" is a negative depth).
//   - Afterwards: the total push (+0x34), the new centre and radius, and the ground bookkeeping:
//     ticks in the air (+0x1fc, from 0 on the ground) and the last ground normal (+0x200).
// Queries (0x2b9aa0, flag 2 set by the constructor 0x32e10c): the course's sphere check with a
// direction (the rest of the sweep; for the catch-up the whole catch-up move), at most 32
// contacts in the buffer (0x24e3ac, copied by 0x282260).
// Not active in this port: parts whose +0x134 is zero (the flag the player collider models as
// partMoving; the terrain has it set, 0x17f) add their projected normal to the push (0x281dd4) and
// carry a sphere standing on them (0x3effe4). The port treats every part as flagged, as for the
// player (PlayerCollider.hpp), so both stay off.
// =============================================================================================
#pragma once

#include "game/Collision.hpp"
#include "game/VecLib.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace captain_toad::RecoveredActorCollider {

using captain_toad::RecoveredCollision::HitInfo;
using captain_toad::RecoveredVec::V3;

// The course's sphere check: contacts of the sphere (centre, radius) with their projections on
// `direction`, appended to `out`.
using SphereQuery = std::function<void(const V3& centre, float radius, const V3& direction, std::vector<HitInfo>& out)>;

class ActorCollider {
public:
    enum class Class { Floor, Wall, Ceiling };
    struct Contact {
        float depth = kNoContact;
        HitInfo hit;
        bool present() const { return depth >= 0.0f; }
    };

    ActorCollider() = default;
    ActorCollider(float radius, float offsetUp) : radius_(radius), offset_(offsetUp) {}

    // 0x2f9fec: the collider forgets its contacts and takes the current centre (after a teleport,
    // or each tick while the actor moves without collision); the air ticks become -1 (max), so the
    // actor is off the ground until its next move finds the floor.
    void reset(const V3& position, const V3& up) {
        clearContacts();
        airTicks_ = ~0u;
        previousCentre_ = centre(position, up);
        previousRadius_ = radius_;
    }

    // 0x2a9348: the move the actor may make this tick for `velocity` (to be added to its position).
    V3 move(const SphereQuery& query, const V3& position, const V3& up, const V3& velocity) {
        using namespace captain_toad::RecoveredVec;
        const V3 here = centre(position, up);
        V3 cur = previousCentre_;
        float curRadius = previousRadius_;
        const float catchUpStep =
            std::min(std::min(radius_ * kStepFraction, previousRadius_ * kStepFraction), kMaxStep);
        clearContacts();
        // Catch-up from where the collider left the sphere to where the actor put it.
        const V3 target = here;   // (+ the floor's carry, not active: see the header)
        const V3 catchUp = sub(target, previousCentre_);
        if (dot(catchUp, catchUp) >= kTiny || std::fabs(previousRadius_ - radius_) >= kRadiusTolerance) {
            Sweep a(previousCentre_, target, previousRadius_, radius_, catchUpStep);
            catchUpSweep(query, a, catchUp, cur, curRadius);
        }
        const float step = std::min(previousRadius_ * kStepFraction, kMaxStep);
        Sweep sweep(cur, add(cur, velocity), radius_, radius_, step);
        size_t found = sweepUntilContact(query, sweep);
        V3 delta;
        if (found == 0 && sweep.finished()) {
            delta = add(sub(cur, here), velocity);   // free move (0x2a9794): no classification
        } else {
            V3 total{};
            size_t resolved = 0;
            while (true) {
                V3 normal{};
                const V3 push = resolve(resolved, &normal);
                resolved += found;
                cur = sweep.position();
                curRadius = sweep.radius();
                V3 rest = sweep.rest();
                cur = add(cur, push);
                total = add(total, push);
                V3 slide = dot(push, push) < kTiny ? normal : push;
                if (dot(slide, slide) < kTiny) {
                    slide = {};
                } else {
                    normalize(slide);
                }
                const float against = dot(rest, slide);
                if (against < 0.0f) {
                    rest = add(rest, scale(slide, -against));
                }
                if (dot(velocity, rest) < 0.0f) {
                    break;
                }
                sweep = Sweep(cur, add(cur, rest), curRadius, radius_, step);
                sweep.advance();
                found = 0;
                bool ended = false;
                while (true) {
                    if (sweep.finished()) {
                        cur = sweep.position();
                        ended = true;
                        break;
                    }
                    if (const size_t n = queryAt(query, sweep.position(), sweep.radius(), sweep.rest())) {
                        found = n;
                        break;
                    }
                    sweep.advance();
                }
                if (ended) {
                    break;
                }
                if (sweep.t < 1.0f) {
                    continue;   // a contact before the end: resolve it and slide on
                }
                cur = sweep.position();
                if (found > 0) {
                    cur = add(cur, resolve(resolved, nullptr));
                }
                break;
            }
            classify();
            totalPush_ = total;
            delta = sub(cur, here);
        }
        previousCentre_ = add(here, delta);
        previousRadius_ = radius_;
        if (!floor_.present()) {
            if (airTicks_ != ~0u) {
                airTicks_++;
            }
        } else {
            airTicks_ = 0;
            lastGroundNormal_ = floor_.hit.faceNormal;
        }
        return delta;
    }

    // 0x2f9bf4: standing on the ground (with up to `graceTicks` in the air), not moving off it.
    bool onGround(const V3& velocity, unsigned graceTicks = 0) const {
        if (!floor_.present() && airTicks_ > graceTicks) {
            return false;
        }
        return captain_toad::RecoveredVec::dot(velocity, groundNormal(graceTicks)) <= 0.0f;
    }
    // 0x299bec: the floor's normal, the last ground normal within the grace ticks, else up.
    V3 groundNormal(unsigned graceTicks = 0) const {
        if (floor_.present()) {
            return floor_.hit.faceNormal;
        }
        return airTicks_ <= graceTicks ? lastGroundNormal_ : V3{0, 1, 0};
    }
    // 0x327d28: standing on an edge or corner of the floor (not a face).
    bool onFloorEdge() const { return floor_.present() && floor_.hit.kind != kFaceContact; }
    // 0x2efc8c / 0x2f14d4 / 0x2c7ac4.
    bool anyContact() const { return floor_.present() || wall_.present() || ceiling_.present(); }
    const Contact& floor() const { return floor_; }
    const Contact& wall() const { return wall_; }
    const Contact& ceiling() const { return ceiling_; }
    // 0x2d38d8: the sum of the touched classes' face normals, normalised (zero without contact).
    V3 contactNormal() const {
        V3 n{};
        for (const Contact* c : {&floor_, &wall_, &ceiling_}) {
            if (c->present()) {
                n = captain_toad::RecoveredVec::add(n, c->hit.faceNormal);
            }
        }
        if (captain_toad::RecoveredVec::dot(n, n) > 0.0f) {
            captain_toad::RecoveredVec::normalize(n);
        }
        return n;
    }
    const V3& totalPush() const { return totalPush_; }
    float radius() const { return radius_; }
    float offset() const { return offset_; }

private:
    static constexpr float kNoContact = -99999.0f;   // 0x281930
    static constexpr float kStepFraction = 0.9f;     // 0x3f666666
    static constexpr float kMaxStep = 35.0f;
    static constexpr float kTiny = 1.0e-6f;             // 0x358637be
    static constexpr float kRadiusTolerance = 0.001f;   // 0x3a83126f
    static constexpr float kClassCosine = 0.342f;       // 0x3eaf1d44, compared as float bits
    static constexpr size_t kMaxContacts = 32;          // 0x24e3ac
    static constexpr uint8_t kFaceContact = 1;
    static constexpr V3 kGravity{0, -1, 0};   // the pose's gravity direction (0x536e30)

    // A sweep of the sphere from `start` by `d`, its radius from r0 to r1 (0x2b997c / 0x2b98ec /
    // 0x2b9950): positions at t in [0, 1], stepped by dt = step / (|d| + r1 - r0).
    struct Sweep {
        V3 start, d;
        float r0 = 0, r1 = 0, dt = 1, t = 0, previous = 0;
        Sweep(const V3& from, const V3& to, float radiusFrom, float radiusTo, float step)
            : start(from), d(captain_toad::RecoveredVec::sub(to, from)), r0(radiusFrom), r1(radiusTo) {
            const float travel = std::sqrt(captain_toad::RecoveredVec::dot(d, d)) + r1 - r0;
            dt = travel <= 0.0f ? 1.0f : step / travel;
        }
        bool finished() const { return previous == 1.0f && t == 1.0f; }
        V3 position() const { return captain_toad::RecoveredVec::add(start, captain_toad::RecoveredVec::scale(d, t)); }
        float radius() const { return r0 + (r1 - r0) * t; }
        V3 rest() const { return captain_toad::RecoveredVec::scale(d, 1.0f - t); }
        void advance() {
            previous = t;
            t = std::min(t + dt, 1.0f);
        }
    };

    V3 centre(const V3& position, const V3& up) const {
        return captain_toad::RecoveredVec::add(position, captain_toad::RecoveredVec::scale(up, offset_));
    }

    // 0x2818f8.
    void clearContacts() {
        hits_.clear();
        floor_ = wall_ = ceiling_ = Contact{};
        totalPush_ = {};
    }

    static Class classOf(const V3& n) {
        if (captain_toad::RecoveredVec::dot(n, n) >= kTiny) {
            const float along = captain_toad::RecoveredVec::dot(n, kGravity);
            if (std::fabs(along) >= kClassCosine) {
                if (along < 0.0f) {
                    return Class::Floor;
                }
            } else {
                return Class::Wall;
            }
        }
        return Class::Ceiling;
    }

    // One query (0x2b9aa0) and the copy into the contact buffer (0x282260): the contacts added.
    size_t queryAt(const SphereQuery& query, const V3& centre, float radius, const V3& direction) {
        found_.clear();
        query(centre, radius, direction, found_);
        size_t added = 0;
        for (const HitInfo& h : found_) {
            if (hits_.size() >= kMaxContacts) {
                break;
            }
            hits_.push_back(h);
            added++;
        }
        return added;
    }

    // 0x32e028: step the sweep until it touches the course; the contacts added (0: none).
    size_t sweepUntilContact(const SphereQuery& query, Sweep& sweep) {
        while (!sweep.finished()) {
            if (const size_t n = queryAt(query, sweep.position(), sweep.radius(), sweep.rest())) {
                return n;
            }
            sweep.advance();
        }
        return 0;
    }

    // 0x32de84: the catch-up sweep; cur / curRadius receive where it ends.
    void catchUpSweep(const SphereQuery& query, Sweep& sweep, const V3& direction, V3& cur, float& curRadius) {
        V3 pushed{};
        bool touched = false;
        while (!sweep.finished()) {
            const V3 at = captain_toad::RecoveredVec::add(sweep.position(), pushed);
            if (queryAt(query, at, sweep.radius(), direction) > 0) {
                touched = true;
                pushed = captain_toad::RecoveredVec::add(pushed, resolve(0, nullptr));
                if (sweep.t >= 1.0f) {
                    break;
                }
            }
            sweep.advance();
        }
        cur = captain_toad::RecoveredVec::add(sweep.position(), pushed);
        curRadius = sweep.radius();
        if (touched) {
            classify();
            hits_.clear();
        }
    }

    // 0x281dd4: the push of the contacts from `first` on (and their normals' combination).
    V3 resolve(size_t first, V3* normalOut) const {
        bool faceTouched[3] = {false, false, false};
        for (size_t i = first; i < hits_.size(); i++) {
            if (hits_[i].kind == kFaceContact) {
                faceTouched[int(classOf(hits_[i].faceNormal))] = true;
            }
        }
        Axes push, normal;
        for (size_t i = first; i < hits_.size(); i++) {
            const HitInfo& h = hits_[i];
            V3 p, n;
            if (h.kind == kFaceContact || faceTouched[int(classOf(h.faceNormal))]) {
                p = captain_toad::RecoveredVec::scale(h.faceNormal, h.depth);   // 0x3ef87c / face case of 0x3ef7a0
                n = h.faceNormal;
            } else {
                V3 dir = captain_toad::RecoveredVec::sub(h.query, h.contact);   // 0x3ef7a0: edge or corner
                if (captain_toad::RecoveredVec::dot(dir, dir) < kTiny) {
                    dir = {};
                } else {
                    captain_toad::RecoveredVec::normalize(dir);
                }
                const V3 facePush = captain_toad::RecoveredVec::scale(h.faceNormal, h.depth);
                p = captain_toad::RecoveredVec::scale(dir, captain_toad::RecoveredVec::dot(facePush, dir));
                n = captain_toad::RecoveredVec::scale(dir, captain_toad::RecoveredVec::dot(h.faceNormal, dir));
            }
            push.add(p);
            if (normalOut) {
                normal.add(n);
            }
        }
        if (normalOut) {
            *normalOut = normal.sum();
        }
        return push.sum();
    }

    // Per-axis combination: the largest positive plus the most negative component.
    struct Axes {
        float hi[3] = {0, 0, 0}, lo[3] = {0, 0, 0};
        void add(const V3& v) {
            const float c[3] = {v.x, v.y, v.z};
            for (int k = 0; k < 3; k++) {
                if (hi[k] < c[k]) {
                    hi[k] = c[k];
                } else if (c[k] < lo[k]) {
                    lo[k] = c[k];
                }
            }
        }
        V3 sum() const { return {hi[0] + lo[0], hi[1] + lo[1], hi[2] + lo[2]}; }
    };

    // 0x281938: the deepest contact of each class.
    void classify() {
        for (const HitInfo& h : hits_) {
            Contact& c = classOf(h.faceNormal) == Class::Floor  ? floor_
                         : classOf(h.faceNormal) == Class::Wall ? wall_
                                                                : ceiling_;
            if (c.depth < h.depth) {
                c.depth = h.depth;
                c.hit = h;
            }
        }
    }

    float radius_ = 0, offset_ = 0;
    V3 previousCentre_{};
    float previousRadius_ = 0;
    std::vector<HitInfo> hits_, found_;
    Contact floor_, wall_, ceiling_;
    V3 totalPush_{};
    unsigned airTicks_ = 0;
    V3 lastGroundNormal_{0, 1, 0};
};

}   // namespace captain_toad::RecoveredActorCollider
