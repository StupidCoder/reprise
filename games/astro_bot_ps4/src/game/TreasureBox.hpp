// Treasure boxes (class treasure_box, vtable 0x16d0de0, constructor 0x970d00 on the breakable
// object base 0x391ea0; setup 0x9716d0, update 0x974010, hit 0x394a10 -> 0x9726b0, break
// 0x395600). Findings: .analysis/docs/objects.md "Treasure boxes".
//
// A box shows the model of its Color (Green, Red, Yellow, Cyan -> mup_co_treasure_box_green /
// _pink / _yellow / _blue) playing its idle clip from a random time, and stands as a body the
// hero can stand on (self 0xd1041a1f). A hit breaks it when its type is allowed: 1-3 (punch, kick;
// TvHeroAttackable), 4 (the hover laser; HoverAttackable), 5 (bazooka). Breaking (0x395600) turns
// the box off, throws the pieces of the matching _break model (each an active body pushed away
// from the hit, gone after VanishStartTime plus VanishTime) and drops coins (5, a big box 10) that
// fly to the hero (0x3d5930 / 0x3d60f0) and count when they reach it.
#pragma once

#include "math/Transform.hpp"
#include "math/Vector.hpp"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace beacon::game {

struct TreasureBoxTuning {
    float unbreakableTime = 0.2f;       // <TreasureBox>
    float horizontalGroupDistance = 0.15f, verticalGroupDistance = 0.6f;   // stacking links
    int coinsSmall = 5, coinsBig = 10;  // <Coin> CoinNumTreasureBoxSmall / CoinNumTresureBoxBig
    // <Breakable><BreakableTreasure> (ParameterSet TreasureBox).
    float horizontalForce = 400, verticalForce = 0, rotationForce = 100, coneAngle = 30;
    float vanishStartMin = 1, vanishStartMax = 1, vanishTime = 0.38f, restitution = 1;
    // <CoinDropParameters>.
    float autoGetSpeed = 1.5f, autoGetImpulsePower = 2, autoGetFlyingDelay = 0, autoGetSpawnDelay = 0.08f;
    float autoGetVelocityBase = 1, autoGetVelocityNoise = 0, autoGetAngleBase = 45, autoGetAngleNoise = 15;
};
TreasureBoxTuning loadTreasureBoxTuning(const std::filesystem::path& root);

// A coin a broken box drops (a coin object with the mover 0x16904a0, at coin +0x26f0): it starts at
// the hit point; hidden for its spawn delay, then along a quadratic curve from its start through
// start + AutoGetImpulsePower x its launch velocity to the hero's centre, the parameter (mover
// +0x48) growing by AutoGetSpeed (+0x4c) per second; taken when it passes 1 (runs roof_box1/2: 40
// updates, coins 0, 4, 9, 14, 19 updates apart).
struct DroppedCoin {
    math::Vec3 start, launch, position;
    float delay = 0, t = 0;
    bool shown = false, taken = false;
    bool fresh = true;   // made by the break this update: its mover runs from the next one
};

class TreasureBox {
public:
    TreasureBox(std::size_t object, const math::Mat4& world, int color, bool big, const TreasureBoxTuning& t);

    std::size_t object() const { return object_; }
    const math::Mat4& world() const { return world_; }
    int color() const { return color_; }   // 0 green, 1 red (pink), 2 yellow, 3 cyan (blue)
    bool broken() const { return broken_; }
    float idleStart() const { return idleStart_; }

    // A hit (0x394a10): type 1-3 / 4 / 5; true when it breaks the box now. Ignored while the box
    // is unbreakable (+0x3908, after it started falling). `attacker`: where the hit came from (the
    // hero), for the coins' lean when the direction has no horizontal part.
    bool hit(int type, const math::Vec3& position, const math::Vec3& direction, const math::Vec3& attacker);

    // Stacks (0x9726b0): the boxes right above / below (-1 none).
    int upper = -1, lower = -1;
    // Falls by `drop` after `delay` (the box below it broke), unbreakable for UnbreakableTime.
    void startFall(float drop, float delay);
    // One update of the fall (phase 2, 0x973eb0); true while it moves.
    bool updateFall(float dt);
    float fallOffset() const { return fallOffset_; }   // y offset from the placement (+0x38f4)
    math::Mat4 pose() const;                            // the world transform with the offset
    // The break's hit point and direction (for the pieces' push).
    const math::Vec3& hitPosition() const { return hitPosition_; }
    const math::Vec3& hitDirection() const { return hitDirection_; }

    // The coins after the break; returns how many reached the hero this update.
    int updateCoins(float dt, const math::Vec3& heroCentre);
    const std::vector<DroppedCoin>& coins() const { return coins_; }

private:
    void dropCoins(const math::Vec3& attacker);
    std::size_t object_;
    math::Mat4 world_;
    int color_;
    bool big_;
    TreasureBoxTuning t_;
    bool broken_ = false;
    float idleStart_ = 0;
    math::Vec3 hitPosition_, hitDirection_;
    std::vector<DroppedCoin> coins_;
    // Fall (+0x38f0 target, +0x38f4 offset, +0x38f8 speed, +0x38fc delay, +0x3900 bounces).
    bool falling_ = false;
    float fallTarget_ = 0, fallOffset_ = 0, fallSpeed_ = 0, fallDelay_ = 0;
    int bounces_ = 0;
    float unbreakable_ = 0;   // +0x3908
    std::mt19937 random_;
};

}   // namespace game
