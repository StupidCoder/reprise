#include "game/turret.hpp"

#include <cmath>

namespace doc::game {
namespace {

// The turret's tuning (g_pVal +0x1dc..+0x1e8: "turret_fire_speed" 142, "turret_burst_count" 3,
// "turret_burst_cooldown" 700, "turret_fire_cooldown" 200), for the boss's launcher (tag 0x66)
// at 0.75 of the speed, 4 times the cooldown between bursts and 1.5 times the one between shots
// (whole numbers, cut).
constexpr int kShotSpeed = 106;          // int(142 * 0.75)
constexpr int kBurstCount = 3;
constexpr int kBurstCooldownMs = 2800;   // 700 * 4
constexpr int kShotIntervalMs = 300;     // int(200 * 1.5)
constexpr int kRestMs = 1000;            // at a stop before firing

// The rail's stops (+0x208), as heights; and the order they come in: 0, 2, 3, 1, 0, ...
float stopHeight(int stop) {
    switch (stop) {
    case 0: return 170.0f;
    case 1: case 2: return 115.0f;
    case 3: return 45.0f;
    default: return 0.0f;
    }
}
int nextStop(int stop) {
    static const int next[4] = {2, 0, 3, 1};
    return stop >= 0 && stop < 4 ? next[stop] : stop;
}

}  // namespace

bool Launcher::update(int dtMs, Shot& shot, bool& burstOver) {
    // ObjControllerSpawner::updateAnimations @0x1bb3fe: the boss's launcher faces 2π every
    // tick (its shots go right, a hair below the axis as the float's sine has it).
    heading_ = 6.28318548f;
    burstOver = false;
    clockMs_ -= dtMs;
    const int state = state_;  // (the state the tick began in picks the branch)
    if (state == kMoving) {
        // One unit a tick towards the stop's height; there, a rest.
        const float d = stopHeight(stop_) - position_.y;
        const float distance = std::fabs(d);
        if (distance >= 1.0f) {
            position_.y = position_.y + d / distance;
        } else {
            state_ = kResting;
            clockMs_ = kRestMs;
        }
    } else if (state == kResting) {
        if (clockMs_ < 1) state_ = kFiring;
    } else if (state == kReload) {
        state_ = kFiring;
        intervalMs_ = kShotIntervalMs;
        shotsLeft_ = kBurstCount;
        clockMs_ = kBurstCooldownMs;
    }
    if (state != kFiring) return false;
    if (clockMs_ < 1) state_ = kReload;
    // (Other launchers look for the mouse first; the boss's fires blind.)
    if (shotsLeft_ < 1) return false;
    clockMs_ = kBurstCooldownMs;
    if (intervalMs_ > 0) {
        intervalMs_ -= dtMs;
        return false;
    }
    // Along its heading, from 5 below its middle and its radius out.
    const Vec2 along{std::cos(heading_), std::sin(heading_)};
    shot.at = Vec2{position_.x, position_.y + 5.0f} + along * radius_;
    shot.velocity = along * float(kShotSpeed);
    shot.stop = stop_;
    intervalMs_ = kShotIntervalMs;
    --shotsLeft_;
    if (shotsLeft_ < 1) {
        burstOver = true;
        state_ = kMoving;
        stop_ = nextStop(stop_);
    }
    return true;
}

}  // namespace doc::game
