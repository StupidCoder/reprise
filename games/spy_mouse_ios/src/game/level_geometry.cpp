#include "game/level_geometry.hpp"

#include <algorithm>

namespace doc::game {

LevelGeometry::LevelGeometry(const data::MapFile& map) {
    for (const data::MapLine& line : map.lines) {
        const data::MapVertex& a = map.vertices[line.a];
        const data::MapVertex& b = map.vertices[line.b];
        walls_.push_back({{a.x, a.y}, {b.x, b.y}, line.a, line.b, line.type, line.tag, line.enabled});
    }
}

void LevelGeometry::setTagEnabled(int tag, bool enabled, int type) {
    for (Wall& wall : walls_)
        if (wall.tag == tag && (type < 0 || wall.type == type)) wall.enabled = enabled;
}

void LevelGeometry::toggleTag(int tag) {
    for (Wall& wall : walls_)
        if (wall.tag == tag) wall.enabled = !wall.enabled;
}

Wall* LevelGeometry::firstWithTag(int tag) {
    for (Wall& wall : walls_)
        if (wall.tag == tag) return &wall;
    return nullptr;
}

namespace {

// Walls entirely left or right of the view's x range are skipped by every query.
bool outsideView(const Wall& wall, float left, float right) {
    if (wall.a.x < left && wall.b.x < left) return true;
    return wall.a.x > right && wall.b.x > right;
}

// Edge types that a line-of-sight test with mode != 0 ignores: trigger edges that do not
// block (MapCollision::doesLineTouch jump table at 0x128d3e). Type 0x91 is ignored in mode 2.
bool ignoredBySightTest(int type, int mode) {
    switch (type) {
    case 0x6C: case 0x6F: case 0x70: case 0x71: case 0x72: case 0x73: case 0x7A: case 0x7B:
    case 0x7D: case 0x7E: case 0x7F: case 0x80: case 0x81: case 0x87: case 0x8D: case 0x90:
        return true;
    case 0x91:
        return mode == 2;
    default:
        return false;
    }
}

}  // namespace

bool segmentCrossing(Vec2 p1, Vec2 p2, Vec2 a, Vec2 b, Vec2& at) {
    // Rotate both segments into the frame of p1p2 (p1 at the origin, p2 on the +x axis).
    Vec2 along = p2 - p1;
    a -= p1;
    b -= p1;
    const float length = along.length();
    const float c = along.x / length;
    const float s = along.y / length;
    const Vec2 ra{a.x * c + a.y * s, a.y * c - a.x * s};
    const Vec2 rb{b.x * c + b.y * s, b.y * c - b.x * s};
    // (The tests are written as the original's: a zero-length p1p2 makes everything NaN, which
    // passes the first two and fails the last, so it crosses nothing; run l44_win, 5962.)
    if (ra.y < 0.0f && rb.y < 0.0f) return false;    // both ends below the line
    if (0.0f <= ra.y && 0.0f <= rb.y) return false;  // both ends on or above it
    // Where ab crosses the axis, measured along p1p2.
    const float t = rb.x + ((ra.x - rb.x) * rb.y) / (rb.y - ra.y);
    if (!(-2.0f <= t) || !(t <= length + 2.0f)) return false;
    at = {p1.x + t * c, p1.y + t * s};
    return true;
}

int closestOnSegment(Vec2 a, Vec2 b, Vec2 p, Vec2& closest) {
    const Vec2 toP = p - a;
    const Vec2 along = b - a;
    float t = along.dot(toP) / along.dot(along);
    int result = 1;
    if (t < 0.0f) {
        result = 2;
        t = 0.0f;
    } else if (t > 1.0f) {
        result = 2;
        t = 1.0f;
    }
    closest = a + along * t;
    return result;
}

// Untagged walls count only where they overlap the view's x range; tagged ones always do
// (the range test in every MapCollision query, 1.1.1).
bool LevelGeometry::inRange(const Wall& wall, float viewLeft) const {
    return wall.tag != 0 || !outsideView(wall, viewLeft, viewLeft + kScreenWidth);
}

bool LevelGeometry::lineTouches(Vec2 a, Vec2 b, float viewLeft, int mode) const {
    for (const Wall& wall : walls_) {
        if (!wall.enabled) continue;
        if (mode != 0 && ignoredBySightTest(wall.type, mode)) continue;
        if (!inRange(wall, viewLeft)) continue;
        Vec2 at;
        if (segmentCrossing(a, b, wall.a, wall.b, at)) {
            return true;
        }
    }
    return false;
}

bool LevelGeometry::lineTouchesType(Vec2 a, Vec2 b, int type, float viewLeft) const {
    if (type == -1) return lineTouches(a, b, viewLeft, -1);
    for (const Wall& wall : walls_) {
        if (!wall.enabled || wall.type != type) continue;
        if (!inRange(wall, viewLeft)) continue;
        Vec2 at;
        if (segmentCrossing(a, b, wall.a, wall.b, at)) return true;
    }
    return false;
}

bool LevelGeometry::clipLine(Vec2 from, Vec2& to, float viewLeft) const {
    if (from.x == to.x && from.y == to.y) return false;
    // The two crossed solid walls nearest to `from` (the second is found but not used).
    int nearest = -1, second = -1;
    float nearestDistance = 99999.9f, secondDistance = 99999.9f;
    for (std::size_t i = 0; i < walls_.size(); ++i) {
        const Wall& wall = walls_[i];
        if (!wall.enabled) continue;
        if (!(wall.type < 0x6A || wall.type == 0x6D || wall.type == 0x79)) continue;
        if (!inRange(wall, viewLeft)) continue;
        Vec2 at;
        if (!segmentCrossing(from, to, wall.a, wall.b, at)) continue;
        const float distance = (at - from).length();
        if (distance < nearestDistance) {
            secondDistance = nearestDistance;
            second = nearest;
            nearestDistance = distance;
            nearest = int(i);
        } else if (distance < secondDistance) {
            secondDistance = distance;
            second = int(i);
        }
    }
    (void)second;
    if (nearest < 0) return false;
    // Onto the wall, two units back toward `from`, then off the plain walls.
    const Wall& wall = walls_[std::size_t(nearest)];
    Vec2 back = from - to;
    back.normalise(1.0f);
    closestOnSegment(wall.a, wall.b, to, to);
    to += back * 2.0f;
    collideSphereSpecific(to, 12.0f, 0, nullptr, viewLeft);
    return true;
}

int circleSegmentContact(Vec2 centre, float radius, Vec2 a, Vec2 b, Vec2& closest) {
    Vec2 toCentre = centre - a;
    Vec2 along = b - a;
    float lengthSquared = along.dot(along);
    if (std::fabs(lengthSquared) < 0.001f) return 0;  // degenerate segment
    float t = toCentre.dot(along) / lengthSquared;
    int result = 1;
    if (t < 0.0f) {
        result = 2;
        t = 0.0f;
    } else if (t > 1.0f) {
        result = 2;
        t = 1.0f;
    }
    closest = a + along * t;
    Vec2 gap = {closest.x - centre.x, closest.y - centre.y};
    if (gap.dot(gap) > radius * radius) result = 0;
    return result;
}

namespace {

// The contact bit of a wall type (MapCollision::collideSphere's switch @0x13f5b4).
unsigned contactOf(int type) {
    if (type == 0x7A) return contact::k7A;
    if ((type >= 0x7D && type <= 0x81) || type == 0x6C || type == 0x87) return contact::kTrigger;
    switch (type) {
    case 0x71: return contact::kConveyor;
    case 0x72: return contact::k72;
    case 0x73: return contact::k73;
    case 0x7C: return contact::k7C;
    case 0x6E: return contact::k6E;
    case 0x90: return contact::k90;
    case 0x8C: return contact::k8C;
    case 0x6F: return contact::k6F;
    case 0x70: return contact::k70;
    case 0x7B: return contact::k7B;
    case 0x8D: return contact::kPaint;
    case 0x78: return contact::k78;
    case 0x8F: return contact::k8F;
    case 0x91: return contact::k91;
    case 0x6D: return contact::kBouncy;
    case 0x00: case 0x79: return contact::kSolid;
    default: return 0;
    }
}

}  // namespace

unsigned LevelGeometry::collideSphere(Vec2& centre, float radius, float viewLeft, Vec2* normal,
                                      std::span<const int> ignoredTypes) const {
    unsigned touched = 0;
    for (std::size_t i = 0; i < walls_.size(); ++i) {
        const Wall& wall = walls_[i];
        if (!wall.enabled) continue;
        if (std::find(ignoredTypes.begin(), ignoredTypes.end(), wall.type) != ignoredTypes.end()) continue;
        if (!inRange(wall, viewLeft)) continue;
        Vec2 closest;
        if (circleSegmentContact(centre, radius, wall.a, wall.b, closest) == 0) continue;
        const unsigned bit = contactOf(wall.type);
        if (normal) {
            if (bit == contact::kTrigger || bit == contact::k72 || bit == contact::k7C) {
                *normal = {float(i), float(wall.tag)};
            } else if (bit == contact::kConveyor) {
                Vec2 along = wall.b - wall.a;
                along.normalise(1.0f);
                *normal = along;
            } else if (bit == contact::kPaint) {
                *normal = {float(wall.tag), float(wall.tag)};
            }
        }
        if (bit == contact::kSolid || bit == contact::kBouncy) {
            // Out along the closest-point direction by the overlap.
            Vec2 push = closest - centre;
            const float distance = push.normalise(1.0f);
            if (normal) *normal += push;
            push *= distance - radius;
            centre += push;
        }
        touched |= bit;
    }
    return touched;
}

unsigned LevelGeometry::collideSphereLine(std::size_t i, Vec2 centre, float radius, float viewLeft) const {
    if (i >= walls_.size()) return 0;
    const Wall& wall = walls_[i];
    if (!inRange(wall, viewLeft)) return 0;
    Vec2 closest;
    if (circleSegmentContact(centre, radius, wall.a, wall.b, closest) == 0) return 0;
    // (The same bits as collideSphere's, but 0x91 counts for nothing here.)
    return wall.type == 0x91 ? 0 : contactOf(wall.type);
}

unsigned LevelGeometry::collideSphereSpecific(Vec2& centre, float radius, int type, Vec2* normal, float viewLeft) const {
    unsigned result = 0;
    for (std::size_t i = 0; i < walls_.size(); ++i) {
        const Wall& wall = walls_[i];
        if (!wall.enabled || (type != -1 && wall.type != type)) continue;
        const unsigned bits = collideSphereLine(i, centre, radius, viewLeft);
        if (bits != 0) result = bits;
        if (bits & contact::kTrigger) {
            if (normal) *normal = {float(i), float(wall.tag)};
            return result;
        }
        if (bits & contact::kSolid) {
            Vec2 closest;
            circleSegmentContact(centre, radius, wall.a, wall.b, closest);
            Vec2 push = closest - centre;
            const float distance = push.normalise(1.0f);
            if (normal) {
                // The wall's perpendicular.
                const Vec2 d = wall.a - wall.b;
                Vec2 across{d.y, -d.x};
                across.normalise(1.0f);
                *normal = across;
            }
            push *= distance - radius;
            centre += push;
        }
    }
    return result;
}

}  // namespace doc::game
