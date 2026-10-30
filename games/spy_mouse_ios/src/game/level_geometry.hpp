// The walls of a level and how round things collide with them (the original's Map line list
// and MapCollision).
#pragma once

#include "data/map_file.hpp"
#include "game/geometry.hpp"

#include <span>
#include <vector>

namespace doc::game {

// What a sphere test touched (MapCollision::collideSphere @0x13f3e4, 1.1.1): one bit per kind
// of wall, all walls touched taken together. Solid walls (type 0 and 0x79) and bouncy ones
// (0x6d) also push the sphere out.
namespace contact {
constexpr unsigned k6F = 0x1, k70 = 0x2;
constexpr unsigned kSolid = 0x4;       // types 0 and 0x79
constexpr unsigned kBouncy = 0x8;      // type 0x6d
constexpr unsigned k6E = 0x10, k78 = 0x20, k8F = 0x40;
constexpr unsigned k73 = 0x80;
constexpr unsigned k72 = 0x100;        // the normal gets (wall index, tag)
constexpr unsigned kConveyor = 0x200;  // type 0x71: the normal gets the wall's direction
constexpr unsigned kTrigger = 0x400;   // types 0x6c, 0x7d..0x81, 0x87: (wall index, tag)
constexpr unsigned k7A = 0x800, k7B = 0x1000;
constexpr unsigned k7C = 0x2000;       // (wall index, tag)
constexpr unsigned k8C = 0x4000;
constexpr unsigned kPaint = 0x8000;    // type 0x8d: the normal gets (tag, tag)
constexpr unsigned k90 = 0x10000, k91 = 0x20000;
}  // namespace contact

// The walls a moving object ignores (Object::updatePositions's list C.738 @0x5252c0).
inline constexpr int kMovementIgnoredWalls[] = {0x7B, 0x73, 0x8D};

struct Wall {
    Vec2 a, b;
    int vertexA = 0, vertexB = 0;  // map vertex numbers (shared corners are detected by number)
    int type = 0;  // 0 = solid wall
    int tag = 0;
    bool enabled = true;
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(a, b, vertexA, vertexB, type, tag, enabled); }
};

class LevelGeometry {
public:
    explicit LevelGeometry(const data::MapFile& map);

    const std::vector<Wall>& walls() const { return walls_; }

    // Switches every wall with this tag (and, when `type` >= 0, this type) on or off: doors
    // and gates (Map::setEnabled @0x13c1ac).
    void setTagEnabled(int tag, bool enabled, int type = -1);
    // Flips every wall with this tag on or off (Map::toggleStates @0x13bfd4; the map's
    // decorations with the tag flip too, handled by the level).
    void toggleTag(int tag);
    void setWallEnabled(std::size_t index, bool enabled) { walls_[index].enabled = enabled; }
    // The first wall carrying `tag`, or null (Map::getLineFromTag @0x13c324).
    Wall* firstWithTag(int tag);

    // MapCollision::collideSphere @0x13f3e4: the sphere against every enabled wall except those
    // of the `ignoredTypes`; walls without a tag only when they overlap the view's x range
    // [viewLeft, viewLeft + 480] (tagged walls always count). Solid and bouncy walls push the
    // centre out (`normal` collects the push directions). Returns the contact bits.
    unsigned collideSphere(Vec2& centre, float radius, float viewLeft, Vec2* normal = nullptr,
                           std::span<const int> ignoredTypes = {}) const;
    // MapCollision::collideSphereLine @0x13effc: one wall, without pushing.
    unsigned collideSphereLine(std::size_t i, Vec2 centre, float radius, float viewLeft) const;
    // MapCollision::collideSphereSpecific @0x13f220: walls of one type (-1: all) one by one; a
    // trigger edge ends the test with (wall index, tag) in `normal`; solid walls push the
    // centre out (`normal`: the wall's perpendicular). Returns the bits of the last wall touched.
    unsigned collideSphereSpecific(Vec2& centre, float radius, int type, Vec2* normal, float viewLeft) const;

    // Whether segment ab crosses an enabled wall (MapCollision::doesLineTouch @0x13f7e4; untagged
    // walls only within the view's x range). With `mode` != 0, special edges that only trigger
    // things (water, holes, doors' triggers, ...) are not counted; 0x91 only in mode 2.
    bool lineTouches(Vec2 a, Vec2 b, float viewLeft, int mode) const;
    // MapCollision::doesLineTouchSpecific @0x13f9a4: only walls of one type (-1: lineTouches
    // with mode -1).
    bool lineTouchesType(Vec2 a, Vec2 b, int type, float viewLeft) const;

    // Keeps a drawn segment out of solid walls (MapCollision::clipLine @0x13fb44): if from->to
    // crosses solid walls, `to` moves onto the nearest one, 2 units back toward `from`, and is
    // pushed off plain walls (radius 12). Returns whether the segment was clipped.
    bool clipLine(Vec2 from, Vec2& to, float viewLeft) const;

private:
    bool inRange(const Wall& wall, float viewLeft) const;

    std::vector<Wall> walls_;
public:
    // Quick saves (state_io.hpp).
    template <class Io>
    void io(Io& io) { io(walls_); }
};

// Closest point of segment ab to `centre`; returns 0 if farther than `radius`, 1 if the
// closest point lies inside the segment, 2 if it is an end point.
// (CircleSegmentIntersect @0x12f444)
int circleSegmentContact(Vec2 centre, float radius, Vec2 a, Vec2 b, Vec2& closest);

// Where segment p1p2 crosses segment ab, with 2 units of tolerance along p1p2
// (lineSegmentIntersection @0x12f6e8). Returns false if they do not cross.
bool segmentCrossing(Vec2 p1, Vec2 p2, Vec2 a, Vec2 b, Vec2& at);

// Closest point to p on segment ab; returns 1 inside, 2 at an end (snapClosest @0x12f3a0).
int closestOnSegment(Vec2 a, Vec2 b, Vec2 p, Vec2& closest);

}  // namespace doc::game
