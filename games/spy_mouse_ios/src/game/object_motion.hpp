// How objects that only walk a route move: the general rules of Object::updatePath @0x115614,
// the turn at the end of the controllers' updates and Object::updatePositions @0x115430. Used
// by world 2's boss fight (the Spitter and the rail spheres); the cats, the droid and the
// roamers keep their own copies, each with the extra rules of its kind.
#pragma once

#include "game/cat.hpp"
#include "game/geometry.hpp"
#include "game/level_geometry.hpp"

#include <functional>

namespace doc::game {

// An angle in [0, 2pi) (fmodf, then 2pi added to a negative rest).
float wrapAngle(float radians);

// The heading turned towards `aim` by rate / pi of the difference, never past it.
float turnTowards(float heading, float aim, float rate);

// A route being walked (Object +0x114 path, +0x16c point, +0x174 direction).
struct RouteWalk {
    CatRoute route;
    int index = 0;
    int direction = 1;
};

// Object::updatePath: heads for the route point `walk.index`; within `reach` of it the point
// is passed and the next one taken (several in one tick if need be). Past an end: a route that
// starts and ends in mode 2 runs on from its last-but-one point; else by its end mode the
// walker stops facing along the last leg (0: returns 0), turns back (1) or starts again at
// point 0 (2). `aim` (+0x10c) is set towards the last point looked at; a stop or a turn back
// sets `heading` (+0x108) too. `beforeStep(previous, next)` runs for each point reached, before
// the index moves on; it may flip `walk.direction`. (Pauses and cut points, which only cats'
// routes have, are not handled here.)
float walkRoute(RouteWalk& walk, Vec2 position, float reach, float speed, float& aim, float& heading,
                const std::function<void(int previous, int next)>& beforeStep);

// Object::updatePositions for an object that is not a cat: one step of `speed` along
// `heading` (0.05 per ms); the step is kept when the walls it then touches allow it (none,
// a plain wall that pushed it back, or edges that only trigger things: kinds 7, 11, 13..16).
// `velocity` (+0x1ac) gets the step.
void stepAlongHeading(Vec2& position, Vec2& velocity, float heading, float speed, int dtMs, float radius,
                      const LevelGeometry& walls, float viewLeft);

// travelArc @0x19d9e4: a flight from `start` to `target` bulging up by `height`, `msPerUnit`
// per unit; `timeMs` runs on by the tick. Returns true on arrival (`point` not moved then).
bool travelArc(Vec2 start, Vec2 target, Vec2& point, int& timeMs, float height, float msPerUnit);

}  // namespace doc::game
