// Glass-block walls (miniblock_volume, MupMiniBlock vtable 0x16a35d0: constructor 0x57b4d0, setup
// 0x57e460, builder 0x57ca00, update 0x580690, per-cube update 0x580960, hit 0x582840, break
// 0x582aa0). Findings: .analysis/docs/objects.md "Glass blocks".
//
// The placement's box is filled with cubes of the model mup_bg_city_miniblock: edge = the box's
// height / VerticalDivision, as many along each axis as fit (at least one). A hit (punch, kick,
// hover laser - any type while Attackable) breaks the cube nearest to it; a breaking cube is
// pushed off (MiniBlock Impluse, turned by random angles within the Horizontal/VerticalAngle
// ranges, a random spin of BreakRotation) as a physics body, and breaks its six neighbours
// 0.05 s / BreakSpeed later - the wall falls apart from the hit outwards - and is gone after
// DisappearEffectDelay.
#pragma once

#include "math/Transform.hpp"
#include "math/Vector.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <random>
#include <vector>

namespace beacon::game {

struct LevelObject;

// <MiniBlock> of multi_platformer.xml (singleton 0x583ff0).
struct MiniBlockTuning {
    float impulse = 120;                              // Impluse (+0x140)
    float horizontalMin = -30, horizontalMax = 30;    // degrees (+0x1f0 / +0x2a0)
    float verticalMin = -60, verticalMax = 0;         // degrees (+0x350 / +0x400)
    float restitution = 0.8f;                         // (+0x4b0)
    float breakRotation = 35;                         // (+0x560)
    float disappearDelay = 0.6f;                      // DisappearEffectDelay (+0x610)
};
MiniBlockTuning loadMiniBlockTuning(const std::filesystem::path& root);

class MiniBlock {
public:
    struct Cube {
        math::Vec3 local;                 // centre in the box's frame (box units: the unit cube scaled)
        std::array<int, 6> neighbours{};  // -1: none (+0x1a0..+0x1c8)
        int outsideBlock = -1, outsideCube = -1;   // a cube of a neighbouring wall (+0x1d0 / +0x1d8)
        int hits = 0;                     // +0x1ec: asked to break
        float delay = 0;                  // +0x134: until it goes
        float life = 0;                   // +0x138: DisappearEffectDelay after it went
        bool flying = false, gone = false;
        math::Vec3 direction;             // the hit's direction it breaks along (+0xd0)
    };
    // A launch for the scene to carry out: cube i's body becomes active, pushed by `force` for one
    // 1/60 s step with `torque` (0x580960 -> 0xca6510).
    struct Launch {
        int cube = 0;
        math::Vec3 force, torque;
    };

    MiniBlock(std::size_t placement, const LevelObject& o, const math::Mat4& world, const MiniBlockTuning& t);

    std::size_t placement() const { return placement_; }
    float edge() const { return edge_; }                       // a cube's edge in metres (+0x2640)
    const std::vector<Cube>& cubes() const { return cubes_; }
    math::Mat4 cubeWorld(int i) const;                         // where cube i stands, unbroken
    // 0x582840: the intact cube nearest to `point` breaks along `direction`; false when none.
    bool hit(const math::Vec3& point, const math::Vec3& direction);
    // One update; the launches of this update. Breaks that cross into another wall are queued in
    // `outside` (block, cube, delay, direction) for the scene to pass on.
    struct OutsideBreak {
        int block = 0, cube = 0;
        float delay = 0;
        math::Vec3 direction;
    };
    std::vector<Launch> update(float dt, std::vector<OutsideBreak>& outside);
    // A break from a neighbouring wall (0x582aa0 through the outside link).
    void breakFrom(int cube, float delay, const math::Vec3& direction) { breakCube(cube, delay, direction); }
    void link(int cube, int block, int other) {
        cubes_[std::size_t(cube)].outsideBlock = block;
        cubes_[std::size_t(cube)].outsideCube = other;
    }
    // A flying cube's life ended (the scene removes its body).
    bool gone(int i) const { return cubes_[std::size_t(i)].gone; }

private:
    void breakCube(int i, float delay, const math::Vec3& direction);   // 0x582aa0
    std::size_t placement_;
    math::Mat4 world_;
    MiniBlockTuning t_;
    float edge_ = 0.1f, breakSpeed_ = 1;
    bool attackable_ = true;
    std::array<int, 3> count_{1, 1, 1};
    std::vector<Cube> cubes_;
    std::mt19937 random_;
};

}   // namespace game
