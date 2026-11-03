// Grass tufts that bend away from the hero (class dynamic_grass; vtable 0x1691b48, constructor
// 0x3ed830). Findings: .analysis/docs/objects.md "Dynamic grass".
//
// A tuft's model has blade joints (named b_joint, b_joint1, ...); its idle clip sways them. Each
// blade keeps a bend: an axis, an angle and a weight. Every 10th update (staggered by the tuft's
// creation index) the heroes within GrassDistanceThreshold + GrassBotDistance of the tuft are
// listed (0x3ee610); while the list is not empty, every update (0x3f0370) each blade finds the
// nearest listed hero in the model's space. Closer than r = GrassBotDistance / the tuft's scale:
// the angle is MaxAngle - d (MaxAngle - MinAngle) / r about the horizontal axis that tips the
// blade away from the hero, and the weight rises by GrassForwardBlendSpeed per second up to 1;
// otherwise it falls by GrassBackwardBlendSpeed per second down to 0 (the axis and angle kept).
// The blade's model-space rotation is then the bend (angle x weight) applied before its animated
// rotation, around its own pivot.
#pragma once

#include "math/Transform.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace beacon::game {

// data/multi_platformer/property/dynamic_grass.xml (binder 0x3ed0d0).
struct DynamicGrassTuning {
    float distanceThreshold = 1.0f;     // GrassDistanceThreshold (+0x140)
    float botDistance = 0.19f;          // GrassBotDistance (+0x1f0)
    float forwardBlendSpeed = 5.5f;     // GrassForwardBlendSpeed (+0x2a0)
    float backwardBlendSpeed = 0.5f;    // GrassBackwardBlendSpeed (+0x350)
    float soundDistance = 0.1f;         // GrassSoundDistance (+0x400; not used yet)
};
DynamicGrassTuning loadDynamicGrassTuning(const std::filesystem::path& root);

class DynamicGrass {
public:
    // blades: skeleton joint indices of the blade joints; angles in degrees (the placement's
    // MinAngle / MaxAngle); index: creation order among the level's tufts (+0x305c).
    DynamicGrass(std::vector<int> blades, float minAngle, float maxAngle, int index);

    // One update. `world` is the tuft's placement matrix, `scale` its x scale; `heroes` the heroes'
    // positions (world); `bladePos` the blades' current model-space positions (same order as the
    // blades). Returns true when the blades are bent (their joints are to be overridden).
    bool update(float dt, const DynamicGrassTuning& t, const math::Mat4& world, float scale,
                std::span<const math::Vec3> heroes, std::span<const math::Vec3> bladePos);

    const std::vector<int>& blades() const { return blades_; }
    // Model-space rotation to put before blade i's animated rotation.
    math::Quat bend(std::size_t i) const;

private:
    struct Blade {
        math::Vec3 axis{1, 0, 0};
        float angle = 0;    // radians
        float weight = 0;   // +0xc0
    };
    std::vector<int> blades_;
    std::vector<Blade> state_;
    float minAngle_, maxAngle_;   // radians
    int index_;
    std::uint32_t counter_ = 0;   // +0x3060
    std::vector<int> listed_;     // heroes listed (+0x3070)
    bool listedAny_ = false;      // +0x3051
};

}   // namespace game
