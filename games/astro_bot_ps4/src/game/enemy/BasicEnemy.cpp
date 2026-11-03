#include "game/enemy/BasicEnemy.hpp"

#include "data/Hash.hpp"
#include "physics/PhysicsWorld.hpp"
#include "data/PhysicsFile.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

namespace beacon::game::enemy {

namespace {

using math::Quat;
using math::Vec3;

constexpr float kWorldGravity = -9.8f;   // physics world reset 0xca87e0: (0, -9.8, 0) at 0x1304080 (SetGravity 0xca8d30 unused)
constexpr float kUnset = 3.4028235e38f;
constexpr float kEpsilon = 1.1920929e-07f;
constexpr float kPi = 3.14159265f;

constexpr std::uint32_t kAll = 0xffffffffu;
constexpr std::uint32_t kBodySelf = 0x1000;   // +0x290 (0x2da770)
constexpr std::uint32_t kProbeTarget = 0x88040810u;   // +0x298 (0x2da770)
constexpr std::size_t kEnemySensors = 4;   // the kind's sensors A..D (enemy +0x3be4..); the body's own follow

// Clip name hashes used below (FNV-1a of the names in the object definitions).
constexpr std::uint32_t kIdle = data::fnv1a("idle");
constexpr std::uint32_t kSpawnStart = data::fnv1a("spawn_start");
constexpr std::uint32_t kSpawnUp = data::fnv1a("spawn_up");
constexpr std::uint32_t kSpawnUpLoop = data::fnv1a("spawn_up_loop");
constexpr std::uint32_t kSpawnDown = data::fnv1a("spawn_down");
constexpr std::uint32_t kSpawnLanding = data::fnv1a("spawn_landing");

// ---- generic leaves ---------------------------------------------------------------------------

// Finishes at once (0x167d2a0): placed after a group of channels, it lets the next behaviour on
// them start only when everything before has ended.
class Terminator final : public Behavior {
    Status onUpdate(BasicEnemy&) override { return Status::Done; }
};

// Counts down (0x167ceb8, update 0x256130): running while the time left before this update is
// positive.
class Wait final : public Behavior {
public:
    explicit Wait(float seconds) : left_(seconds) {}

private:
    Status onUpdate(BasicEnemy& e) override {
        const float before = left_;
        left_ -= e.dt();
        return before <= 0 ? Status::Done : Status::Running;
    }
    float left_;
};

// Runs its parts in parallel, each on its own channel, and ends when the first one ends (0x1686a80:
// start 0x2f9a40, update 0x2f9a00).
class Race final : public Behavior {
public:
    void addPart(std::unique_ptr<Behavior> b) { parts_.push_back(std::move(b)); }

private:
    void onStart(BasicEnemy&) override {
        for (std::size_t i = 0; i < parts_.size(); ++i) {
            add(std::move(parts_[i]), 1u << i);
        }
        count_ = int(parts_.size());
        parts_.clear();
    }
    Status onUpdate(BasicEnemy&) override { return childCount() != count_ ? Status::Done : Status::Running; }
    std::vector<std::unique_ptr<Behavior>> parts_;
    int count_ = 0;
};

// The body mode a clip asks for: 1 = on the ground or in the air, whichever holds now.
int resolveMode(const BasicEnemy& e, int mode) {
    return mode == kModeInitial ? (e.grounded() ? kModeGround : kModeAir) : mode;
}

// Keeps the body mode in step with the ground under "either" clips (0x2e7e00 / 0x2e7c20 / 0x2f78f0).
void followGround(BasicEnemy& e) {
    if (e.bodyMode() == kModeAir && e.grounded()) {
        e.setMode(kModeGround);
    } else if (e.bodyMode() == kModeGround && !e.grounded()) {
        e.setMode(kModeAir);
    }
}

// Plays a clip once (0x16863f0: start 0x2e7d30, update 0x2e7e00, stop 0x2e7de0).
class PlayOnce final : public Behavior {
public:
    PlayOnce(std::uint32_t clip, int mode, float speed = 1, bool rootMotion = false, bool rootMotionY = false)
        : clip_(clip), mode_(mode), speed_(speed), rootMotion_(rootMotion), rootMotionY_(rootMotionY) {}

private:
    void onStart(BasicEnemy& e) override {
        e.playClip(clip_, false, resolveMode(e, mode_), rootMotion_, rootMotionY_);
        if (speed_ < 1) {
            e.setClipSpeed(speed_);
        }
    }
    Status onUpdate(BasicEnemy& e) override {
        if (mode_ == kModeInitial) {
            followGround(e);
        }
        return e.clipPlaying() ? Status::Running : Status::Done;
    }
    void onStop(BasicEnemy& e) override {
        if (speed_ < 1) {
            e.setClipSpeed(1);
        }
    }
    std::uint32_t clip_;
    int mode_;
    float speed_;
    bool rootMotion_, rootMotionY_;
};

// Plays a clip in a loop and yields its channels (0x1686738: start 0x2f7860, update 0x2f78f0).
class PlayLoop final : public Behavior {
public:
    PlayLoop(std::uint32_t clip, int mode) : clip_(clip), mode_(mode) {}

private:
    void onStart(BasicEnemy& e) override { e.playClip(clip_, true, resolveMode(e, mode_)); }
    Status onUpdate(BasicEnemy& e) override {
        if (mode_ == kModeInitial) {
            followGround(e);
        }
        return Status::Waiting;
    }
    std::uint32_t clip_;
    int mode_;
};

// Plays a clip once on the ground or in the air and follows the ground meanwhile (0x1686378:
// start 0x2e7b60, update 0x2e7c20).
class PlayGrounded final : public Behavior {
public:
    explicit PlayGrounded(std::uint32_t clip) : clip_(clip) {}

private:
    void onStart(BasicEnemy& e) override {
        add(std::make_unique<PlayOnce>(clip_, e.grounded() ? kModeGround : kModeAir), 1);
    }
    Status onUpdate(BasicEnemy& e) override {
        followGround(e);
        return channelsFree() ? Status::Done : Status::Running;
    }
    std::uint32_t clip_;
};

// Turns from the rotation at the start to `target` over `duration`, eased (1 - cos(pi t)) / 2
// (0x1685fb8, update 0x2e5ab0); yields once the time is over.
class TurnOverTime final : public Behavior {
public:
    TurnOverTime(float duration, const Quat& target) : duration_(duration), target_(target) {}

private:
    void onStart(BasicEnemy& e) override { from_ = e.rotation(); }
    Status onUpdate(BasicEnemy& e) override {
        if (duration_ <= kEpsilon) {
            e.setRotation(target_);
            return Status::Running;
        }
        const float t = e.dt() * float(updates()) / duration_;
        if (t > 1) {
            return Status::Waiting;
        }
        const float s = (1 - std::cos(t * kPi)) * 0.5f;
        e.setRotation(math::slerp(from_, target_, s));
        return Status::Running;
    }
    float duration_;
    Quat from_, target_;
};

// ---- movement -----------------------------------------------------------------------------------

float yawOf(const Quat& q) { return 2 * std::atan2(q.y, q.w); }
Quat yawQuat(float yaw) { return {0, std::sin(yaw * 0.5f), 0, std::cos(yaw * 0.5f)}; }
float wrapAngle(float a) {
    while (a > kPi) {
        a -= 2 * kPi;
    }
    while (a < -kPi) {
        a += 2 * kPi;
    }
    return a;
}

// A goal the brain moves to: a point (0x167d0e0) or the target hero.
struct Goal {
    Vec3 at;
    bool hero = false;
};

Vec3 goalAt(const BasicEnemy& e, const Goal& g) {
    return g.hero && e.world().hasHero ? e.world().heroFeet : g.at;
}

// Angle between the facing and the direction to `at` on the ground plane, signed (positive: the
// goal is on the left, i.e. the yaw has to grow).
float angleTo(const BasicEnemy& e, const Vec3& at);
float angleTo(const BasicEnemy& e, const Goal& g) { return angleTo(e, goalAt(e, g)); }
float angleTo(const BasicEnemy& e, const Vec3& at) {
    const Vec3& p = e.position();
    const float dx = at.x - p.x, dz = at.z - p.z;
    if (dx * dx + dz * dz < 1e-12f) {
        return 0;
    }
    return wrapAngle(std::atan2(dx, dz) - yawOf(e.rotation()));
}

// 0x2e7510: facing within `limit` (radians) of the goal; limits below 0 or from pi up always hold.
bool facing(const BasicEnemy& e, const Goal* g, float limit) {
    if (limit < 0 || limit >= kPi) {
        return true;
    }
    if (!g) {
        return false;
    }
    return std::fabs(angleTo(e, *g)) <= limit;
}

// Brain parameters copied from brain to brain (0x2ea710 / 0x2ea8c0).
struct BrainParams {
    std::shared_ptr<Goal> goal;   // +0xe8
    std::uint32_t walk = 0, turnRight = 0, turnLeft = 0;   // +0xd0 / +0xd4 / +0xd8
    float radius = 0.15f;          // +0xf4 horizontal reach
    float height = -1;             // +0xf8 vertical reach (x 0.5), < 0: any
    float faceAngle = kPi;         // +0xfc facing needed when reached
    float angleToTurn = 0;         // +0x100 AngleToTurn (rad)
    float angleToBigTurn = -1;     // +0x104 AngleToBigTurn (rad, < 0 off)
    int purpose = 0;               // +0x84: 0 a goal, 2 a waypoint of a planned path (1: rope, not ported)
    int planLevel = 1;             // +0x194: the next search's level (1, then 2 on a retry)
    std::shared_ptr<std::vector<Vec3>> path = std::make_shared<std::vector<Vec3>>();   // +0x198 waypoints
};

// 0x2eaee0: the goal is reached: horizontally within the radius (and vertically within half the
// height when set), facing as asked.
bool reached(const BasicEnemy& e, const BrainParams& b) {
    if (!b.goal) {
        return false;
    }
    const Vec3& p = e.position();
    const Vec3 at = goalAt(e, *b.goal);
    const float dx = p.x - at.x, dz = p.z - at.z;
    if (std::sqrt(dx * dx + dz * dz) > b.radius) {
        return false;
    }
    if (b.height >= 0 && std::fabs(p.y - at.y) > b.height * 0.5f) {
        return false;
    }
    return facing(e, b.goal.get(), b.faceAngle);
}

// Plays a looping clip with root motion and turns toward the goal at AngularVelocity (0x1686210:
// start 0x2e6e70, update 0x2e6ea0 -> 0x2e6ef0); yields its channels.
class SteerTo final : public Behavior {
public:
    SteerTo(std::shared_ptr<Goal> goal, std::uint32_t clip) : goal_(std::move(goal)), clip_(clip) {}

private:
    void onStart(BasicEnemy& e) override { e.playClip(clip_, true, kModeGround, true); }
    Status onUpdate(BasicEnemy& e) override {
        if (goal_) {
            const float maxStep = e.tuning().number("AngularVelocity", 240) * kPi / 180.0f * e.dt();
            const float a = angleTo(e, *goal_);
            const float step = std::fabs(a) <= maxStep ? a : std::copysign(maxStep, a);
            e.setRotation(yawQuat(yawOf(e.rotation()) + step));
        }
        return Status::Waiting;
    }
    std::shared_ptr<Goal> goal_;
    std::uint32_t clip_;
};

// Falls while airborne (0x1685e50: start 0x2e4dd0, update 0x2e4ea0, stop 0x2e4e50): on the ground it
// plays nothing and ends at once, so placed behind other behaviours it closes their channels.
// Airborne: the falling clip until grounded (the update's smoothing and edge cases are not ported).
class FallUntilGrounded final : public Behavior {
public:
    explicit FallUntilGrounded(std::uint32_t clip = data::fnv1a("falling")) : clip_(clip) {}

private:
    void onStart(BasicEnemy& e) override {
        if (!e.grounded()) {
            e.playClip(clip_, true, kModeAir);
        }
    }
    std::uint32_t clip_;
    Status onUpdate(BasicEnemy& e) override {
        if (e.grounded()) {
            if (e.bodyMode() == kModeAir) {
                e.setMode(kModeGround);
            }
            return Status::Done;
        }
        return Status::Running;
    }
};

// Plays a clip held in place (mode 0x10, the clip's root lifting it) until grounded or the clip
// ends (0x1686a08: start 0x2f9750, update 0x2f9780).
class PlayUntilGrounded final : public Behavior {
public:
    explicit PlayUntilGrounded(std::uint32_t clip) : clip_(clip) {}

private:
    void onStart(BasicEnemy& e) override { e.playClip(clip_, false, kModeHeld, false, true); }
    Status onUpdate(BasicEnemy& e) override {
        return e.grounded() || !e.clipPlaying() ? Status::Done : Status::Running;
    }
    std::uint32_t clip_;
};

// A jump driven by its clips (0x1685f40: start 0x2e5710, update 0x2e5940; JumpLocomotion): jump_up
// and jump_down held in place with the clips' vertical root motion, then jump_down_loop falling
// and jump_landing on the ground, one after the other (all share channel 1). While the first two
// play, and on the ground while the landing plays, the root moves by the horizontal velocity;
// the landing ends the jump at an edge (sensor state 4).
class ClipJump final : public Behavior {
public:
    explicit ClipJump(const Vec3& velocity) : velocity_(velocity) {}

private:
    void onStart(BasicEnemy&) override {
        add(std::make_unique<PlayOnce>(data::fnv1a("jump_up"), kModeHeld, 1.0f, false, true), 0x80000001u);
        add(std::make_unique<PlayUntilGrounded>(data::fnv1a("jump_down")), 0x80000001u);
        add(std::make_unique<FallUntilGrounded>(data::fnv1a("jump_down_loop")), 0x40000001u);
        add(std::make_unique<PlayOnce>(data::fnv1a("jump_landing"), kModeGround), 0x20000001u);
    }
    Status onUpdate(BasicEnemy& e) override {
        if (channelsFree()) {
            return Status::Done;
        }
        if (channelsFree(0x80000000u)) {
            if (!channelsFree(0x40000000u) || !e.grounded()) {
                return Status::Running;
            }
            if (e.sensorState() == 4) {
                return Status::Done;
            }
        }
        const Vec3& p = e.position();
        e.moveRoot({p.x + velocity_.x * e.dt(), p.y, p.z + velocity_.z * e.dt()});
        return Status::Running;
    }
    Vec3 velocity_;
};

// The search test (0x2eb8d0, a ground enemy; enemy +0x3c6c, which only kinds that walk on water
// raise, stays 0): no ground ahead (sensor state 4), or with `enemyToo` another enemy ahead (1)
// while the brain's stress is above StressThreshold.
bool needSearch(const BasicEnemy& e, float stress, bool enemyToo) {
    const int s = e.sensorState();
    return s == 4 || (enemyToo && s == 1 && stress > e.tuning().number("StressThreshold", 0.5f));
}

// The path planner (0x1686558: start 0x2e8320, update 0x2e8620, a child of a Stop brain): looks for
// a way around an edge or an enemy by probing directions around a point with the ground sweep's
// geometry, one probe per update while that sweep is off (7 updates of 8), and appends a waypoint
// for the first gap; a child planner continues from it toward the goal, up to three waypoints.
//   - Directions: 12 x level per turn, index i at angle 2 pi i / (12 level) about y from the base
//     direction, in the order +0xe0 (0: 0, -1, +1, -2, ...; -1 / 1: one way); 8 level + 1 samples
//     at depth 0, 6 level deeper (where only points nearer the goal than the start are probed).
//   - Sample point P = origin + turn(base direction x the sweep's forward reach + its height).
//   - Probe 1: a sphere of the sweep's radius from above the origin to P (self 0x1000, target
//     0xd7242f9f): blocked. Probe 2 (unblocked samples, next update): a sphere from P down the
//     sweep's length (target 0x88040810): ground and its height.
//   - A gap: the sample just finished and its neighbour toward the base direction both have ground
//     and are unblocked; the waypoint lies halfway between them at the reach, at their mean height.
//   - It runs while the sensors report no ground (4) or an enemy ahead (1), and ends when the
//     samples are spent at depth 0 or a deeper level finished.
class Planner final : public Behavior {
public:
    Planner(std::shared_ptr<Goal> goal, std::shared_ptr<std::vector<Vec3>> path, int level, int depth = 0,
            int order = 0, const Vec3& origin = {}, const Vec3& base = {})
        : goal_(std::move(goal)), path_(std::move(path)), level_(level), depth_(depth), order_(order),
          origin_(origin), base_(base) {}
    const char* label() const override { return "Plan"; }

private:
    static constexpr std::uint32_t kWallTarget = 0xd7242f9fu, kGroundTarget = 0x88040810u, kSelf = 0x1000;

    void onStart(BasicEnemy& e) override {
        // 0x2e8320: the enemy's position (+0x90); at depth 0 the origin and the horizontal direction
        // to the goal (the facing when the goal is right here).
        start_ = e.position();
        if (depth_ == 0) {
            order_ = 0;
            origin_ = start_;
            const Vec3 g = goal_ ? goalAt(e, *goal_) : start_;
            const Vec3 d{g.x - start_.x, 0, g.z - start_.z};
            const float len = std::sqrt(d.x * d.x + d.z * d.z);
            const float yaw = yawOf(e.rotation());
            base_ = len >= 1.1920929e-7f ? Vec3{d.x / len, 0, d.z / len} : Vec3{std::sin(yaw), 0, std::cos(yaw)};
        }
    }
    int directions() const { return 12 * level_; }
    int samples() const { return depth_ == 0 ? level_ * 8 + 1 : level_ * 6; }
    int indexOf(int k) const {
        const int n = directions();
        if (order_ == -1) {
            return (n - k) % n;
        }
        if (order_ == 0) {
            return (k & 1) ? n - 1 - k / 2 : k / 2;
        }
        return k;
    }
    // origin + turn(base x reach + (0, height, 0)) about y by `angle` (the y component of the base is 0).
    Vec3 pointAt(float angle, float reach, float height) const {
        const float s = std::sin(angle), c = std::cos(angle);
        const Vec3 v{base_.x * reach, height, base_.z * reach};
        return {origin_.x + c * v.x + s * v.z, origin_.y + v.y, origin_.z - s * v.x + c * v.z};
    }
    static float flatDistance(const Vec3& a, const Vec3& b) {
        const float dx = a.x - b.x, dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }
    Status onUpdate(BasicEnemy& e) override {
        if (done_) {
            return Status::Done;
        }
        if (!channelsFree()) {
            return Status::Running;   // a deeper level works
        }
        const int state = e.sensorState();
        if (int(path_->size()) > depth_ + 1) {
            e.setGroundSweep(true);
            done_ = true;
            return Status::Running;
        }
        Vec3 at;
        float length = 0, radius = 0;
        physics::PhysicsWorld* w = e.world().physics;
        const int count = samples();
        const float step = 2 * kPi / float(directions());
        bool probed = false;
        if (w && e.groundSweep(at, length, radius) && !e.groundSweepOn()) {
            if (next_ == cur_) {
                int i = -1;
                Vec3 p;
                while (cur_ < count) {
                    i = indexOf(cur_);
                    p = pointAt(float(i) * step, at.z, at.y);
                    if (depth_ == 0 || !goal_ ||
                        flatDistance(start_, goalAt(e, *goal_)) > flatDistance(p, goalAt(e, *goal_))) {
                        break;
                    }
                    ++cur_;
                    ++next_;
                }
                if (i >= 0 && next_ < count) {
                    const auto hit = w->castSphere({origin_.x, p.y, origin_.z}, p, radius, physics::kHitAll,
                                                   {kSelf, kWallTarget});
                    blocked_[std::size_t(i)] = hit.has_value();
                    ++next_;
                    if (hit) {
                        ++cur_;
                    }
                    probed = true;
                }
            } else {
                const int i = indexOf(cur_);
                const Vec3 p = pointAt(float(i) * step, at.z, at.y);
                const auto hit = w->castSphere(p, {p.x, p.y - length, p.z}, radius, physics::kHitAll,
                                               {kSelf, kGroundTarget});
                ground_[std::size_t(i)] = hit.has_value();
                if (hit) {
                    groundY_[std::size_t(i)] = hit->point.y;
                }
                ++cur_;
                probed = true;
            }
        }
        e.setGroundSweep((updates() & 7) == 7);   // +0x78: the sweep runs one update in eight
        if (cur_ == next_) {
            bool gap = false;
            float angle = 0, height = 0;
            int side = 1;
            if (cur_ >= 2) {
                const int c = indexOf(cur_ - 1);
                side = c > 6 * level_ ? -1 : 1;
                const int nb = (c - side) % directions();
                if (nb >= 0 && ground_[std::size_t(c)] && ground_[std::size_t(nb)] && !blocked_[std::size_t(c)] &&
                    !blocked_[std::size_t(nb)]) {
                    gap = true;
                    angle = step * (float(c) - float(side) * 0.5f);
                    height = 0.5f * (groundY_[std::size_t(c)] + groundY_[std::size_t(nb)]);
                }
            } else if (count == 1 && ground_[0] && !blocked_[0]) {
                gap = true;
                height = groundY_[0];
            }
            if (gap && probed && goal_) {
                Vec3 wp = pointAt(angle, at.z, 0);
                wp.y = height;
                path_->resize(std::min(path_->size(), std::size_t(depth_)));
                const Vec3 g = goalAt(e, *goal_);
                const bool closer = flatDistance(g, start_) > flatDistance(g, wp);
                if (depth_ == 0 || closer) {
                    path_->push_back(wp);
                    if (depth_ < 2) {
                        const Vec3 d{g.x - wp.x, 0, g.z - wp.z};
                        const float len = std::sqrt(d.x * d.x + d.z * d.z);
                        const Vec3 base = len > 0 ? Vec3{d.x / len, 0, d.z / len} : base_;
                        add(std::make_unique<Planner>(goal_, path_, level_, depth_ + 1, side, wp, base), 0xffffffffu);
                    } else {
                        done_ = true;
                    }
                }
            } else if (cur_ >= count) {
                if (depth_ == 0) {
                    e.setGroundSweep(true);
                }
                done_ = true;
            }
        }
        if (state != 1 && state != 4) {   // the trouble is over
            e.setGroundSweep(true);
            done_ = true;
        }
        return Status::Running;
    }

    std::shared_ptr<Goal> goal_;
    std::shared_ptr<std::vector<Vec3>> path_;
    int level_, depth_, order_;
    Vec3 origin_, base_, start_;
    int cur_ = 0, next_ = 0;            // +0xe4 / +0xe8
    bool done_ = false;                 // +0x2a0
    std::array<bool, 25> ground_{};     // +0xec
    std::array<bool, 25> blocked_{};    // +0x104
    std::array<float, 25> groundY_{};   // +0x120.. (the hit points' heights)
};

// The hop test (0x2eb6b0, a ground enemy): JumpUpHeight > 0 and an obstacle ahead that is low
// enough (sensor state 3, or 2 while the brain's stress is above StressThreshold).
bool canHop(const BasicEnemy& e, float stress) {
    if (e.tuning().number("JumpUpHeight", 0.3f) <= 0) {
        return false;
    }
    const int s = e.sensorState();
    return s == 3 || (s == 2 && stress > e.tuning().number("StressThreshold", 0.5f));
}

// The locomotion brain ("Basic", 0x16865d0: start 0x2edfc0, update 0x2f1dd0). Ported states:
// NoGoal (1), Fall (7), Jump (8, jump up), Turn (10), Walk (6); a state change queues a new
// brain (the asked state, 0 = decide again) and ends this one.
class Brain final : public Behavior {
public:
    Brain(BrainParams p, int state = 0) : p_(std::move(p)), state_(state) {}
    const char* label() const override {
        switch (state_) {
        case 1: return "NoGoal";
        case 6: return "Walk";
        case 5: return "Path";
        case 7: return "Fall";
        case 8: return "Jump";
        case 11: return "Stop";
        case 10: return "Turn";
        default: return "Brain";
        }
    }

private:
    void onStart(BasicEnemy& e) override {
        e.setSensors(true);   // virtual +0x368 (0x2edfc0)
        e.setBrainStress(0);
        if (!p_.goal) {
            add(std::make_unique<PlayLoop>(kIdle, kModeInitial), 1);
            state_ = 1;
            return;
        }
        if (state_ == 8) {
            startJump(e);
            return;
        }
        if (state_ == 11) {
            startStop();
            return;
        }
        if (state_ == 5) {
            startPath();
            return;
        }
        // A fresh brain: a low obstacle with the goal ahead -> jump; airborne -> fall.
        if (canHop(e, 0) && facing(e, p_.goal.get(), p_.angleToTurn)) {
            startJump(e);
            return;
        }
        if (!e.grounded()) {
            add(std::make_unique<FallUntilGrounded>(), 0xffffffffu);
            state_ = 7;
            return;
        }
        // Hypothesis (the rest of the start's decision block is not decoded): turn in place when
        // the goal is outside AngleToTurn, else walk (traced: 193 deg -> Turn, then Walk).
        if (!facing(e, p_.goal.get(), p_.angleToTurn)) {
            const float a = angleTo(e, *p_.goal);
            add(std::make_unique<SteerTo>(p_.goal, a >= 0 ? p_.turnRight : p_.turnLeft), 1);
            state_ = 10;
        } else if (needSearch(e, 0, true)) {
            startStop();   // the goal is ahead but the way is not (0x2edfc0 at 0x2f1336)
        } else {
            add(std::make_unique<SteerTo>(p_.goal, p_.walk), 1);
            state_ = 6;
        }
    }
    // Stop (0xb): idle on the ground while a planner looks for a way (level +0x194) and fills the
    // path (+0x198, cleared first).
    void startStop() {
        p_.path->clear();
        add(std::make_unique<PlayLoop>(kIdle, kModeGround), 1);
        add(std::make_unique<Planner>(p_.goal, p_.path, p_.planLevel), 2);
        state_ = 11;
    }
    // MoveToSurface (5): a nested brain walks to the first waypoint of the path (reach 0.05 m, any
    // height, facing within 5 deg; +0x84 = 2). The original first turns toward it with 0x1686288 on
    // channel 1; the nested brain's own Turn stands in for that (approximation).
    void startPath() {
        BrainParams w = p_;
        w.goal = std::make_shared<Goal>(Goal{p_.path->front(), false});
        w.radius = 0.05f;
        w.height = -1;
        w.faceAngle = 0.0872665f;
        w.purpose = 2;
        w.path = std::make_shared<std::vector<Vec3>>();
        add(std::make_unique<Brain>(w), 0xffffffffu);
        state_ = 5;
    }
    // Jump (8), type 0 (+0x90): up JumpUpHeight, forward JumpDistance over the time to the apex
    // and back (v0 = sqrt(-2 g h), t = v0 / -g, speed = JumpDistance / 2t), turned by the facing;
    // JumpLocomotion: the clips drive it (0x1685f40 on channel 2).
    void startJump(BasicEnemy& e) {
        const float g = e.gravity() < 3.0e38f ? e.gravity() : -9.8f;
        const float v0 = std::sqrt(-2 * g * e.tuning().number("JumpUpHeight", 0.3f));
        const float t = v0 / -g;
        const float speed = e.tuning().number("JumpDistance", 0.3f) / (t + t);
        const float yaw = yawOf(e.rotation());
        add(std::make_unique<ClipJump>(Vec3{speed * std::sin(yaw), v0, speed * std::cos(yaw)}), 2);
        state_ = 8;
    }
    Status onUpdate(BasicEnemy& e) override {
        // Stress (+0x108): eased toward the share of the move lost to obstacles (0x2f1dd0's start).
        const float keep = std::pow(1 - e.tuning().number("StressInterpRatio", 0.2f), 60 * e.dt());
        stress_ = std::clamp(keep * stress_ + (1 - keep) * e.stressSample(), 0.0f, 1.0f);
        e.setBrainStress(stress_);
        switch (state_) {
        case 10: {
            if (reached(e, p_)) {
                return Status::Done;
            }
            // FreeTransition: AngleToTurn x 0.05, else pi/4 (traced: re-plans 6.9 deg short).
            const float limit = e.tuning().flag("Bind/FreeTransition") ? p_.angleToTurn * 0.05f : kPi / 4;
            if (e.grounded() && facing(e, p_.goal.get(), limit)) {
                return replan();
            }
            return Status::Running;
        }
        case 6:
            if (reached(e, p_)) {
                return Status::Done;
            }
            if (canHop(e, stress_)) {
                return replan(8);
            }
            if (!facing(e, p_.goal.get(), p_.angleToTurn)) {
                return replan();
            }
            // Inside the angle: no way ahead -> Stop (the Edge branch, enemy +0x25e5, is not
            // ported); on the ground keep walking; off the ground decide again.
            if (needSearch(e, stress_, true)) {
                return replan(11);
            }
            return e.grounded() ? Status::Running : replan();
        case 11: {
            // 0x2f1dd0 case 0xb, once the planner has finished.
            if (!channelsFree(2)) {
                return Status::Running;
            }
            const float limit = e.tuning().flag("Bind/FreeTransition") ? p_.angleToTurn * 0.3f : kPi / 4;
            if (!facing(e, p_.goal.get(), limit)) {
                p_.planLevel = 1;
                return replan(10);
            }
            if (!needSearch(e, 0, false) || !e.grounded()) {
                p_.planLevel = 1;
                return replan();
            }
            if (p_.purpose == 2) {
                return Status::Running;
            }
            if (p_.path->size() > 2) {
                p_.planLevel = 1;
                return replan(5);
            }
            p_.planLevel = p_.planLevel == 1 ? 2 : 1;   // search again at the other level
            return replan(11);
        }
        case 5:
            // 0x2f1dd0 case 5: the nested brain reached the waypoint -> the next one, or decide
            // again; a waypoint not reached within 5 s on the ground drops the path.
            if (reached(e, p_)) {
                return Status::Done;
            }
            if (channelsFree()) {
                p_.path->erase(p_.path->begin());
                return replan(p_.path->empty() ? 0 : 5);
            }
            if (float(updates()) * e.dt() > 5.0f && e.bodyMode() == kModeGround) {
                p_.path->clear();
                return replan();
            }
            return Status::Running;
        case 7:
        case 8:
            return channelsFree() ? replan() : Status::Running;
        default:
            return Status::Running;
        }
    }
    Status replan(int state = 0) {
        follow(std::make_unique<Brain>(p_, state));
        return Status::Done;
    }
    BrainParams p_;
    int state_;
    float stress_ = 0;   // +0x108 (0 in every new brain, 0x2ea8c0)
};

// Idles on the ground (0x1686300: start 0x2e7880, update 0x2e79a0); falls when the ground is gone
// (the falling branch is not ported).
class IdleGround final : public Behavior {
public:
    explicit IdleGround(std::uint32_t clip) : clip_(clip) {}

private:
    void onStart(BasicEnemy&) override { add(std::make_unique<PlayLoop>(clip_, kModeGround), 1); }
    Status onUpdate(BasicEnemy& e) override { return e.grounded() ? Status::Waiting : Status::Waiting; }
    std::uint32_t clip_;
};

// Holds the enemy's contact sensors off for a while (0x1687ad0: start 0x3095b0, update 0x3095d0).
class SensorPause final : public Behavior {
public:
    explicit SensorPause(float seconds) : seconds_(seconds) {}

private:
    Status onUpdate(BasicEnemy& e) override {
        return seconds_ <= e.dt() * float(updates()) ? Status::Done : Status::Running;
    }
    float seconds_;
};


// ---- enemy behaviours -------------------------------------------------------------------------

// Watches for hits and reaction requests and passes them up as events (0x1687038: start 0x2fcbd0,
// update 0x2fcc20): a hit -> event 1 to the whole tree, then done; a reaction request -> event 2.
class TriggerWatch final : public Behavior {
public:
    TriggerWatch(bool hits, bool reactions) : hits_(hits), reactions_(reactions) {}

private:
    Status onUpdate(BasicEnemy& e) override {
        if (hits_ && e.hitPending()) {
            root()->broadcast(1);
            return Status::Done;
        }
        if (reactions_ && e.reactionRequest() != 0) {
            root()->broadcast(2);
        }
        return Status::Waiting;
    }
    bool hits_, reactions_;
};

// ---- hits and defeat (.analysis/docs/enemies.md, "Hit reaction and defeat") ----------------

// Quaternion to Euler angles (0x32c70 with rotation order 2). Hypothesis: order 2 is Y, X, Z (yaw,
// pitch, roll) and the standard formulas match the original's polynomial asin / atan2; the
// quaternion is used as given (the caller's is not normalised), the asin argument clamped.
Vec3 eulerYXZ(float x, float y, float z, float w) {
    const float sp = std::clamp(2 * (w * x - y * z), -1.0f, 1.0f);
    return {std::asin(sp), std::atan2(2 * (x * z + w * y), w * w - x * x - y * y + z * z),
            std::atan2(2 * (x * y + w * z), w * w - x * x + y * y - z * z)};
}

// The throw of a defeated enemy (0x60a8d0): it rises AttackPunchVector.y and lands
// |AttackPunchVector.xz| further along the hit's horizontal direction, at the starting height,
// under world gravity; it spins about the horizontal axis across the direction.
void throwVelocity(const BasicEnemy& e, const Vec3& dir, const Vec3& punch, float spin, Vec3& vel, Vec3& angular) {
    vel = {};
    angular = {};
    if (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z < 0.01f) {
        return;
    }
    // Facing (the rotation applied to the model's forward axis +Z): the fallback direction.
    const Quat q = e.rotation();
    const Vec3 facing{2 * (q.x * q.z + q.w * q.y), 2 * (q.y * q.z - q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y)};
    Vec3 fwd{dir.x, 0, dir.z};
    const float len = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
    fwd = len < 1.1920929e-7f ? facing : Vec3{fwd.x / len, 0, fwd.z / len};
    const float g = kWorldGravity;
    const float up = std::sqrt(g * -2.0f * punch.y);
    const float flight = up * -2.0f / g;
    const float across = flight > 0 ? std::sqrt(punch.x * punch.x + punch.z * punch.z) / flight : 0;
    vel = {fwd.x * across, up, fwd.z * across};
    Vec3 d = dir;
    if (std::fabs(d.y) > 0.9f) {
        d = facing;
    }
    Vec3 axis{d.z, 0, -d.x};
    const float al = std::sqrt(axis.x * axis.x + axis.z * axis.z);
    if (al > 0) {
        axis = {axis.x / al, 0, axis.z / al};
    }
    const Vec3 euler = eulerYXZ(axis.x, axis.y, axis.z, 0.7853982f);
    const float k = spin / 45.0f;
    angular = {euler.x * k, euler.y * k, euler.z * k};
}

// Keeps the defeated enemy flying (0x1686828 with damage_hit_loop: start 0x2f8010, update
// 0x2f81c0, stop 0x2f80d0): the enemy's DeadCollision body becomes a free rigid body with the
// enemy's velocity and spin (0x2e2750) and the root follows it each update (virtuals +0x70 /
// +0x78 from the model's transform). Yields its channel.
class ThrownLoop final : public Behavior {
public:
    explicit ThrownLoop(std::uint32_t clip) : clip_(clip) {}

private:
    void onStart(BasicEnemy& e) override {
        // The dead body flies (virtual +0x380 on): the loop plays in mode 1; without one the root
        // dynamics' own body flies (mode 8).
        flying_ = e.switchDeadBody(true);
        e.playClip(clip_, true, flying_ ? kModeInitial : kModeThrown);
    }
    Status onUpdate(BasicEnemy& e) override {
        if (flying_) {
            e.followDeadBody();
        }
        return Status::Waiting;
    }
    void onStop(BasicEnemy& e) override {
        if (flying_) {
            e.followDeadBody();
            e.switchDeadBody(false);
        }
    }
    std::uint32_t clip_;
    bool flying_ = false;
};

// Gives the enemy its spin when it starts (0x16877e8, start 0x3078c0: virtual +0x338), after the
// death clip with DamageHitLocomotion: the thrown loop that starts with it hands the spin to the
// dead body (run combat7: the torso flies without spin, the dead body spins).
class SetSpin final : public Behavior {
public:
    explicit SetSpin(const Vec3& w) : w_(w) {}

private:
    void onStart(BasicEnemy& e) override { e.setAngularVelocity(w_); }
    Status onUpdate(BasicEnemy&) override { return Status::Done; }
    Vec3 w_;
};

// Ends the invulnerability after a survived hit (0x1687860, update 0x307930): clears the hit.
class HitCleared final : public Behavior {
    Status onUpdate(BasicEnemy& e) override {
        e.clearHit();
        return Status::Done;
    }
};

// The reaction to a hit (0x1687308: start 0x2fe910, update 0x2ff970): lose the damage; survive
// with damage_hit_light and UnbeatableTime of invulnerability, or be thrown (damage_hit, then
// damage_hit_loop) for DeadTimePunch + random x DeadTimeRandomPunch, then report death (event 0).
class HitReaction final : public Behavior {
public:
    const char* label() const override { return "Hit"; }

private:
    void onStart(BasicEnemy& e) override {
        const EnemyTuning& t = e.tuning();
        const EnemyHit& h = e.lastHit();
        Vec3 vel, angular;
        throwVelocity(e, h.direction, t.vector("AttackPunchVector", {0, 2.5f, 2}),
                      t.number("AttackPunchAngularVelocity", 2250), vel, angular);
        e.setSensors(false);   // virtual +0x368 (0x2fe910)
        const int damage = h.type == 5 ? h.damage : 1;
        e.setHitpoints(std::max(0, e.hitpoints() - damage));
        if (h.type == 4 || h.type == 9) {
            // The hero's hover laser (4) and fire (9): burned on the spot (0x2fea3c), the clip in a
            // loop, DeadTimeBurned, then the reaction ends (death when no hit points are left).
            add(std::make_unique<PlayLoop>(data::fnv1a("burned"), kModeAir), 1);
            add(std::make_unique<Wait>(t.number("DeadTimeBurned", 1.0f)), 4);
            add(std::make_unique<Terminator>(), kAll);
            return;
        }
        // Types 0x13 and 0x15 have their own branches: not ported.
        if (e.hitpoints() > 0) {
            add(std::make_unique<PlayOnce>(data::fnv1a("damage_hit_light"), kModeInitial), 1);
            add(std::make_unique<Wait>(t.number("UnbeatableTime", 0.5f)), 4);
            add(std::make_unique<HitCleared>(), 4);
            add(std::make_unique<TriggerWatch>(true, true), 4);   // 0x2e2f30 after it: not ported
            return;
        }
        const float deadTime = t.number("DeadTimePunch", 0.8f) + e.random(0, 1) * t.number("DeadTimeRandomPunch", 0.25f);
        e.setVelocity(vel);
        if (!t.flag("Bind/DamageHitLocomotion")) {   // tuning +0x1010
            e.setAngularVelocity(angular);
            add(std::make_unique<PlayOnce>(data::fnv1a("damage_hit"), kModeThrown), 1);
        } else {
            // The velocity only; the death clip holds channels 1 and 2 (not for kind 0x7d3), the spin
            // follows on 2 and the thrown loop on 1 once it ends.
            if (e.kindId() != 0x7d3) {
                add(std::make_unique<PlayOnce>(data::fnv1a("damage_hit"), kModeThrown), 1 | 2);
            }
            add(std::make_unique<SetSpin>(angular), 2);
        }
        add(std::make_unique<ThrownLoop>(data::fnv1a("damage_hit_loop")), 1);
        add(std::make_unique<Wait>(deadTime), 4);
        add(std::make_unique<Terminator>(), kAll);
        // Rewards (+0x3c38: ef_enemy_trajectory effect for 2 x deadTime) and the reward /
        // score calls (virtuals +0x2e0, +0x2e8): not ported yet.
    }
    Status onUpdate(BasicEnemy& e) override {
        if (!channelsFree(1)) {
            return event(1) || event(0) ? Status::Done : Status::Running;
        }
        e.clearHit();
        if (e.hitpoints() < 1) {
            root()->broadcast(0);
        }
        return Status::Done;
    }
};

// Defeated (0x16875d8: start 0x307210 -> virtual +0x388, update: running for good).
class Dead final : public Behavior {
public:
    const char* label() const override { return "Dead"; }

private:
    void onStart(BasicEnemy& e) override { e.remove(); }
};

// The launch jump of LaunchType Jumpup (0x16860a8: start 0x2e65f0, update 0x2e6af0, stop 0x2e6ac0):
// spawn_start held in place, then the ballistic velocity while the airborne clips play
// (spawn_up / spawn_up_loop for the rise, spawn_down for the fall), then spawn_landing.
class LaunchArc final : public Behavior {
public:
    LaunchArc(const Vec3& velocity, float rise, float fall, float gravity, const Quat& target, bool throughWall)
        : velocity_(velocity), rise_(rise), fall_(fall), gravity_(gravity), target_(target),
          throughWall_(throughWall) {}

private:
    void onStart(BasicEnemy& e) override {
        if (gravity_ < 0) {   // LaunchJumpGravity replaces the body's gravity; the old one is kept here
            const float old = e.gravity();
            e.setGravity(gravity_);
            gravity_ = old;
        }
        const float flight = rise_ + fall_;
        const float rest = std::max(0.0f, flight - e.clipLength(kSpawnDown));
        add(std::make_unique<PlayOnce>(kSpawnStart, kModeHeld), 0x0800000f);
        add(std::make_unique<TurnOverTime>(flight, target_), 4);
        auto race = std::make_unique<Race>();
        race->addPart(std::make_unique<PlayOnce>(kSpawnUp, kModeAir));
        race->addPart(std::make_unique<Wait>(rest));
        add(std::move(race), 0x10000001);
        add(std::make_unique<PlayLoop>(kSpawnUpLoop, kModeAir), 0x10000001);
        add(std::make_unique<Wait>(rest), 2);
        add(std::make_unique<Terminator>(), 3);
        add(std::make_unique<PlayOnce>(kSpawnDown, kModeInitial), 0x10000001);
        add(std::make_unique<Terminator>(), 0x10000007);
        add(std::make_unique<PlayGrounded>(kSpawnLanding), 0x20000001);
        if (throughWall_) {
            pendingCollision_ = true;
        }
    }
    Status onUpdate(BasicEnemy& e) override {
        if (channelsFree()) {
            return Status::Done;
        }
        if (channelsFree(0x08000000) && !channelsFree(0x10000000)) {
            // v.y = v0.y + g (elapsed - dt): the body adds g dt in its step.
            const float g = e.gravity();
            e.setVelocity({velocity_.x, g * (elapsed_ - e.dt()) + velocity_.y, velocity_.z});
            elapsed_ += e.dt();
            return Status::Running;
        }
        if (channelsFree(0x10000000) && pendingCollision_) {
            e.setCollision(true);
            pendingCollision_ = false;
        }
        return Status::Running;
    }
    void onStop(BasicEnemy& e) override {
        if (gravity_ < 0) {   // as 0x2e6ac0: only a negative saved gravity goes back (never, after a swap)
            e.setGravity(gravity_);
        }
    }
    Vec3 velocity_;
    float rise_, fall_, gravity_;
    Quat target_;
    bool throughWall_;
    bool pendingCollision_ = false;
    float elapsed_ = 0;
};

// A ballistic jump (0x1685ec8, made by 0x2e52f0: start 0x2e53b0, update 0x2e5550, stop 0x2e5530):
// in the air from the start velocity, the clip once and then "falling" looping on the same
// channel, v.y = v0.y + g (elapsed - dt) every update; ends once grounded and falling (v.y below
// -FLT_EPSILON) after the first update, or after `duration` when positive.
class BallisticJump final : public Behavior {
public:
    BallisticJump(const Vec3& velocity, std::uint32_t clip, float gravity, float duration)
        : velocity_(velocity), clip_(clip), gravity_(gravity), duration_(duration) {}

private:
    void onStart(BasicEnemy& e) override {
        if (gravity_ < 0) {   // as in LaunchArc: swapped with the body's gravity
            const float old = e.gravity();
            e.setGravity(gravity_);
            gravity_ = old;
        }
        e.setVelocity(velocity_);
        add(std::make_unique<PlayOnce>(clip_, kModeAir), 0x80000001);
        add(std::make_unique<FallUntilGrounded>(), 1);
    }
    Status onUpdate(BasicEnemy& e) override {
        if (channelsFree()) {
            return Status::Done;
        }
        if (e.grounded() && e.velocity().y < -1.1920929e-07f && updates() != 0) {
            return Status::Done;
        }
        if (duration_ > 0 && duration_ <= e.dt() * float(updates())) {
            e.setCollision(true);
            return Status::Done;
        }
        const float g = e.gravity();
        e.setVelocity({velocity_.x, g * (e.dt() * float(updates()) - e.dt()) + velocity_.y, velocity_.z});
        return Status::Running;
    }
    void onStop(BasicEnemy& e) override {
        if (gravity_ < 0) {
            e.setGravity(gravity_);
        }
    }
    Vec3 velocity_;
    std::uint32_t clip_;
    float gravity_, duration_;
};

class Active;
class Cruising;

// Launch (0x16873f8: start 0x300420, update 0x3010c0), by LaunchType.
class Launch final : public Behavior {
public:
    explicit Launch(int type) : type_(type) {}
    const char* label() const override { return "Launch"; }

private:
    void onStart(BasicEnemy& e) override;
    Status onUpdate(BasicEnemy& e) override;
    int type_;
    bool standDone_ = false;
};

// Discover (0x1687218: start 0x2fdb40, update 0x2fe180): plays "discover" (with root motion when
// DiscoverLocomotion) and turns to face the hero between DiscoverRotStartFrame and
// DiscoverRotEndFrame (frames of 1/60 s).
class Discover final : public Behavior {
public:
    const char* label() const override { return "Discover"; }

private:
    void onStart(BasicEnemy& e) override {
        const EnemyTuning& t = e.tuning();
        const float startDelay = std::max(0.0f, t.number("Bind/DiscoverRotStartFrame", 0)) * e.dt();
        const float endTime = t.number("Bind/DiscoverRotEndFrame", -1) >= 0
                                  ? t.number("Bind/DiscoverRotEndFrame", 0) * e.dt()
                                  : e.clipLength(data::fnv1a("discover"));
        add(std::make_unique<PlayOnce>(data::fnv1a("discover"), kModeGround, 1,
                                       t.flag("Bind/DiscoverLocomotion")), 1);
        if (startDelay > 0) {
            add(std::make_unique<Wait>(startDelay), 4);
        }
        Quat face = e.rotation();
        if (e.world().hasHero) {
            const Vec3 h = e.world().heroFeet;
            face = yawQuat(std::atan2(h.x - e.position().x, h.z - e.position().z));
        }
        add(std::make_unique<TurnOverTime>(endTime - startDelay, face), 4);
        add(std::make_unique<FallUntilGrounded>(), 5);
    }
    Status onUpdate(BasicEnemy&) override { return channelsFree() ? Status::Done : Status::Running; }
};

// Chase (0x1687128: start 0x2fcf10, update 0x2fd290): a brain walking the chase clip to the hero;
// reach = GoalRadius x mean scale, GoalHeight x scale, facing within GoalAngle; when the brain
// ends out of reach (radius + 0.2), chase again.
class Chase final : public Behavior {
public:
    const char* label() const override { return "Chase"; }

private:
    void onStart(BasicEnemy& e) override {
        if (!reached(e, params(e))) {
            add(std::make_unique<Brain>(params(e)), 0x8000000f);
        }
    }
    Status onUpdate(BasicEnemy& e) override {
        if (channelsFree()) {
            return Status::Done;
        }
        if (!channelsFree(0x80000000) || updates() == 0) {
            return Status::Running;
        }
        BrainParams wide = params(e);
        wide.radius += 0.2f;
        if (!reached(e, wide)) {
            follow(std::make_unique<Chase>());
            return Status::Done;
        }
        return Status::Running;
    }
    static BrainParams params(const BasicEnemy& e) {
        const EnemyTuning& t = e.tuning();
        BrainParams b;
        b.goal = std::make_shared<Goal>();
        b.goal->hero = true;
        b.walk = data::fnv1a("chase");
        const bool turnClips = t.flag("Bind/TurnLocomotion");
        b.turnRight = turnClips ? data::fnv1a("turn_alerted_l") : data::fnv1a("turn_alerted");
        b.turnLeft = turnClips ? data::fnv1a("turn_alerted_r") : data::fnv1a("turn_alerted");
        b.radius = t.number("GoalRadius", 0.2f) * e.scale();
        b.height = t.number("GoalHeight", 0.4f) * e.scale();
        b.faceAngle = t.number("GoalAngle", 5) * kPi / 180.0f;
        b.angleToTurn = t.number("AngleToTurn", 135) * kPi / 180.0f;
        const float big = t.number("AngleToBigTurn", -1);
        b.angleToBigTurn = big >= 0 ? big * kPi / 180.0f : -1;
        return b;
    }
};

// Loops a clip on the ground and keeps the body mode in step with the ground (0x1686300 made by
// 0x2e7820 with "follow" set: start 0x2e7880, update 0x2e79a0); yields its channels.
class GroundLoop final : public Behavior {
public:
    explicit GroundLoop(std::uint32_t clip) : clip_(clip) {}

private:
    void onStart(BasicEnemy&) override { add(std::make_unique<PlayLoop>(clip_, kModeGround), 1); }
    Status onUpdate(BasicEnemy& e) override {
        followGround(e);
        return Status::Waiting;
    }
    std::uint32_t clip_;
};

// One attack (0x16871a0: start 0x2fd560, update 0x2fd810, stop 0x2fd7f0). With a clip it plays
// that clip; with none (the spiky) attack_start, then attack_idle looping for AttackTime (tuning
// +0x29d0), then attack_end when the definition has it. The guard (+0x3aaf, 0x2dfef0) is up from
// the start until the 0x80000000 channels are free: for the spiky attack_start (0.533 s) + 0.8 s
// = 80 updates (run roof_g4: 81, 3602-3683); for enemy_standard the "attack" clip (13 updates).
class Attack final : public Behavior {
public:
    const char* label() const override { return "Attack"; }
    explicit Attack(std::uint32_t clip) : clip_(clip) {}

private:
    void onStart(BasicEnemy& e) override {
        if (clip_ != 0) {
            add(std::make_unique<PlayGrounded>(clip_), 0x80000001);
            add(std::make_unique<Terminator>(), kAll);
        } else {
            add(std::make_unique<PlayGrounded>(data::fnv1a("attack_start")), 0x80000001);
            add(std::make_unique<Terminator>(), kAll);
            add(std::make_unique<GroundLoop>(data::fnv1a("attack_idle")), 0x80000001);
            add(std::make_unique<Wait>(e.tuning().number("AttackTime", 0.8f)), 0x80000002);
            add(std::make_unique<Terminator>(), kAll);
            if (e.clipLength(data::fnv1a("attack_end")) > 0) {   // 0x2df770: the definition has it
                add(std::make_unique<PlayGrounded>(data::fnv1a("attack_end")), 1);
                add(std::make_unique<Terminator>(), 0xf);
            }
        }
        e.setAttacking(true);
    }
    Status onUpdate(BasicEnemy& e) override {
        if (channelsFree(0x80000000)) {
            e.setAttacking(false);
        }
        return channelsFree() ? Status::Done : Status::Running;
    }
    void onStop(BasicEnemy& e) override { e.setAttacking(false); }
    std::uint32_t clip_;
};

// The spiky's charge before an attack (0x16870b0: start 0x2fcd70, update 0x2fce90): shake_start,
// then shake_idle looping, until ChargeTime (tuning +0x2920) has passed.
class SpikyCharge final : public Behavior {
public:
    explicit SpikyCharge(float seconds) : seconds_(seconds) {}
    const char* label() const override { return "Charge"; }

private:
    void onStart(BasicEnemy&) override {
        add(std::make_unique<PlayGrounded>(data::fnv1a("shake_start")), 1);
        add(std::make_unique<PlayLoop>(data::fnv1a("shake_idle"), kModeInitial), 1);
        add(std::make_unique<Wait>(seconds_), 2);
        add(std::make_unique<Terminator>(), kAll);
    }
    Status onUpdate(BasicEnemy&) override { return channelsFree() ? Status::Done : Status::Running; }
    float seconds_;
};

// Active (0x1687470: start 0x301a30, update 0x302af0) for enemy_standard (0x7d2): discover (the
// first time), chase, attack_start, attack, attack_end, each group closed by a terminator (the
// spiky 0x7d4: chase, charge, attack); then
// again while the hero stays in the chase or end trigger, else back to Cruising.
class Active final : public Behavior {
public:
    explicit Active(bool discover) : discover_(discover) {}
    const char* label() const override { return "Active"; }

private:
    void onStart(BasicEnemy& e) override {
        // The target: the nearest hero inside the chase trigger (virtual +0x300 lists the heroes
        // for which +0x2f8(hero, chase) holds). Without one no children are added and the update
        // hands over to Cruising (this is how a launch with nobody near ends in Cruising).
        hasTarget_ = e.chaseTarget();
        if (!hasTarget_) {
            return;
        }
        if (discover_) {
            add(std::make_unique<Discover>(), 0x08000002);
            add(std::make_unique<TriggerWatch>(true, true), 4);
            add(std::make_unique<Terminator>(), 0xf);
        }
        add(std::make_unique<Chase>(), 0x10000003);
        add(std::make_unique<TriggerWatch>(true, true), 4);
        add(std::make_unique<Terminator>(), 0xf);
        if (e.kindId() == 0x7d4) {
            // enemy_spiky: charge (shaking), then the attack with its own clips; no attack_end here
            // (the attack plays it).
            add(std::make_unique<SpikyCharge>(e.tuning().number("ChargeTime", 0.6f)), 0x20000002);
            add(std::make_unique<TriggerWatch>(true, true), 4);
            add(std::make_unique<Terminator>(), 0xf);
            add(std::make_unique<Attack>(0), 0x40000002);
            return;
        }
        add(std::make_unique<PlayGrounded>(data::fnv1a("attack_start")), 0x20000002);
        add(std::make_unique<TriggerWatch>(true, true), 4);
        add(std::make_unique<Terminator>(), 0xf);
        add(std::make_unique<Attack>(data::fnv1a("attack")), 0x40000002);
        add(std::make_unique<TriggerWatch>(true, true), 4);
        add(std::make_unique<Terminator>(), 0xf);
        add(std::make_unique<PlayGrounded>(data::fnv1a("attack_end")), 0x80000002);
    }
    Status onUpdate(BasicEnemy& e) override;
    bool discover_;
    bool hasTarget_ = false;   // +0x80
};

// Cruising (0x1687650: start 0x303aa0, update 0x3050d0): walk back home when away from it, else
// to a random point within CruisingRadius of home; then idle for a random time between
// CrusingTimeSpan and CruisingTimeSpan; then cruise again. Starts Active when a hero is found.
class Cruising final : public Behavior {
public:
    const char* label() const override { return "Cruising"; }

private:
    void onStart(BasicEnemy& e) override {
        const EnemyTuning& t = e.tuning();
        BrainParams b;
        b.walk = data::fnv1a("walk");
        const bool turnClips = t.flag("Bind/TurnLocomotion");
        b.turnRight = turnClips ? data::fnv1a("turn_r") : data::fnv1a("turn");
        b.turnLeft = turnClips ? data::fnv1a("turn_l") : data::fnv1a("turn");
        b.angleToTurn = t.number("AngleToTurn", 135) * kPi / 180.0f;
        const float big = t.number("AngleToBigTurn", -1);
        b.angleToBigTurn = big >= 0 ? big * kPi / 180.0f : -1;
        b.goal = std::make_shared<Goal>();
        const Vec3 home = e.home();
        const float dx = e.position().x - home.x, dz = e.position().z - home.z;
        if (std::sqrt(dx * dx + dz * dz) > 0.15f) {   // 0x2ead50(0.15, -1, pi): not home
            b.goal->at = home;
            b.radius = 0.075f;
            b.faceAngle = kPi;
        } else {
            // A random point around home (the original draws from the engine's global generator,
            // so the points differ from the original's).
            const float r = t.number("CruisingRadius", 0.5f) * std::sqrt(e.random(0, 1));
            const float a = e.random(-kPi, kPi);
            b.goal->at = {home.x + r * std::sin(a), home.y, home.z + r * std::cos(a)};
            b.radius = 0.15f;
            b.faceAngle = 0.06981317f;   // 4 deg
        }
        if (!e.scriptedGoals.empty()) {   // verification: the original's goal
            b.goal->at = e.scriptedGoals.front();
            e.scriptedGoals.erase(e.scriptedGoals.begin());
        }
        float wait = e.random(t.number("CrusingTimeSpan", 1), t.number("CruisingTimeSpan", 5));
        if (!e.scriptedWaits.empty()) {
            wait = e.scriptedWaits.front();
            e.scriptedWaits.erase(e.scriptedWaits.begin());
        }
        add(std::make_unique<Brain>(b), 3);
        add(std::make_unique<TriggerWatch>(true, true), 4);
        add(std::make_unique<Terminator>(), kAll);
        add(std::make_unique<IdleGround>(kIdle), 1);
        add(std::make_unique<Wait>(wait), 2);
        add(std::make_unique<TriggerWatch>(true, true), 4);
        add(std::make_unique<SensorPause>(wait), 8);
        add(std::make_unique<Terminator>(), kAll);
    }
    Status onUpdate(BasicEnemy& e) override {
        if (event(0)) {
            follow(std::make_unique<Dead>());
            return Status::Done;
        }
        if (event(1)) {
            follow(std::make_unique<HitReaction>());
            follow(std::make_unique<Cruising>());
            return Status::Done;
        }
        if (event(2)) {
            return Status::Running;   // reaction requests (0x1687380): not ported yet
        }
        if (e.chaseTarget()) {
            follow(std::make_unique<Active>(!e.tuning().flag("Bind/NoDiscoveryAction") && !e.noDiscovery()));
            return Status::Done;
        }
        if (!channelsFree()) {
            // After more than a second of cruising, no ground (sensor state 4) or another enemy
            // (1) ahead while not airborne starts cruising again, and with it a fresh brain
            // (traced: an obstacle hop rising over an edge in held mode turns into a fall).
            const int s = e.sensorState();
            if ((s == 4 || s == 1) && e.bodyMode() != kModeAir && float(updates()) * e.dt() > 1.0f) {
                follow(std::make_unique<Cruising>());
                return Status::Done;
            }
            return Status::Running;
        }
        follow(std::make_unique<Cruising>());
        return Status::Done;
    }
};

Status Active::onUpdate(BasicEnemy& e) {
    // 0x302af0: death -> Dead; hit -> the hit reaction, then Active again (no discover).
    if (event(0)) {
        follow(std::make_unique<Dead>());
        return Status::Done;
    }
    if (event(1)) {
        follow(std::make_unique<HitReaction>());
        follow(std::make_unique<Active>(false));
        return Status::Done;
    }
    if (event(2)) {
        return Status::Running;   // reaction requests (0x1687380): not ported yet
    }
    if (!hasTarget_) {
        follow(std::make_unique<Cruising>());
        return Status::Done;
    }
    if (channelsFree()) {
        follow(std::make_unique<Active>(false));
        return Status::Done;
    }
    if (e.triggerFired(2) || e.triggerFired(1)) {
        return Status::Running;
    }
    // Out of both triggers: back to cruising unless an attack follows the chase, a discover is
    // under way, the enemy is off the ground or a one-shot clip still plays (the player's loop
    // flag, 0xbb8170).
    const bool attackNext = channelsFree(0x10000000) && !channelsFree(0x40000000);
    if (attackNext || startedChildHolds(0x08000000) || !e.grounded() || e.bodyMode() != kModeGround ||
        !e.clipLoops()) {
        return Status::Running;
    }
    follow(std::make_unique<Cruising>());
    return Status::Done;
}

// Standby (0x16876c8: start 0x306170, update 0x3069c0): idle in place (hidden with
// InvisibleStandby) until the enemy is launched, then Launch(LaunchType) and Active follow.
class Standby final : public Behavior {
public:
    const char* label() const override { return "Standby"; }

private:
    void onStart(BasicEnemy& e) override {
        e.setCollision(false);
        // Parachute enemies and InvisibleStandby ones (+0x3430) neither play idle nor watch.
        if (e.launchType() != 1 && !e.invisibleStandby()) {
            e.playClip(kIdle, true, kModeHeld);
            add(std::make_unique<TriggerWatch>(true, true), 4);
        }
    }
    Status onUpdate(BasicEnemy& e) override {
        // 0x3069c0: death -> Dead (collision back on); hit -> the hit reaction, then Standby.
        if (event(0)) {
            follow(std::make_unique<Dead>());
            e.setCollision(true);
            return Status::Done;
        }
        if (event(1)) {
            follow(std::make_unique<HitReaction>());
            follow(std::make_unique<Standby>());
            return Status::Done;
        }
        if (!event(2) && !e.triggerFired(0)) {
            return Status::Running;
        }
        if (event(2)) {
            return Status::Running;   // reaction requests (0x1687380): not ported yet
        }
        // Launch, then Active; discover only after a Stand or Panic launch, unless
        // NoDiscoveryAction (0x3069c0).
        follow(std::make_unique<Launch>(e.launchType()));
        const bool standOrPanic = e.launchType() == 0 || e.launchType() == 3;
        follow(std::make_unique<Active>(standOrPanic && !e.noDiscovery()));
        e.setCollision(true);
        return Status::Done;
    }
};

void Launch::onStart(BasicEnemy& e) {
    e.setCollision(true);
    e.setSensors(false);   // virtual +0x368 (0x300420)
    e.setVisible(true);
    switch (type_) {
    case 2: {   // Jumpup
        Vec3 target;
        Quat targetRot = e.rotation();
        if (!e.launchTarget(target, targetRot)) {
            standDone_ = true;   // the straight jump without a target (0x2e52f0 / 0x16879c8): not ported
            break;
        }
        // Trajectory 0xba9310: rise with v = sqrt(-2 g h) to the apex h above the start, fall to
        // the target; horizontal speed = distance / (rise + fall).
        const float h = e.launchJumpHeight(), g = e.launchJumpGravity();
        const Vec3 from = e.position();
        const float vUp = std::sqrt(g * -2.0f * h);
        const float drop = std::min(target.y - from.y - h, 0.0f);
        const float fall = std::sqrt(std::max(0.0f, (drop + drop) / g));
        const float rise = -vUp / g;
        const float dx = target.x - from.x, dz = target.z - from.z;
        const float dist = std::sqrt(std::max(dx * dx + dz * dz, 1.1754944e-38f));
        Vec3 dir;
        if (dist >= kEpsilon) {
            dir = {dx / dist, 0, dz / dist};
        }
        const float speed = dist / (rise + fall);
        const Vec3 velocity{dir.x * speed, vUp, dir.z * speed};
        if (e.kindId() == 0x7d2) {
            add(std::make_unique<LaunchArc>(velocity, rise, fall, g, targetRot, false), 1);
            add(std::make_unique<TriggerWatch>(true, false), 4);
        } else {
            // Other kinds (the spiky; 0x7d6 has its own, not ported): a plain ballistic jump playing
            // "spawn" (or "discover" without it) on channel 2 while turning to the target over the
            // flight time on 1, then "landing" (run roof_g4: the spiky grounded at 3513, Active
            // from 3527 = the landing's 14 updates).
            const std::uint32_t clip = e.clipLength(data::fnv1a("spawn")) > 0 ? data::fnv1a("spawn") : data::fnv1a("discover");
            add(std::make_unique<BallisticJump>(velocity, clip, g, -1.0f), 2);
            add(std::make_unique<TurnOverTime>(rise + fall, targetRot), 1);
            add(std::make_unique<TriggerWatch>(true, false), 4);
            add(std::make_unique<PlayGrounded>(data::fnv1a("landing")), 3);
        }
        add(std::make_unique<Terminator>(), kAll);
        break;
    }
    default:   // Stand (no standing time), Parachute, Panic: not ported yet
        standDone_ = true;
        break;
    }
}

Status Launch::onUpdate(BasicEnemy& e) {
    if (event(0)) {
        return Status::Done;   // death: not ported yet
    }
    if (!channelsFree() && !standDone_) {
        return Status::Running;
    }
    e.setCollision(true);
    if (updates() == 0) {
        return Status::Running;
    }
    e.setHome(e.position(), e.rotation());
    e.setActivated();
    return Status::Done;
}

// Root (0x1687740, start 0x307230).
class Root final : public Behavior {
    void onStart(BasicEnemy&) override { add(std::make_unique<Standby>(), kAll); }
};

}   // namespace

// ---- BasicEnemy -------------------------------------------------------------------------------

BasicEnemy::BasicEnemy(const Level& level, std::size_t placement, int kindId, EnemyTuning tuning,
                       std::map<std::uint32_t, EnemyClip> clips, EnemyTransitions transitions, std::uint32_t seed)
    : placement_(placement), kindId_(kindId), tuning_(std::move(tuning)), clips_(std::move(clips)),
      transitions_(std::move(transitions)), rng_(seed ? seed : 1), traceId_(seed) {
    const LevelObject& o = level.objects[placement];
    pos_ = {o.transform.translate[0], o.transform.translate[1], o.transform.translate[2]};
    const math::Mat4 m = o.transform.matrix();
    // Yaw-only rotation of the placement (enemies stand upright).
    const float yaw = std::atan2(m[8], m[10]);
    rot_ = {0, std::sin(yaw * 0.5f), 0, std::cos(yaw * 0.5f)};
    homePos_ = pos_;
    homeRot_ = rot_;
    scale_ = o.transform.scale[1];
    spawnLink_ = resolveLink(level, o, "SpawnTrigger");
    chaseLink_ = resolveLink(level, o, "ChaseTrigger");
    endLink_ = resolveLink(level, o, "EndTrigger");
    launchEnd_ = resolveLink(level, o, "LaunchJumpEnd");
    const std::string lt = o.text("LaunchType");
    launchType_ = lt == "Parachute" ? 1 : lt == "Jumpup" ? 2 : lt == "Panic" ? 3 : 0;
    launchJumpHeight_ = o.number("LaunchJumpHeight").value_or(0.1f);   // binder 0x2d77f0 default
    launchJumpGravity_ = o.number("LaunchJumpGravity").value_or(-9.8f);
    launchDelay_ = o.number("LaunchDelayTime").value_or(0);
    chaseDelay_ = o.number("ChaseDelayTime").value_or(0);
    invisibleStandby_ = o.flag("InvisibleStandby").value_or(false);
    noDiscovery_ = o.flag("NoDiscoveryAction").value_or(false);
    hp_ = int(o.number("Hitpoints").value_or(1));   // binder 0x2d77f0 default 1; +0x2c30 -> +0x3aa8
    visible_ = !invisibleStandby_;
    threshold_ = tuning_.number("Bind/ThresholdOfGround", 0.01f);
    radius_ = tuning_.number("Bind/RootRadius", 0.05f);
    stepRadius_ = tuning_.number("Bind/StepRadius", 0.1f);
    stepSlope_ = tuning_.number("Bind/StepSlopeAngle", 0) * kPi / 180;
    probeRadius_ = tuning_.number("GeometrySensorRadius", 0.025f);
    lastPos_ = pos_;
    lastRot_ = rot_;
    root_ = std::make_unique<Root>();
}

BasicEnemy::~BasicEnemy() = default;

const char* BasicEnemy::stateName() const {
    // The labels down the first-child path ("Active/Chase/Walk").
    static thread_local std::string path;
    path.clear();
    for (const Behavior* b = root_->firstChild(); b; b = b->firstChild()) {
        if (std::string_view(b->label()) != "Basic") {
            if (!path.empty()) {
                path += '/';
            }
            path += b->label();
        }
    }
    return path.empty() ? "-" : path.c_str();
}

bool BasicEnemy::playClip(std::uint32_t hash, bool loop, int mode, bool rootMotion, bool rootMotionY) {
    mode_ = mode;
    auto it = clips_.find(hash);
    if (it == clips_.end()) {
        return false;
    }
    if (clip_ == hash && loop && clipLoop_) {
        rootMotion_ = rootMotion;
        rootMotionY_ = rootMotionY;
        return true;   // the same looping clip keeps playing
    }
    // The clip playing so far blends out over the definition's transition time.
    auto tr = transitions_.find({clip_, hash});
    fromClip_ = clip_;
    fromTime_ = clipTime_;
    fromLoop_ = clipLoop_;
    fromRootMotion_ = rootMotion_;
    fromRootMotionY_ = rootMotionY_;
    blendDuration_ = tr == transitions_.end() ? 0 : tr->second;
    blendTime_ = 0;
    clip_ = hash;
    clipTime_ = 0;
    clipLoop_ = loop;
    rootMotion_ = rootMotion;
    rootMotionY_ = rootMotionY;
    if (hash == data::fnv1a("chase") || hash == data::fnv1a("chase_rope")) {
        speedScale_ = tuning_.vector("SpeedScale", {1, 1, 1});   // kept for later clips too
    }
    return true;
}

math::Vec3 EnemyClip::rootAt(float time) const {
    if (root.empty()) {
        return {};
    }
    const float f = std::max(0.0f, time * rate);
    const std::size_t i = std::min(std::size_t(f), root.size() - 1);
    const std::size_t j = std::min(i + 1, root.size() - 1);
    return math::lerp(root[i], root[j], f - float(i));
}

bool EnemyClip::jointFrame(std::uint32_t joint, float time, math::Vec3& at, math::Quat& turn) const {
    auto it = joints.find(joint);
    if (it == joints.end() || it->second.at.empty()) {
        return false;
    }
    const JointTrack& k = it->second;
    const float f = std::max(0.0f, time * rate);
    const std::size_t i = std::min(std::size_t(f), k.at.size() - 1);
    const std::size_t j = std::min(i + 1, k.at.size() - 1);
    at = math::lerp(k.at[i], k.at[j], f - float(i));
    turn = math::nlerp(k.turn[i], k.turn[j], f - float(i));
    return true;
}

float BasicEnemy::random(float lo, float hi) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return lo + (hi - lo) * float(rng_ & 0xffffff) / float(0x1000000);
}

// Advances the clip (and the one it blends from); `rootDelta` gets the blended root-joint motion
// of this update in the enemy's frame (scaled by the placement scale).
void BasicEnemy::advanceClips(float dt, Vec3& rootDelta) {
    // Advances one clip's time and returns its root-joint motion of this update (zero without
    // `motion`).
    auto step = [&](std::uint32_t hash, float& time, bool loop, bool motion) -> Vec3 {
        auto it = clips_.find(hash);
        if (it == clips_.end()) {
            return {};
        }
        const EnemyClip& c = it->second;
        const float srcLen = c.length * c.speed;   // seconds of the source clip
        const float before = time * c.speed;
        time += dt * clipSpeed_;
        float after = time * c.speed;
        if (loop && srcLen > 0) {
            if (after >= srcLen) {
                time = std::fmod(time, c.length);
            }
        } else if (after > srcLen) {
            after = srcLen;
        }
        if (!motion || c.root.empty()) {
            return {};
        }
        if (loop && srcLen > 0 && after >= srcLen) {   // wrapped: to the end, then from the start
            const Vec3 end = c.rootAt(srcLen), a = c.rootAt(before), b = c.rootAt(std::fmod(after, srcLen));
            return {end.x - a.x + b.x - c.root[0].x, end.y - a.y + b.y - c.root[0].y, end.z - a.z + b.z - c.root[0].z};
        }
        const Vec3 a = c.rootAt(before), b = c.rootAt(after);
        return {b.x - a.x, b.y - a.y, b.z - a.z};
    };
    // Only a clip played with root motion moves the body; while blending, the clip it blends from
    // adds its share (traced: walk ramps up over the 0.2 s turn -> walk blend; reaching the goal
    // stops at once although walk -> idle blends for 0.2 s). Vertical root motion of a clip blended
    // in from one without it counts in full (traced: an obstacle hop's walk -> jump_up rises with
    // the clip's own root speed from its first update; hypothesis: the rule is per axis).
    const bool motion = rootMotion_ || rootMotionY_;
    Vec3 from{};
    float w = 1;
    if (blendDuration_ > 0 && blendTime_ < blendDuration_) {
        blendTime_ += dt;
        w = std::min(1.0f, blendTime_ / blendDuration_);
        from = step(fromClip_, fromTime_, fromLoop_, (fromRootMotion_ || fromRootMotionY_) && motion);
    }
    const Vec3 cur = step(clip_, clipTime_, clipLoop_, motion);
    const float wy = rootMotionY_ && !fromRootMotionY_ ? 1.0f : w;
    rootDelta = {from.x * (1 - w) + cur.x * w, from.y * (1 - wy) + cur.y * wy, from.z * (1 - w) + cur.z * w};
    rootDelta = {rootDelta.x * scale_ * speedScale_.x, rootDelta.y * scale_ * speedScale_.y,
                 rootDelta.z * scale_ * speedScale_.z};
}

bool BasicEnemy::clipPlaying() const {
    auto it = clips_.find(clip_);
    if (it == clips_.end()) {
        return false;
    }
    return clipLoop_ || clipTime_ < it->second.length;
}

float BasicEnemy::clipLength(std::uint32_t hash) const {
    auto it = clips_.find(hash);
    return it == clips_.end() ? 0 : it->second.length;
}

bool BasicEnemy::grounded() const {
    // 0x7e9ad0: the probe's distance within ThresholdOfGround; held in place or thrown (modes 0x10
    // / 8), also the ground cylinder touching ground (its hit bits within +0x298).
    if (groundDistance_ >= 0 && groundDistance_ <= threshold_) {
        return true;
    }
    return (mode_ == kModeHeld || mode_ == kModeThrown) && sensors_.size() > kEnemySensors &&
           sensorBits(int(kEnemySensors), kProbeTarget) != 0;
}

bool BasicEnemy::launchTarget(Vec3& at, Quat& rot) const {
    if (launchEnd_.targets.empty()) {
        return false;
    }
    const math::Mat4& m = world_->runtime->world(launchEnd_.targets.front());
    at = {m[12], m[13], m[14]};
    const float yaw = std::atan2(m[8], m[10]);
    rot = {0, std::sin(yaw * 0.5f), 0, std::cos(yaw * 0.5f)};
    return true;
}

// The default sensors of an enemy without ChaseTrigger / EndTrigger links (0x60a520: sensor
// objects posed at +0x2440 with the kind's DefaultChaseTrigger / DefaultEndTrigger sizes): a hero
// within the vertical cylinder. Hypothesis for the shape: a cylinder of Radius and HalfHeight.
bool BasicEnemy::inDefaultSensor(const char* group) const {
    if (!world_->hasHero) {
        return false;
    }
    const std::string g = std::string("Enemy/") + group;
    const float r = tuning_.number(g + "/Radius", 3), h = tuning_.number(g + "/HalfHeight", 3);
    const Vec3& p = world_->heroFeet;
    const float dx = p.x - homePos_.x, dz = p.z - homePos_.z;
    return dx * dx + dz * dz <= r * r && std::fabs(p.y - homePos_.y) <= h;
}

bool BasicEnemy::triggerFired(int which) const {
    switch (which) {
    case 0:
        return launched_;
    case 1: {   // chase: the link (or default sensor) on for ChaseDelayTime
        return chaseOn_ && chaseDelay_ <= chaseTime_;
    }
    case 2:   // end: the link, or the default end sensor or the chase sensor
        if (!endLink_.empty()) {
            return world_->runtime->linkFired(endLink_);
        }
        return inDefaultSensor("DefaultEndTrigger") || inDefaultSensor("DefaultChaseTrigger");
    default:
        return false;
    }
}

void BasicEnemy::updateTriggers(float dt) {
    // 0x60a520: the spawn link firing once starts the clock; LaunchDelayTime later the enemy is
    // launched. An empty SpawnTrigger counts as fired (hypothesis: set up elsewhere).
    if (!spawned_ && (spawnLink_.empty() || world_->runtime->linkFired(spawnLink_))) {
        spawned_ = true;
    }
    if (!spawned_) {
        return;
    }
    if (sinceSpawn_ < 0) {
        sinceSpawn_ = 0;
    } else {
        sinceSpawn_ += dt;
    }
    if (!launched_ && launchDelay_ <= sinceSpawn_) {
        launched_ = true;
    }
    // Chase trigger time (+0x24a8): while the link (or the default sensor) is on (and +0x24ac,
    // set from the start).
    chaseOn_ = chaseLink_.empty() ? inDefaultSensor("DefaultChaseTrigger") : world_->runtime->linkFired(chaseLink_);
    if (chaseOn_) {
        chaseTime_ += dt;
    } else {
        chaseTime_ = 0;
    }
}

void BasicEnemy::hit(const EnemyHit& h) {
    if (hitPending_ || removed_ || h.type == 0xd) {
        return;
    }
    if (h.type == 6) {   // a reaction request, not damage
        reaction_ = 2;
        return;
    }
    // The spiky kinds' guard (0x2e20a0): while the attack flag +0x3aaf is up (Attack start
    // 0x2fd560 .. stop 0x2fd7f0), kinds 0x7d4..0x7d7 take no punch or kick (types 1-3, 16); the
    // attacker is kept (+0x3b30) for the spikes' answer (0x2e12a0 - the hero's damage, not ported).
    // Type 0xf (only with +0x3ab2) is not ported.
    if (attacking_ && (kindId_ & ~3) == 0x7d4 && ((h.type >= 1 && h.type <= 3) || h.type == 16)) {
        blockedPunch_ = true;
        return;
    }
    hitPending_ = true;
    hit_ = h;
    wasHit_ = true;
}

void BasicEnemy::clearHit() {
    if (hitPending_) {
        wasHit_ = true;
    }
    hitPending_ = false;
}

void BasicEnemy::remove() {
    removed_ = true;
    visible_ = false;
    collide_ = false;
    vel_ = {};
    angular_ = {};
}

void BasicEnemy::update(const EnemyWorld& world) {
    world_ = &world;
    if (removed_) {
        return;
    }
    const float dt = world.dt;
    if (mode_ == kModeInitial && world.runtime) {
        // Before it is launched the root is the placement's world frame, so an enemy under a moving
        // joint rides along with it (traced: the FC_S03 rope-area enemies in Standby).
        const physics::Placement p = physics::placementOf(world.runtime->world(placement_));
        pos_ = p.position;
        rot_ = p.rotation;
    }
    // 0x7dfe50 (start of the enemy update): the sensors, then the body moved by the last world step.
    readSensors();
    if (mode_ != kModeInitial) {
        // Every mode but 1 (0x7e0770); a held root with a holder follows it afterwards (0x7e61b0,
        // no holders in the port yet).
        readBody();
        if (mode_ != kModeThrown) {
            // The velocity becomes what was achieved, each axis within the commanded speed.
            const Vec3 got{(pos_.x - lastPos_.x) / dt, (pos_.y - lastPos_.y) / dt, (pos_.z - lastPos_.z) / dt};
            vel_ = {std::clamp(got.x, -std::fabs(vel_.x), std::fabs(vel_.x)),
                    std::clamp(got.y, -std::fabs(vel_.y), std::fabs(vel_.y)),
                    std::clamp(got.z, -std::fabs(vel_.z), std::fabs(vel_.z))};
        }
    }
    push_ = {pos_.x - applyRoot_.x, pos_.y - applyRoot_.y, pos_.z - applyRoot_.z};   // 0x7e4760
    probeGround();
    if (traced() && body_ >= 0 && world.physics) {   // research aid: root, body centre, body velocity
        const Vec3 b = world.physics->position(body_), v = world.physics->linearVelocity(body_);
        const Vec3 bw = world.physics->angularVelocity(body_);
        std::fprintf(stderr, "rd mode %d pos %.4f %.4f %.4f body %.4f %.4f %.4f bvel %.4f %.4f %.4f bw %.2f %.2f %.2f gd %.4f",
                     mode_, pos_.x, pos_.y, pos_.z, b.x, b.y, b.z, v.x, v.y, v.z, bw.x, bw.y, bw.z, groundDistance_);
        world.physics->library().forEachManifold([&](int a, int c, const physics::rigid::ContactManifold& m) {
            if ((a == body_ || c == body_) && m.size() > 0) {
                const int other = a == body_ ? c : a;
                std::fprintf(stderr, " | with %d (owner %u)", other, world.physics->surface(other).owner);
                for (int i = 0; i < m.size(); ++i) {
                    std::fprintf(stderr, " [n %.3f %.3f %.3f d %.4f]", m.point(i).normal().x, m.point(i).normal().y,
                                 m.point(i).normal().z, m.point(i).distance);
                }
            }
        });
        std::fprintf(stderr, "\n");
    }
    attackHero();   // virtual +0x358, before the classification (+0x3b8)
    classifySensors();
    lastPos_ = pos_;
    lastRot_ = rot_;
    velocitySet_ = false;
    angularSet_ = false;
    updateTriggers(dt);
    root_->run(*this);
    Vec3 root;
    advanceClips(dt, root);
    applyBody(dt, root);
}

// ---- Attack on the hero -------------------------------------------------------------------------

// Virtual +0x358 (basic 0x2e1d80, spiky 0x92c4d0), every update after the root dynamics' step.
// The hit type by kind: the spiky kinds (0x7d4..0x7d7) 0x53 / 0x53 / 0x53 / 0x16 (table 0x11a2630,
// 0x53: spikes), enemy_standard 0x2e (LightDamage), others 0x19 (+0x3ab3: Damage).
// - 0x2e0130: while the attack flag (+0x3aaf) is up, each hero whose body touches the attack body
//   (the DamageJoint body +0x3b38; the spiky also its root body +0x2810) - once per attack (+0x3cf8)
//   - gets the hit from the body's position along (hero - body) normalised.
// - 0x2e12a0 (not enemy_standard): when no hero was hit this update and the flag is up, a hero who
//   punched the guard (+0x3b30), else a hero standing on one of the enemy's bodies (+0x1b0), gets
//   the hit from the damage body; the punch is then forgotten.
void BasicEnemy::attackHero() {
    const EnemyWorld& w = *world_;
    if (removed_ || !w.heroHits || !w.physics) {
        return;
    }
    const bool spiky = (kindId_ & ~3) == 0x7d4;
    static constexpr int kSpikyTypes[4] = {0x53, 0x53, 0x53, 0x16};
    const bool standard = (kindId_ | 1) == 0x7d3;   // +0x3ab3 clear
    const int type = spiky ? kSpikyTypes[kindId_ - 0x7d4] : standard ? 0x2e : 0x19;
    bool landed = false;   // +0x3ab0
    const auto send = [&](int from) {
        const Vec3 at = w.physics->position(from);
        Vec3 d = w.heroFeet - at;   // **[hypothesis]** the hero's +0x58 is its feet
        const float len = math::length(d);
        d = len >= 1.1920929e-07f ? d * (1 / len) : Vec3{0, 0, 1};   // the hero's +0x68 otherwise (not ported)
        w.heroHits->push_back({type, at, d});
        heroHit_ = true;
        landed = true;
    };
    if (attacking_ && !heroHit_ && w.hasHero) {
        const auto touches = [&](int body) {
            if (body < 0) {
                return false;
            }
            for (const physics::rigid::BodyContact& c : w.physics->library().contactsOf(body)) {
                if (c.other >= 0 && c.other == w.heroFoot) {
                    return true;
                }
            }
            return false;
        };
        if (std::getenv("BEACON_DEBUG_ATTACK") && damageBody_ >= 0) {   // research aid
            const Vec3 b = w.physics->position(damageBody_);
            std::fprintf(stderr, "attack %zu head %.3f %.3f %.3f hero %.3f %.3f %.3f contacts", placement_, b.x, b.y, b.z,
                         w.heroFeet.x, w.heroFeet.y, w.heroFeet.z);
            for (const physics::rigid::BodyContact& c : w.physics->library().contactsOf(damageBody_)) {
                std::fprintf(stderr, " %d", c.other);
                if (c.other == w.heroFoot || c.other == w.heroSensor) {
                    for (int k = 0; k < c.count; ++k) {
                        std::fprintf(stderr, "[d %.4f]", c.points[k].distance);
                    }
                }
            }
            const auto ci = clips_.find(clip_);
            std::fprintf(stderr, " (foot %d sensor %d) clip %08x t %.3f joint %08x tracked %d\n", w.heroFoot, w.heroSensor, clip_,
                         clipTime_, damageJoint_, ci != clips_.end() ? int(ci->second.joints.count(damageJoint_)) : -1);
        }
        if (touches(damageBody_)) {
            send(damageBody_);
        } else if (spiky && touches(body_)) {
            send(body_);
        }
    }
    if (!standard && attacking_ && !landed && damageBody_ >= 0 && w.hasHero) {
        const bool onTop = w.heroGround >= 0 && (w.heroGround == body_ || w.heroGround == damageBody_ || w.heroGround == deadBody_);
        if (blockedPunch_ || (onTop && !heroHit_)) {
            send(damageBody_);
        }
    }
    blockedPunch_ = false;
}

// ---- Root dynamics ----------------------------------------------------------------------------

// Research aid: BEACON_ENEMY_TRACE=PLACEMENT prints that enemy's root dynamics each update.
bool BasicEnemy::traced() const {
    // A placement index, or with an x prefix the UUID's hash (the captures' enemy ids: x7ec7251b).
    static const char* spec = std::getenv("BEACON_ENEMY_TRACE");
    if (!spec) {
        return false;
    }
    if (spec[0] == 'x') {
        return std::strtoul(spec + 1, nullptr, 16) == traceId_;
    }
    return std::size_t(std::atol(spec)) == placement_;
}

namespace {

constexpr float kMoved = 1.19e-7f;            // read-back: squared distance the body must move (0x1251bbc)
constexpr float kProbeLength = 1.0f;          // ground probe reach below the root (+0x44)
constexpr float kWalkableSin = 0.86602540f;   // sin(+0x88 = 60 deg): walkable ground
constexpr float kBodyMass = 200.0f;           // 0x7dd0c0: +0x3f0
constexpr float kBodyFriction = 0.01f;        // +0x3f4

Quat mul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

Vec3 turnVec(const Quat& q, const Vec3& v) {
    const Quat r = mul(mul(q, Quat{v.x, v.y, v.z, 0}), conj(q));
    return {r.x, r.y, r.z};
}

}   // namespace

void BasicEnemy::bodyPose(const Vec3& root, const Quat& rot, Vec3& at, Quat& turn) const {
    // 0x7de4f0: the root pose times the body's frame (the frame scaled by m_scl +0x120).
    const Vec3 o = turnVec(rot, {frameOffset_.x * scale_, frameOffset_.y * scale_, frameOffset_.z * scale_});
    at = {root.x + o.x, root.y + o.y, root.z + o.z};
    turn = mul(rot, frameTurn_);
}

void BasicEnemy::sampleJoint(std::uint32_t joint, Vec3& at, Quat& turn) const {
    // The clip's joint frame (blended with the clip it blends from); `at` / `turn` keep the base
    // pose's when the clip has none.
    auto sample = [&](std::uint32_t hash, float time, Vec3& a, Quat& t) {
        auto it = clips_.find(hash);
        return it != clips_.end() && it->second.jointFrame(joint, time * it->second.speed, a, t);
    };
    Vec3 a;
    Quat t;
    if (sample(clip_, clipTime_, a, t)) {
        at = a, turn = t;
        Vec3 fa;
        Quat ft;
        const float w = blendWeight();
        if (w < 1 && sample(fromClip_, fromTime_, fa, ft)) {
            at = math::lerp(fa, at, w);
            turn = math::nlerp(ft, turn, w);
        }
    }
}

void BasicEnemy::updateBodyFrame() {
    Vec3 at = baseOffset_;
    Quat turn = baseTurn_;
    sampleJoint(bodyJoint_, at, turn);
    const Vec3 o = turnVec(turn, bodyShift_);
    frameOffset_ = {at.x + o.x, at.y + o.y, at.z + o.z};
    frameTurn_ = mul(turn, bodyTwist_);
}

void BasicEnemy::attachDamageBody(physics::PhysicsWorld& world, const data::PhysicsFile& file, std::size_t record,
                                  std::uint32_t joint, const Vec3& frameOffset, const Quat& frameTurn,
                                  const Vec3& bodyShift, const Quat& bodyTwist) {
    damageJoint_ = joint;
    damageOffset_ = frameOffset;
    damageTurn_ = frameTurn;
    damageShift_ = bodyShift;
    damageTwist_ = bodyTwist;
    physics::BodyOptions options;
    options.motion = physics::rigid::MotionType::Keyframe;
    options.filterSelf = kBodySelf;
    options.filterTarget = 0xffffffffu;   // +0x3ac0 (value open)
    options.surface.owner = std::uint32_t(placement_);
    options.surface.mask = physics::kHitAll;
    damageBody_ = world.addBody(file, record, {pos_, rot_, {scale_, scale_, scale_}}, options);
    if (damageBody_ >= 0 && body_ >= 0) {
        // One object's bodies never pair (a shared group, state +0x10).
        const std::uint16_t g = std::uint16_t(placement_ & 0x7fff);
        world.setGroups(damageBody_, g, 0xffff);
        world.setGroups(body_, g, 0xffff);
        if (deadBody_ >= 0) {
            world.setGroups(deadBody_, g, 0xffff);
        }
    }
    moveDamageBody();
}

void BasicEnemy::moveDamageBody() {
    if (!bodyWorld_ || damageBody_ < 0) {
        return;
    }
    Vec3 at = damageOffset_;
    Quat turn = damageTurn_;
    sampleJoint(damageJoint_, at, turn);
    const Vec3 shift = turnVec(turn, damageShift_);
    at = {at.x + shift.x, at.y + shift.y, at.z + shift.z};
    const Vec3 o = turnVec(rot_, {at.x * scale_, at.y * scale_, at.z * scale_});
    bodyWorld_->setPose(damageBody_, {pos_.x + o.x, pos_.y + o.y, pos_.z + o.z}, mul(rot_, mul(turn, damageTwist_)));
}

void BasicEnemy::attachBody(physics::PhysicsWorld& world, const data::PhysicsFile& file, std::size_t record,
                            std::uint32_t joint, const Vec3& frameOffset, const Quat& frameTurn,
                            const Vec3& bodyShift, const Quat& bodyTwist) {
    bodyJoint_ = joint;
    baseOffset_ = frameOffset;
    baseTurn_ = frameTurn;
    bodyShift_ = bodyShift;
    bodyTwist_ = bodyTwist;
    updateBodyFrame();
    physics::BodyOptions options;
    options.motion = physics::rigid::MotionType::Fixed;
    options.filterSelf = 0;   // until the first mode change (0x7dd0c0, 0x7e71b0)
    options.filterTarget = 0xffffffffu;
    options.surface.owner = std::uint32_t(placement_);
    options.surface.mask = physics::kHitAll;
    body_ = world.addBody(file, record, {pos_, rot_, {scale_, scale_, scale_}}, options);
    bodyWorld_ = &world;
    if (body_ < 0) {
        return;
    }
    // Mass 200 and the inertia of a cylinder along x for the body's first shape (0x7dd0c0 ->
    // 0xcacc40 -> 0xcbb400 -> 0xde60d0, the library's cylinder rule m (3 r^2 + 4 h^2) / 12 and
    // m r^2 / 2 for half length h and radius r) - called with twice the shape's half length and
    // radius (a capsule's half length with its caps), so four times the solid cylinder's. Run
    // combat7 (the launch's ledge contact): spin after the contact (15.5, 0.9, 15.9) against the
    // original's (15.0, 2.4, 13.9); with the plain cylinder's (24.7, -2.5, 29.1).
    const auto& shapes = world.library().body(body_).shapes;
    physics::rigid::Mat3 inertia;
    if (!shapes.empty()) {
        const auto& sh = shapes[0];
        const float r = 2 * sh.radius;
        const float h = 2 * (sh.halfLength + (sh.type == physics::rigid::ShapeType::Capsule ? sh.radius : 0.0f));
        const float side = kBodyMass * (3 * r * r + 4 * h * h) / 12;
        inertia.r[0] = {0.5f * kBodyMass * r * r, 0, 0};
        inertia.r[1] = {0, side, 0};
        inertia.r[2] = {0, 0, side};
    }
    world.setMass(body_, kBodyMass, inertia);
    world.setFriction(body_, kBodyFriction);
    world.setMaxAngularVelocity(body_, 0);
    Vec3 at;
    Quat turn;
    bodyPose(pos_, rot_, at, turn);
    world.setPose(body_, at, turn);
    if (traced()) {   // research aid
        std::fprintf(stderr, "enemy %zu body %d frame %.5f %.5f %.5f turn %.4f %.4f %.4f %.4f at %.4f %.4f %.4f\n",
                     placement_, body_, frameOffset_.x, frameOffset_.y, frameOffset_.z, frameTurn_.x, frameTurn_.y,
                     frameTurn_.z, frameTurn_.w, at.x, at.y, at.z);
    }
}

void BasicEnemy::setPosition(const Vec3& p) {
    pos_ = p;
    lastPos_ = p;
    if (bodyWorld_ && body_ >= 0) {
        Vec3 at;
        Quat turn;
        bodyPose(pos_, rot_, at, turn);
        bodyWorld_->setPose(body_, at, turn);
    }
}

void BasicEnemy::setRotation(const Quat& q) {
    rot_ = q;
    lastRot_ = q;
    if (bodyWorld_ && body_ >= 0) {
        Vec3 at;
        Quat turn;
        bodyPose(pos_, rot_, at, turn);
        bodyWorld_->setPose(body_, at, turn);
    }
}

void BasicEnemy::attachDeadBody(physics::PhysicsWorld& world, const data::PhysicsFile& file, std::size_t record) {
    physics::BodyOptions options;
    options.motion = physics::rigid::MotionType::Fixed;
    options.filterSelf = 0;
    options.filterTarget = 0xffffffffu;
    options.surface.owner = std::uint32_t(placement_);
    options.surface.mask = physics::kHitAll;
    deadBody_ = world.addBody(file, record, {pos_, rot_, {scale_, scale_, scale_}}, options);
    deadShift_ = file.bodies[record].bodyPosition;
    deadTwist_ = file.bodies[record].bodyRotation;
    bodyWorld_ = &world;
}

bool BasicEnemy::switchDeadBody(bool on) {
    physics::PhysicsWorld* w = bodyWorld_;
    if (!w || deadBody_ < 0) {
        return false;
    }
    using physics::rigid::MotionType;
    if (!on) {
        w->setFilters(deadBody_, 0, 0xffffffffu);
        w->setMotion(deadBody_, MotionType::Fixed);
        if (body_ >= 0) {
            w->setFilters(body_, kBodySelf, 0xffffffffu);
        }
        return true;
    }
    // At the root times the body's offset from it as created (+0x3b70: root^-1 x the body's pose
    // then, i.e. the record's body frame, scaled; 0.07 up for the basic enemy).
    w->setFilters(deadBody_, kBodySelf, 0xffffffffu);
    w->setMotion(deadBody_, MotionType::Active);
    deadSynced_ = false;
    const Vec3 o = turnVec(rot_, {deadShift_.x * scale_, deadShift_.y * scale_, deadShift_.z * scale_});
    w->setPose(deadBody_, {pos_.x + o.x, pos_.y + o.y, pos_.z + o.z}, mul(rot_, deadTwist_));
    w->setLinearVelocity(deadBody_, vel_);
    w->setAngularVelocity(deadBody_, angular_);
    if (body_ >= 0) {
        w->setFilters(body_, 0, 0xffffffffu);   // the enemy's other bodies stop colliding
    }
    return true;
}

void BasicEnemy::followDeadBody() {
    if (!bodyWorld_ || deadBody_ < 0) {
        return;
    }
    // The root from the model's transform (0x2f81c0), which the body sync makes the body times
    // its frame's inverse (the dead record's node frame is the identity). The model was synced
    // from the body before this update's step: the root trails the body by one update (runs
    // combat5 / combat7: the root holds on the update the body is switched on).
    const Vec3 nowAt = bodyWorld_->position(deadBody_);
    const Quat nowTurn = bodyWorld_->orientation(deadBody_);
    if (!deadSynced_) {
        deadSynced_ = true;
        deadModelAt_ = nowAt;
        deadModelTurn_ = nowTurn;
        return;
    }
    const Vec3 at = deadModelAt_;
    rot_ = mul(deadModelTurn_, conj(deadTwist_));
    deadModelAt_ = nowAt;
    deadModelTurn_ = nowTurn;
    const Vec3 o = turnVec(rot_, {deadShift_.x * scale_, deadShift_.y * scale_, deadShift_.z * scale_});
    pos_ = {at.x - o.x, at.y - o.y, at.z - o.z};
    lastPos_ = pos_;
    lastRot_ = rot_;
}

void BasicEnemy::readBody() {
    // 0x7e0770: the root from the body (body = root x frame).
    physics::PhysicsWorld* w = world_->physics;
    if (!w || body_ < 0) {
        return;
    }
    // Thrown: the whole pose from the body; else the position only, the frame undone with the
    // root's own rotation (the body does not turn: its angular speed limit is 0).
    const Quat rot = mode_ == kModeThrown ? mul(w->orientation(body_), conj(frameTurn_)) : rot_;
    const Vec3 at = w->position(body_);
    const Vec3 o = turnVec(rot, {frameOffset_.x * scale_, frameOffset_.y * scale_, frameOffset_.z * scale_});
    const Vec3 root{at.x - o.x, at.y - o.y, at.z - o.z};
    if (mode_ == kModeThrown) {
        pos_ = root;
        rot_ = rot;
        vel_ = w->linearVelocity(body_);
        angular_ = w->angularVelocity(body_);
        return;
    }
    const Vec3 d{root.x - pos_.x, root.y - pos_.y, root.z - pos_.z};
    if (d.x * d.x + d.y * d.y + d.z * d.z > kMoved) {
        pos_ = root;
    }
}

void BasicEnemy::probeGround() {
    // 0x7e20e0: a sphere query from the body centre's height down to the probe length below the
    // root; the hit is the ground (point straight below the root at the hit's height).
    carry_ = {};
    if (mode_ == kModeInitial || mode_ == kModeHeld || mode_ == kModeThrown) {
        groundDistance_ = -2;
        return;
    }
    physics::PhysicsWorld* w = world_->physics;
    if (!w || !collide_) {
        groundDistance_ = -1;
        return;
    }
    // The cached walkable ground follows its body (0x7e20e0: the record keeps the body and the
    // point / normal in its frame, placed again every update); it is dropped when the body's
    // filters stop matching the probe's masks or its normal turns below walkable.
    if (flatGround_ && flatBody_ >= 0) {
        const Quat bt = w->orientation(flatBody_);
        const Vec3 bp = w->position(flatBody_);
        const Vec3 p = turnVec(bt, flatLocal_);
        const Vec3 n = turnVec(bt, flatNormalLocal_);
        const auto& st = w->library().body(flatBody_).state;
        if ((st.filterTarget & kBodySelf) == 0 || (kProbeTarget & st.filterSelf) == 0 || n.y < kWalkableSin) {
            flatGround_ = false;
            flatBody_ = -1;
        } else {
            flatPoint_ = {bp.x + p.x, bp.y + p.y, bp.z + p.z};
        }
    }
    Vec3 centre;
    Quat turn;
    bodyPose(pos_, rot_, centre, turn);
    const Vec3 from{pos_.x, centre.y, pos_.z};
    const Vec3 to{pos_.x, pos_.y - kProbeLength, pos_.z};
    const auto hit = w->castSphere(from, to, probeRadius_, physics::kHitOther, {kBodySelf, kProbeTarget});
    if (!hit) {
        groundDistance_ = -1;
        return;
    }
    float groundY = hit->point.y;
    if (hit->normal.y >= kWalkableSin) {
        flatGround_ = true;
        flatPoint_ = hit->point;
        flatBody_ = hit->body;
        const Quat inv = conj(w->orientation(hit->body));
        const Vec3 bp = w->position(hit->body);
        flatLocal_ = turnVec(inv, {hit->point.x - bp.x, hit->point.y - bp.y, hit->point.z - bp.z});
        flatNormalLocal_ = turnVec(inv, hit->normal);
    } else if (flatGround_) {
        // Steep: within StepRadius of the last walkable ground the root keeps that height
        // (lowered by the step slope over the distance).
        const float dx = pos_.x - flatPoint_.x, dz = pos_.z - flatPoint_.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d <= stepRadius_) {
            groundY = std::max(groundY, flatPoint_.y - std::tan(stepSlope_) * d);
        }
    }
    // Ground mode snaps to the ground, air mode is lifted onto it when below; the distance is taken
    // afterwards (0x7e20e0: +0x4c = root y - ground y after the lift, so a lifted root is grounded).
    if (mode_ == kModeGround || (mode_ == kModeAir && pos_.y < groundY)) {
        pos_.y = groundY;
    }
    groundDistance_ = pos_.y - groundY;
    if (w->moving(hit->body)) {
        // Standing on a moving body: its motion of this update carries the root.
        const Vec3 moved = math::transformPoint(w->delta(hit->body), pos_);
        carry_ = {moved.x - pos_.x, moved.y - pos_.y, moved.z - pos_.z};
    }
}

void BasicEnemy::applyBody(float dt, const Vec3& root) {
    // 0x7e5c60 after the behaviours.
    // Root motion (0x7e8450, body modes 2 and 0x10): the clip's root delta times the axis flags,
    // turned by the facing, moves the root.
    const float yaw = 2 * std::atan2(rot_.y, rot_.w);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    const float h = rootMotion_ ? 1.0f : 0.0f;
    if (mode_ == kModeGround && !velocitySet_) {
        vel_ = {h * (root.x * cy + root.z * sy) / dt, 0, h * (-root.x * sy + root.z * cy) / dt};
    } else if (mode_ == kModeHeld) {
        const float v = rootMotionY_ ? 1.0f : 0.0f;
        pos_ = {pos_.x + h * (root.x * cy + root.z * sy), pos_.y + v * root.y, pos_.z + h * (-root.x * sy + root.z * cy)};
    }
    if (traced()) {
        std::fprintf(stderr, "apply set %d root %.5f %.5f %.5f vel %.5f %.5f %.5f\n", int(velocitySet_), root.x, root.y,
                     root.z, vel_.x, vel_.y, vel_.z);
    }
    if (mode_ == kModeGround) {
        pos_ = {pos_.x + vel_.x * dt, pos_.y, pos_.z + vel_.z * dt};
    } else if (mode_ == kModeAir) {
        const float g = gravity_ < kUnset ? gravity_ : kWorldGravity;
        vel_.y += g * dt;
        pos_ = {pos_.x + vel_.x * dt, pos_.y + vel_.y * dt, pos_.z + vel_.z * dt};
    }
    if (mode_ != kModeThrown) {
        pos_ = {pos_.x + carry_.x, pos_.y + carry_.y, pos_.z + carry_.z};
    }
    if (mode_ != kModeInitial && mode_ != kModeThrown) {
        vel_ = {(pos_.x - lastPos_.x) / dt, (pos_.y - lastPos_.y) / dt, (pos_.z - lastPos_.z) / dt};
    }
    // The frame follows the animated joint first (virtual +0x50 at the start of 0x7e5c60), so the
    // last pose's body and the target's use the same frame: a change of frame moves the body at
    // once and its velocity carries the root's motion only (run rootdyn1: held, body velocity 0).
    updateBodyFrame();
    Vec3 from;
    Quat fromTurn;
    bodyPose(lastPos_, lastRot_, from, fromTurn);
    if (traced()) {
        Vec3 to;
        Quat tt;
        bodyPose(pos_, rot_, to, tt);
        std::fprintf(stderr, "write last %.5f pos %.5f from %.5f to %.5f\n", lastPos_.x, pos_.x, from.x, to.x);
    }
    applyMove_ = {pos_.x - lastPos_.x, pos_.y - lastPos_.y, pos_.z - lastPos_.z};   // 0x7e5efa
    applyRoot_ = pos_;                                                               // 0x7e5f34
    writeBody(dt, from, fromTurn);
    placeSensors();
    moveDamageBody();
    prevMode_ = mode_;
}

void BasicEnemy::writeBody(float dt, const Vec3& from, const Quat& fromTurn) {
    // 0x7e71b0.
    physics::PhysicsWorld* w = world_->physics;
    if (!w || body_ < 0) {
        if (mode_ == kModeThrown) {
            // Without a physics world: the flight by itself (world gravity, velocity, spin).
            vel_.y += kWorldGravity * dt;
            pos_ = {pos_.x + vel_.x * dt, pos_.y + vel_.y * dt, pos_.z + vel_.z * dt};
        }
        return;
    }
    using physics::rigid::MotionType;
    if (mode_ == kModeInitial) {
        if (prevMode_ != kModeInitial) {
            w->setMotion(body_, MotionType::Fixed);
            w->setFilters(body_, 0, 0xffffffffu);
        }
        return;
    }
    if (prevMode_ == kModeInitial) {
        w->setMotion(body_, MotionType::Active);
        w->setFilters(body_, kBodySelf, 0xffffffffu);
    }
    if (mode_ == kModeThrown) {
        if (prevMode_ != kModeThrown) {
            // Entering: the spin is free (FLT_MAX, 0x1251c18); the body stays where it is (the pose
            // and zero velocities are written only with flag +0x79, not modelled; run combat7: the
            // torso leaves from its resting place).
            w->setMaxAngularVelocity(body_, kUnset);
        }
        if (velocitySet_) {   // +0x6e / +0x6f: the velocities a hit gave, once
            w->setLinearVelocity(body_, vel_);
        }
        if (angularSet_) {
            w->setAngularVelocity(body_, angular_);
        }
        // The controller's gravity instead of the world's (as a force, +0x60).
        w->cancelGravity(body_);
        const float g = gravity_ < kUnset ? gravity_ : kWorldGravity;
        const physics::rigid::RigidBody& m = w->library().body(body_).mass;
        w->applyForce(body_, {0, m.mass * g, 0}, {}, w->timeStep());
        return;
    }
    if (prevMode_ == kModeThrown) {
        angular_ = {};
    }
    if (prevMode_ != mode_) {
        w->setMaxAngularVelocity(body_, 0);
    }
    // Put the body at the last root pose and give it the velocities that reach this one.
    Vec3 to;
    Quat toTurn;
    bodyPose(pos_, rot_, to, toTurn);
    w->setPose(body_, from, fromTurn);
    w->moveTo(body_, to);
    w->turnTo(body_, toTurn);
    w->cancelGravity(body_);
}

// ---- Sensors ----------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kWallTarget = 0xd7a63fdfu;     // root dynamics +0x294 (0x2da770: +0x3ac0)
constexpr std::uint32_t kSensorSelf = 0x40;            // 0x7e82f0 / 0x7e7bb0 once the mode leaves 1
constexpr std::uint32_t kBlockingBits = 0xff7dff9fu;   // what counts as blocking (0x2e2c60)
constexpr std::uint32_t kEnemyBits = 0x1000;           // another enemy's body
constexpr std::uint32_t kSolidBits = 0xfffffff0u;      // anything but the low four bits
constexpr float kSensorRadius = 0.01f;                 // xmm1 of every 0x7de8a0 call in 0x2dba50
constexpr float kSensorMass = 200.0f;                  // 0x7deb20: the capsule's mass

// The turn that lays the library capsule's x axis along `axis` (unit).
Quat alongX(const Vec3& axis) {
    const Vec3 x{1, 0, 0};
    const Vec3 c{x.y * axis.z - x.z * axis.y, x.z * axis.x - x.x * axis.z, x.x * axis.y - x.y * axis.x};
    const float w = 1 + axis.x;
    if (w < 1e-6f) {
        return {0, 1, 0, 0};
    }
    const float n = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z + w * w);
    return {c.x / n, c.y / n, c.z / n, w / n};
}

}   // namespace

void BasicEnemy::attachSensors(physics::PhysicsWorld& world, float rootHeight) {
    // 0x2dba50 with the kind's sensor geometry; the forward reach starts at the RootCollision body's
    // height (its body frame's y times the scale: 0.0963 x 1.1 for enemy_standard, run sens1).
    const float s = scale_;
    const float sensorY = tuning_.number("SensorY", 0.05f) * s;
    const float up = tuning_.number("SensorUpHeight", 0.1f);
    const float down = tuning_.number("SensorDownHeight", 0.5f);
    const float h = rootHeight * s;
    if (!bodyWorld_) {
        bodyWorld_ = &world;
    }
    sensors_.clear();
    // A: obstacle ahead at the feet (target +0x294).
    sensors_.push_back({false, {0, sensorY, 0}, {0, 0, 1}, h + tuning_.number("SensorDistance", 0.05f),
                        kSensorRadius, kWallTarget});
    // B: ground ahead, from the feet down (target +0x298).
    sensors_.push_back({false, {0, sensorY, tuning_.number("SensorEdgeDistance", 0.1f)}, {0, -1, 0},
                        sensorY + down, kSensorRadius, kProbeTarget});
    // C: obstacle ahead one step higher (+0x294).
    sensors_.push_back({false, {0, sensorY + up, 0}, {0, 0, 1}, h + tuning_.number("SensorJumpUpDistance", 0.05f),
                        kSensorRadius, kWallTarget});
    // D: a sphere swept down further ahead, on the ground only (+0x298).
    Sensor d{true, {0, sensorY + up, h + tuning_.number("SensorDownDistance", 0.1f)}, {0, -1, 0},
             sensorY + up + down, kSensorRadius, kProbeTarget};
    d.modes = kModeGround;
    sensors_.push_back(d);
    // The root dynamics' own ground sensor (0x7de3fe, index kept at +0x350; run sens5th): a
    // vertical cylinder of the probe radius from the body's height down to ThresholdOfGround below
    // the root, ground mask (+0x298), in every mode. The grounded test reads it in modes 0x10 / 8.
    Sensor g{false, {0, h, 0}, {0, -1, 0}, h + threshold_, probeRadius_, kProbeTarget};
    g.cylinder = true;
    sensors_.push_back(g);
    // 0x7deb20: the capsules as trigger bodies, self 0 until the mode leaves 1; their contacts with
    // the owner's own bodies are left out (0x7df980; readSensors).
    physics::BodyOptions o;
    o.motion = physics::rigid::MotionType::Trigger;
    o.filterSelf = 0;
    o.surface.owner = std::uint32_t(placement_);
    o.surface.mask = 0;   // nothing queries a sensor
    for (Sensor& sn : sensors_) {
        if (!sn.sweep) {
            o.filterTarget = sn.target;
            sn.body = sn.cylinder ? world.addCylinder(sn.length / 2, sn.radius, kSensorMass, o)
                                  : world.addCapsule(sn.length / 2, sn.radius, kSensorMass, o);
        }
    }
    placeSensors();
}

void BasicEnemy::setSensors(bool on) {
    // 0x2e2dc0: the switch and every sensor's enabled flag.
    sensorsOn_ = on;
    for (std::size_t i = 0; i < sensors_.size() && i < kEnemySensors; ++i) {   // not the body's own
        sensors_[i].enabled = on;
    }
}

std::uint32_t BasicEnemy::sensorBits(int index, std::uint32_t mask) const {
    if (index < 0) {
        return contactBits_ & mask;
    }
    const Sensor& sn = sensors_[std::size_t(index)];
    return sn.hit ? sn.bits & mask : 0;
}

void BasicEnemy::readSensors() {
    // 0x7e0020 (start of the step, the root not yet read back): trigger capsules take the bits of
    // the bodies they touched in the last world step, the sweep runs now.
    physics::PhysicsWorld* w = world_->physics;
    contactBits_ = 0;
    if (!w) {
        return;
    }
    const auto selfBits = [&](int other) { return w->library().body(other).state.filterSelf; };
    // 0x7df980 switches off the pairs of the sensors with the owner's other bodies (0xca9930).
    const auto own = [&](int other) {
        if (other == body_ || other == damageBody_ || other == deadBody_) {
            return true;
        }
        for (const Sensor& sn : sensors_) {
            if (sn.body == other) {
                return true;
            }
        }
        return false;
    };
    if (body_ >= 0) {
        for (const physics::rigid::BodyContact& c : w->library().contactsOf(body_)) {
            if (!own(c.other)) {
                contactBits_ |= selfBits(c.other);
            }
        }
    }
    const Quat yaw = yawQuat(yawOf(rot_));   // the y Euler angle alone (0x7e0020 / 0x7e7bb0)
    for (Sensor& sn : sensors_) {
        sn.hit = false;
        sn.bits = 0;
        if (!sn.enabled || (sn.modes & mode_) == 0) {
            continue;
        }
        if (sn.sweep) {
            const Vec3 o = turnVec(yaw, sn.at), a = turnVec(yaw, sn.axis);
            const Vec3 from{pos_.x + o.x, pos_.y + o.y, pos_.z + o.z};
            const Vec3 to{from.x + a.x * sn.length, from.y + a.y * sn.length, from.z + a.z * sn.length};
            if (auto hit = w->castSphere(from, to, sn.radius, physics::kHitAll, {kBodySelf, sn.target});
                hit && hit->body >= 0) {
                sn.hit = true;
                sn.bits |= selfBits(hit->body);
            }
        } else if (sn.body >= 0) {
            for (const physics::rigid::BodyContact& c : w->library().contactsOf(sn.body)) {
                if (traced()) {   // research aid
                    std::fprintf(stderr, "sensor %d meets %d self %x own %d\n", int(&sn - sensors_.data()), c.other,
                                 selfBits(c.other), int(own(c.other)));
                }
                if (own(c.other)) {
                    continue;
                }
                sn.hit = true;
                sn.bits |= selfBits(c.other);
            }
        }
    }
}

void BasicEnemy::classifySensors() {
    // 0x2e2c60 (virtual +0x3b8, after the step): A obstacle, B ground ahead, C higher obstacle,
    // D ground sweep.
    sensorState_ = 0;
    if (!sensorsOn_ || sensors_.size() < 4) {
        return;
    }
    const bool contact = sensorBits(-1, kBlockingBits) != 0;
    const bool c = sensorBits(2, kSolidBits) != 0;
    if (sensorBits(0, kBlockingBits)) {
        sensorState_ = contact && sensorBits(0, kEnemyBits) ? 1 : 2 - int(c);
        return;
    }
    const bool b = sensorBits(1, kSolidBits) != 0, d = sensorBits(3, kSolidBits) != 0;
    if (traced()) {   // research aid
        std::fprintf(stderr, "classify A0 B%d C%d D%d grounded %d gd %.4f mode %d contact %d\n", int(b), int(c), int(d),
                     int(grounded()), groundDistance_, mode_, int(contact));
    }
    if (!grounded() && !c && d && b) {   // rising over ground ahead (rootdyn virtual +0x40: hypothesis)
        sensorState_ = 3;
        return;
    }
    if (!c && !d) {
        sensorState_ = 4;
        return;
    }
    sensorState_ = contact ? 2 - int(c) : 0;
}

void BasicEnemy::placeSensors() {
    // 0x7e7bb0 (end of the apply): the capsules put at the new root, turned by its yaw; self bits
    // only while the mode is not 1.
    physics::PhysicsWorld* w = world_ ? world_->physics : bodyWorld_;
    if (!w) {
        return;
    }
    const Quat yaw = yawQuat(yawOf(rot_));   // the y Euler angle alone (0x7e0020 / 0x7e7bb0)
    for (const Sensor& sn : sensors_) {
        if (sn.body < 0) {
            continue;
        }
        const Vec3 mid{sn.at.x + sn.axis.x * sn.length / 2, sn.at.y + sn.axis.y * sn.length / 2,
                       sn.at.z + sn.axis.z * sn.length / 2};
        const Vec3 o = turnVec(yaw, mid);
        w->setPose(sn.body, {pos_.x + o.x, pos_.y + o.y, pos_.z + o.z}, mul(yaw, alongX(sn.axis)));
        w->setFilters(sn.body, mode_ == kModeInitial ? 0 : kSensorSelf, sn.target);
    }
}

bool BasicEnemy::groundSweep(Vec3& at, float& length, float& radius) const {
    if (sensors_.size() < 4) {
        return false;
    }
    at = sensors_[3].at;
    length = sensors_[3].length;
    radius = sensors_[3].radius;
    return true;
}

float BasicEnemy::stressSample() const {
    const Vec3 m{applyMove_.x, 0, applyMove_.z};
    const float len = std::sqrt(m.x * m.x + m.z * m.z);
    if (len <= 1.1920929e-7f) {
        return 0;
    }
    const float along = (m.x * (m.x + push_.x) + m.z * (m.z + push_.z)) / len;
    return 1 - along / len;
}

}   // namespace beacon::game::enemy
