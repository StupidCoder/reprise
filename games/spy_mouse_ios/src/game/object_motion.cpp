#include "game/object_motion.hpp"

#include "game/tuning.hpp"

#include <algorithm>
#include <cmath>

namespace doc::game {
namespace {

constexpr float kPi = 3.14159274f, kTwoPi = 6.28318548f;

}  // namespace

float wrapAngle(float radians) {
    float a = std::fmod(radians, kTwoPi);
    if (a < 0.0f) a = a + kTwoPi;
    return a;
}

float turnTowards(float heading, float aim, float rate) {
    const float target = wrapAngle(aim);
    const float h = wrapAngle(heading);
    float d = std::fmod(target - h, kTwoPi);
    if (d > kPi) d = d + -kTwoPi;
    if (d < -kPi) d = d + kTwoPi;
    float step = d * rate;
    step = step * 0.318309873f;
    const float limit = std::fabs(d);
    step = std::min(limit, std::max(-limit, step));
    return wrapAngle(h + step);
}

float walkRoute(RouteWalk& walk, Vec2 position, float reach, float speed, float& aim, float& heading,
                const std::function<void(int previous, int next)>& beforeStep) {
    const CatRoute& route = walk.route;
    const int count = int(route.points.size());
    if (walk.index < 0 || walk.index >= count) return 0.0f;
    auto point = [&](int i) { return route.points[std::size_t(i)].position; };
    const float reachSquared = reach * reach;
    float dx = point(walk.index).x - position.x;
    float dy = point(walk.index).y - position.y;
    for (;;) {
        if (dx * dx + dy * dy > reachSquared) break;
        if (beforeStep) beforeStep(walk.index - walk.direction, walk.index + walk.direction);
        const int previous = walk.index;
        walk.index += walk.direction;
        if (walk.index >= 0 && walk.index < count) {
            dx = point(walk.index).x - position.x;
            dy = point(walk.index).y - position.y;
            continue;
        }
        if (walk.index < 0 && route.startMode == 2 && route.endMode == 2) {
            walk.index = count - 2;
            break;
        }
        switch (route.endMode) {
        case 0:
            if (walk.index < 2) break;
            walk.index = previous;
            heading = aim = std::atan2(point(previous).y - point(previous - 1).y, point(previous).x - point(previous - 1).x);
            return 0.0f;
        case 1:
            walk.index -= 2 * walk.direction;
            walk.direction = -walk.direction;
            dx = point(walk.index).x - position.x;
            dy = point(walk.index).y - position.y;
            heading = aim = std::atan2(dy, dx);
            break;
        case 2:
            walk.direction = 1;
            walk.index = 0;
            break;
        default:
            break;  // (mode 3 links another route; not on the reconstructed levels)
        }
        break;
    }
    if (walk.index < count) aim = std::atan2(dy, dx);
    return speed;
}

void stepAlongHeading(Vec2& position, Vec2& velocity, float heading, float speed, int dtMs, float radius,
                      const LevelGeometry& walls, float viewLeft) {
    const float move = (float(dtMs) * speed) * 0.0500000007f;
    velocity = {libCos(heading) * move, libSin(heading) * move};
    Vec2 next = position + velocity;
    // Object::updatePositions @0x115430 (1.1.1): pushed off the walls, it takes the pushed place.
    walls.collideSphere(next, radius, viewLeft, nullptr, kMovementIgnoredWalls);
    position = next;
}

// travelArc @0x19d9e4: `timeMs` runs on by the tick; the flight takes `msPerUnit` per unit of
// distance; on the way the point bulges up by 0.006 x height x (h^2 - (s - h)^2), s the
// distance travelled and h half the whole. Returns true on arrival (the point is not moved).
bool travelArc(Vec2 start, Vec2 target, Vec2& point, int& timeMs, float height, float msPerUnit) {
    timeMs += kTickMs;
    Vec2 dir = target - start;
    const float length = dir.normalise(1.0f);
    const int total = int(length * msPerUnit);
    if (timeMs >= total) return true;
    const float f = float(timeMs) / float(total);
    const float travelled = f * length;
    const float half = length * 0.5f;
    point = start + dir * travelled;
    const float off = std::fabs(travelled - half);
    point.y = point.y + ((half * half - off * off) * height) * 0.00600000005f;
    return false;
}

}  // namespace doc::game
