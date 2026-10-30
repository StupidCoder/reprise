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

bool LevelGeometry::lineTouches(Vec2 a, Vec2 b, float viewLeft, int mode) const {
    const float right = viewLeft + kScreenWidth;
    for (const Wall& wall : walls_) {
        if (!wall.enabled) continue;
        if (mode != 0 && ignoredBySightTest(wall.type, mode)) continue;
        if (outsideView(wall, viewLeft, right)) continue;
        Vec2 at;
        if (segmentCrossing(a, b, wall.a, wall.b, at)) return true;
    }
    return false;
}

bool LevelGeometry::clipLine(Vec2 from, Vec2& to, float viewLeft) const {
    // The end point is first pushed off solid walls. The original passes the view position
    // where the ignored edge type belongs and 0 as the view position (kept as found).
    SphereOptions push;
    push.plainWallsOnly = true;
    push.ignoreType = int(viewLeft);
    push.viewLeft = 0.0f;
    collideSphere(to, 10.0f, push);

    // The two crossed walls nearest to `from`.
    const float right = viewLeft + kScreenWidth;
    int nearest = -1, second = -1;
    float nearestDistance = 99999.9f, secondDistance = 99999.9f;
    for (std::size_t i = 0; i < walls_.size(); ++i) {
        const Wall& wall = walls_[i];
        if (!wall.enabled) continue;
        if (!(wall.type < 0x6A || wall.type == 0x6D || wall.type == 0x79)) continue;
        if (outsideView(wall, viewLeft, right)) continue;
        Vec2 at;
        if (!segmentCrossing(from, to, wall.a, wall.b, at)) continue;
        const float distance = (from - at).length();
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

    if (second >= 0) {
        // Two walls meeting at a corner: step 13 units away from both, along the bisector.
        const Wall& w1 = walls_[nearest];
        const Wall& w2 = walls_[second];
        Vec2 corner, end1, end2;
        bool shared = true;
        if (w1.vertexA == w2.vertexA) corner = w1.a, end1 = w1.b, end2 = w2.b;
        else if (w1.vertexA == w2.vertexB) corner = w1.a, end1 = w1.b, end2 = w2.a;
        else if (w1.vertexB == w2.vertexA) corner = w1.b, end1 = w1.a, end2 = w2.b;
        else if (w1.vertexB == w2.vertexB) corner = w1.b, end1 = w1.a, end2 = w2.a;
        else shared = false;
        if (shared) {
            Vec2 d1 = corner - end1;
            Vec2 d2 = corner - end2;
            d1.normalise(1.0f);
            d2.normalise(1.0f);
            to = corner + (d1 + d2) * 13.0f;
            return true;
        }
    }
    if (nearest < 0) return false;
    // One wall: move onto it, one unit back toward `from`, then off the walls again.
    const Wall& wall = walls_[nearest];
    closestOnSegment(wall.a, wall.b, to, to);
    Vec2 back = from - to;
    back.normalise(1.0f);
    to += back;
    collideSphere(to, 10.0f, push);
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

// MapCollision::collideSphereLine: one wall against the sphere. Returns the contact kind (0 if
// none); `final` is set for edges that end the whole test (kinds 11 and 12).
int LevelGeometry::collideWall(std::size_t i, Vec2& centre, float radius, const SphereOptions& options, bool& final) const {
    const Wall& wall = walls_[i];
    final = false;
    Vec2 closest;
    if (circleSegmentContact(centre, radius, wall.a, wall.b, closest) == 0) return 0;

    int kind = 0;
    if (!options.plainWallsOnly) {
        const int t = wall.type;
        auto edgeId = [&] { return Vec2{float(i), float(wall.tag)}; };
        if (t == 0x7A) {
            final = true;  // stops at the first such edge
            return 12;
        }
        if ((t >= 0x7D && t <= 0x81) || t == 0x6C || t == 0x87) {
            if (options.pushNormal) *options.pushNormal = edgeId();
            final = true;  // stops at the first such edge
            return 11;
        }
        switch (t) {
        case 0x71: {
            kind = 10;
            if (options.pushNormal) {
                Vec2 n = wall.b - wall.a;
                n.normalise(1.0f);
                *options.pushNormal = n;
            }
            break;
        }
        case 0x72: kind = 9; if (options.pushNormal) *options.pushNormal = edgeId(); break;
        case 0x7C: kind = 14; if (options.pushNormal) *options.pushNormal = edgeId(); break;
        case 0x73: kind = 8; break;
        case 0x6E: kind = 5; break;
        case 0x90: kind = 17; break;
        case 0x8C: kind = 15; break;
        case 0x6F: kind = 1; break;
        case 0x70: kind = 2; break;
        case 0x7B: kind = 13; break;
        case 0x8D:
            kind = 16;
            if (options.pushNormal) *options.pushNormal = {float(wall.tag), float(wall.tag)};
            break;
        case 0x78: kind = 6; break;
        case 0x8F: kind = 7; break;
        default: kind = 0; break;
        }
    }
    // Edge types that always act as walls for this test.
    if (wall.type == 0) {
        if (kind <= 3) kind = 3;
    } else if (wall.type == 0x6D) {
        if (kind <= 9) kind = 4;
    } else if (wall.type == 0x79) {
        if (kind < 4) kind = 3;
    }
    if (kind == 3 || kind == 4 || kind == 6) {
        // Push the centre out along the closest-point direction by the overlap.
        Vec2 push = closest - centre;
        float distance = push.normalise(1.0f);
        if (options.pushNormal) *options.pushNormal += push;
        push *= distance - radius;
        centre += push;
    }
    return kind;
}

int LevelGeometry::collideSphere(Vec2& centre, float radius, const SphereOptions& options) const {
    const float left = options.viewLeft;
    const float right = options.viewLeft + kScreenWidth;
    int best = 0;
    for (std::size_t i = 0; i < walls_.size(); ++i) {
        const Wall& wall = walls_[i];
        if (!wall.enabled) continue;
        if (options.ignoreType >= 1 && wall.type == options.ignoreType) continue;
        if (wall.a.x < left && wall.b.x < left) continue;
        if (wall.a.x > right && wall.b.x > right) continue;
        bool final = false;
        const int kind = collideWall(i, centre, radius, options, final);
        if (final) return kind;
        if (kind != 0) best = best == 0 ? kind : std::min(best, kind);
    }
    return best;
}

int LevelGeometry::collideSphereOfType(Vec2& centre, float radius, int type, Vec2& found, float viewLeft) const {
    LevelGeometry::SphereOptions options;
    options.pushNormal = &found;
    options.viewLeft = viewLeft;
    int best = 0;
    for (std::size_t i = 0; i < walls_.size(); ++i) {
        const Wall& wall = walls_[i];
        if (!wall.enabled || wall.type != type) continue;
        if (wall.a.x < viewLeft && wall.b.x < viewLeft) continue;
        if (wall.a.x > viewLeft + kScreenWidth && wall.b.x > viewLeft + kScreenWidth) continue;
        bool final = false;
        const int kind = collideWall(i, centre, radius, options, final);
        if (kind > 0) best = kind;
        if (kind == 11) {
            found.x = float(i);
            return best;
        }
    }
    return best;
}

}  // namespace doc::game
