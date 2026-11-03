// What a collision surface is, for both collision services (physics/CollisionWorld and
// physics/PhysicsWorld): who it stops and how it behaves. From the level data (placement
// attributes HitTvHero, HitVrHero, HitOther, IsSlope, IPhysics/Material).
#pragma once

#include <cstdint>

namespace beacon::physics {

// Who a surface stops (per placement: HitVrHero, HitTvHero, HitOther in the level data). The
// VR player's hero is the robot (art_bg 0x5ecda0: HitVrHero switches the bits of the hero
// manager's first hero, 1; HitTvHero the other heroes', 0xe; HitOther everything else).
enum CollisionMask : std::uint32_t {
    kHitHero = 1u << 0,    // the robot hero (HitVrHero)
    kHitVr = 1u << 1,      // the other heroes' bits (HitTvHero; no such hero in the port yet)
    kHitOther = 1u << 2,   // enemies, thrown objects, ...
    kHitAll = kHitHero | kHitVr | kHitOther,
};

// What a surface is: who it stops and how it behaves.
struct Surface {
    std::uint32_t owner = 0;      // caller's id (placement index)
    std::uint32_t mask = kHitAll;
    std::uint32_t material = 0;   // fnv1a of the material name ("Stone", "Metal", ...), 0 = none
    bool slope = false;           // slide surface (IsSlope)
};

}   // namespace physics
