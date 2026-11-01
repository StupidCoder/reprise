// =============================================================================================
// WalkPlanner.hpp - routes over the course floors for touch control (enhancement)
// =============================================================================================
//
// Not in the original: tap to walk and the double tap's walk to an object steer the stick
// along a route instead of straight at the target, so the protagonist goes around a block
// rather than pressing against its wall. The route is planned on a grid over the course
// collision as it is when the tap lands: each cell's floors come from a vertical ray (faces whose
// normal points up), a step between neighbouring cells may rise or fall a little (slopes) and
// must not cross a wall at knee height. Cells next to a ledge cost more, so the route keeps off
// edges where it can.
// Two more kinds of step: a drop off a ledge onto a floor that is there below (less than the
// game's panic-fall height, with room to land; one-way, and dearer than walking, so a slope down
// wins when there is one), and a ladder in either direction (walk into its foot, or off the ledge
// at its top, then climb with the stick the ladder reads).
// The route may be at most about twice the straight distance: it rounds corners and blocks and
// takes the obvious ladder or drop, but does not find the way through the course (no lifts, no
// long detours; the puzzles stay the player's). The player's own movement rules still decide what
// happens.
// =============================================================================================
#pragma once

#include "game/Collision.hpp"

#include <vector>

namespace captain_toad::session {

// A usable ladder: its foot and top on the ladder axis, and the side it is climbed from.
struct LadderSpan {
    captain_toad::RecoveredVec::V3 bottom, top, front;
};

struct WalkRoute {
    struct Point {
        captain_toad::RecoveredVec::V3 pos;    // a cell centre on its floor
        int climb = 0;                         // reached by ladder: 1 climbing up, -1 down (0: walking or a drop)
        captain_toad::RecoveredVec::V3 grab;   // a ladder: where to walk to take hold of it
    };
    std::vector<Point> points;     // start excluded
    size_t next = 0;               // the first point not passed yet
    bool climbing = false;         // on a ladder (toward points[next] when planned)
    int climbDirection = 0;        // 1 up, -1 down
    bool unplannedClimb = false;   // a ladder caught him off the route's plan

    bool empty() const { return points.empty(); }
    // The point to steer for from pos while walking: the farthest of the next few points within
    // reach (the grid's corners smoothed), advancing past the points already reached; a ladder's
    // grab point once the ladder is next.
    captain_toad::RecoveredVec::V3 steerPoint(const captain_toad::RecoveredVec::V3& pos);
};

// A route from `from` to within `reach` (horizontally, on about the same floor) of `to`; empty
// when there is none (the caller then walks straight).
WalkRoute planWalk(const std::vector<captain_toad::RecoveredCollision::Part>& parts,
                   const std::vector<LadderSpan>& ladders, const captain_toad::RecoveredVec::V3& from,
                   const captain_toad::RecoveredVec::V3& to, float reach);

}   // namespace captain_toad::session
