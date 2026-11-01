// Recovered sensor push (original game, function 0x2db770).
//
// Characters in the game carry invisible spheres called sensors (for example a "Head" or a "Body"
// sphere attached to a joint). When two characters walk into each other, the game pushes one of
// them out of the other's sphere instead of running a full physics collision.
//
// How the original computes a push for a receiver ("mine") against another sensor ("other"):
//   1. If the two spheres do not overlap (distance > sum of radii R), nothing happens.
//   2. The offset between the centres loses its component along gravity, so the push is always
//      horizontal on flat ground.
//   3. The push is min(R - length, maxPush) along that normalised offset.
//   4. The receiver adds it to its collider (vtable +0x18, 0x2c53cc), which resolves it against the
//      terrain like any other movement.
// The original also has a velocity cut (flag 1) and picks a random direction when both centres
// coincide; the opening never uses either.
//
// In the opening, the companion pushes the player when she walks her rail past him: the player's
// message handler 0x1574dc receives message 0x2db9c4 with maxPush 20 and flag 0.
// Timing detail (traced on push-exec-a): the message is sent when the two sensors overlap at the
// *previous* tick's positions; the handler then recomputes the push with the receiver's *current*
// sensor. Only calls 247 and 249 hit, whereas a test on current positions would also hit 243/251.
#pragma once

#include <cmath>
#include <stdexcept>

#include "game/VecLib.hpp"

namespace captain_toad::RecoveredPush {

using captain_toad::RecoveredVec::V3;

// True when two spheres (centre, radius) overlap or touch.
inline bool overlaps(const V3& a, float radiusA, const V3& b, float radiusB) {
    const V3 d = captain_toad::RecoveredVec::sub(a, b);
    float distanceSquared = d.x * d.x;
    distanceSquared = distanceSquared + d.y * d.y;
    distanceSquared = distanceSquared + d.z * d.z;
    return distanceSquared <= (radiusA + radiusB) * (radiusA + radiusB);
}

// Computes the push for the receiver's sensor `mine`. Returns false when the spheres do not
// overlap; otherwise writes the push vector to `out`.
inline bool push(const V3& mine, float radiusMine, const V3& other, float radiusOther, const V3& gravity, float maxPush,
                 V3& out) {
    using namespace captain_toad::RecoveredVec;
    const float R = radiusMine + radiusOther;

    V3 offset = sub(mine, other);
    float distanceSquared = offset.x * offset.x;
    distanceSquared = distanceSquared + offset.y * offset.y;
    distanceSquared = distanceSquared + offset.z * offset.z;
    if (distanceSquared > R * R) {
        return false;
    }

    // Remove the gravity component: offset -= gravity * dot(gravity, offset).
    float alongGravity = gravity.x * offset.x;
    alongGravity = alongGravity + gravity.y * offset.y;
    alongGravity = alongGravity + gravity.z * offset.z;
    offset = addScaled(gravity, offset, -alongGravity);

    float lengthSquared = offset.x * offset.x;
    lengthSquared = lengthSquared + offset.y * offset.y;
    lengthSquared = lengthSquared + offset.z * offset.z;
    const float length = std::sqrt(lengthSquared);
    if (length <= 0.0f) {
        throw std::runtime_error("push: coincident sensors (random direction path not ported)");
    }

    float amount = R - length;
    if (amount > maxPush) {
        amount = maxPush;
    }
    normalize(offset);
    out = scale(offset, amount);
    return true;
}

}   // namespace captain_toad::RecoveredPush
