// The walls of a level and how round things collide with them (the original's Map line list
// and MapCollision).
#pragma once

#include "data/map_file.hpp"
#include "game/geometry.hpp"

#include <vector>

namespace doc::game {

// Result of a sphere test, ordered by priority: smaller non-zero values win when several
// walls are touched. Values follow the original (MapCollision::collideSphere @0x13f3e4).
enum class Contact : int {
    None = 0,
    Wall = 3,  // ordinary solid wall: the sphere is pushed out
    // Other values (1..17) belong to special edges (water, holes, doors, ...) and are kept as
    // plain numbers until their meaning is recovered.
};

struct Wall {
    Vec2 a, b;
    int vertexA = 0, vertexB = 0;  // map vertex numbers (shared corners are detected by number)
    int type = 0;  // 0 = solid wall
    int tag = 0;
    bool enabled = true;
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

    struct SphereOptions {
        Vec2* pushNormal = nullptr;   // receives the sum of push directions
        bool plainWallsOnly = false;  // true skips the special-edge classification
        int ignoreType = -1;          // walls of this type (>= 1) are ignored
        float viewLeft = 0.0f;        // only walls overlapping [viewLeft, viewLeft + 480] in x
    };

    // Pushes `centre` out of every wall it overlaps with radius `radius`; returns the
    // highest-priority contact kind (0 if none). Exact port of the original's rules.
    int collideSphere(Vec2& centre, float radius, const SphereOptions& options) const;

    // MapCollision::collideSphereSpecific @0x13f220: the sphere against the enabled walls of
    // one type only. For trigger edges (kind 11) `found` receives (wall index, wall tag) and
    // the test stops there; returns the contact kind (0 if none). Only the trigger edges'
    // case is used (by the boss droid).
    int collideSphereOfType(Vec2& centre, float radius, int type, Vec2& found, float viewLeft) const;

    // Whether segment ab crosses an enabled wall overlapping the view in x
    // (MapCollision::doesLineTouch @0x13f7e4). With `mode` != 0, special edges that only
    // trigger things (water, holes, doors' triggers, ...) are not counted.
    bool lineTouches(Vec2 a, Vec2 b, float viewLeft, int mode) const;

    // Keeps a drawn segment out of solid walls (MapCollision::clipLine @0x13fb44): `to` is
    // pushed off walls first; if from->to still crosses solid walls, `to` is moved back in
    // front of the nearest one (or out of the corner two of them form). Returns whether the
    // segment was clipped.
    bool clipLine(Vec2 from, Vec2& to, float viewLeft) const;

private:
    int collideWall(std::size_t i, Vec2& centre, float radius, const SphereOptions& options, bool& final) const;

    std::vector<Wall> walls_;
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
