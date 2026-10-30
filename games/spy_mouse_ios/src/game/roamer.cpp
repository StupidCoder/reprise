#include "game/roamer.hpp"

#include <algorithm>
#include <cmath>

namespace doc::game {
namespace {

constexpr float kPi = 3.14159274f, kTwoPi = 6.28318548f;
constexpr float kCarSpeed = 4.9f;            // Values: "car_speed"
constexpr float kButterflySpeed = 0.9f;      // "butterfly_speed"
constexpr int kCarWaitMin = 28000, kCarWaitMax = 42000;          // "car_appear_time_min/max"
constexpr int kButterflyWaitMin = 6000, kButterflyWaitMax = 12000;  // "butterfly_appear_time_min/max"

float wrapPositive(float a) {
    float r = std::fmod(a, kTwoPi);
    if (r < 0.0f) r = r + kTwoPi;
    return r;
}

int drawWait(GameRandom& random, int lo, int hi) {
    if (hi == lo) return lo;
    return lo + random.next() % (hi - lo);
}

}  // namespace

Roamer::Roamer(int objectId, int type, Vec2 position, float radius, CatRoute route, GameRandom& random)
    : objectId_(objectId), type_(type), position_(position), radius_(radius), route_(std::move(route)) {
    if (type >= 410 && type <= 412) {
        speed_ = kCarSpeed;
        waitMs_ = drawWait(random, kCarWaitMin, kCarWaitMax);
    } else {
        speed_ = kButterflySpeed;
        if (type == 421) waitMs_ = drawWait(random, kButterflyWaitMin, kButterflyWaitMax);
    }
    // Object::loadAssets: the butterfly's three frames at 200 ms.
    anim.setSheet(48, 16, 16.0f);
    anim.setAnim(0, 2, SpriteAnim::Loop, 200);
}

float Roamer::followRoute(float speed) {
    const int count = int(route_.points.size());
    if (index_ < 0 || index_ >= count) return 0.0f;
    auto point = [&](int i) { return route_.points[std::size_t(i)].position; };
    float dx = point(index_).x - position_.x;
    float dy = point(index_).y - position_.y;
    const float reach = radius_ * radius_;
    for (;;) {
        if (reach < dx * dx + dy * dy) break;
        const int previous = index_;
        index_ += direction_;
        if (index_ >= count || index_ < 0) {
            switch (route_.endMode) {
            case 0:
                if (index_ > 1) {
                    // Stopped at the last point, facing along the last stretch.
                    index_ = previous;
                    const Vec2 last = point(previous), before = point(previous - 1);
                    heading_ = rotation_ = std::atan2(last.y - before.y, last.x - before.x);
                    return 0.0f;
                }
                break;
            case 1:
                direction_ = -direction_;
                index_ = index_ + direction_ * 2;
                heading_ = rotation_ = std::atan2(point(index_).y - position_.y, point(index_).x - position_.x);
                break;
            case 2:
                direction_ = 1;
                index_ = 0;
                break;
            default: break;
            }
            if (index_ >= 0 && index_ < count) heading_ = std::atan2(dy, dx);
            return speed;
        }
        dx = point(index_).x - position_.x;
        dy = point(index_).y - position_.y;
    }
    heading_ = std::atan2(dy, dx);
    return speed;
}

void Roamer::update(int dtMs, const LevelGeometry& walls, float viewLeft, GameRandom& random) {
    float speed = 0.0f;
    // Object::update: the picture runs first, waiting or not (run l111_explore).
    if (type_ == 420 || type_ == 421) anim.update(dtMs);
    if (waitMs_ < 1) {
        speed = followRoute(speed_);
        if (type_ == 420 || type_ == 421) anim.update(dtMs);  // (and again while flying)
        if (speed == 0.0f) waitMs_ = drawWait(random, kCarWaitMin, kCarWaitMax);
    } else {
        waitMs_ -= dtMs;
        if (waitMs_ < 1) {
            index_ = 0;
            if (!route_.points.empty()) position_ = route_.points.front().position;
        }
    }
    // The generic turn (rate 1 × speed / pi of the difference, never past it) and move.
    const float aim = wrapPositive(heading_);
    const float h = wrapPositive(rotation_);
    float d = std::fmod(aim - h, kTwoPi);
    if (d > kPi) d = d + -kTwoPi;
    if (d < -kPi) d = d + kTwoPi;
    float step = d * (1.0f * speed);
    step = step * 0.318309873f;
    const float limit = std::fabs(d);
    step = std::min(limit, std::max(-limit, step));
    rotation_ = wrapPositive(h + step);
    const float move = (float(dtMs) * speed) * 0.0500000007f;
    Vec2 next = position_ + Vec2{libCos(rotation_) * move, libSin(rotation_) * move};
    if (type_ == 420 || type_ == 421) {
        position_ = next;  // butterflies fly over walls
        return;
    }
    // Object::updatePositions @0x115430: pushed off the walls, it takes the pushed place.
    walls.collideSphere(next, radius_, viewLeft, nullptr, kMovementIgnoredWalls);
    position_ = next;
}

}  // namespace doc::game
