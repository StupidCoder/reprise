// Turns local joint poses into world matrices and a model's skinning palette.
#pragma once

#include "data/Model.hpp"
#include "data/Skeleton.hpp"

#include <vector>

namespace beacon::anim {

// Model-space matrices of all joints of `skeleton` for the local poses `locals` (parents first,
// with segment scale compensation).
void jointWorldMatrices(const data::Skeleton& skeleton, const std::vector<data::JointPose>& locals,
                        std::vector<math::Mat4>& world);

class ModelPoser {
public:
    // Binds a model's palette to a skeleton by joint name hash. Entries whose joint the skeleton
    // lacks stay at the identity.
    ModelPoser(const data::Skeleton& skeleton, const data::Model& model);

    // World matrices of all joints for `locals` (model space).
    void worldMatrices(const std::vector<data::JointPose>& locals, std::vector<math::Mat4>& world) const;
    // Skinning matrices for every palette entry of the model.
    void palette(const std::vector<data::JointPose>& locals, std::vector<math::Mat4>& out) const;
    // The same from model-space joint matrices (e.g. after joints were overridden).
    void paletteFromWorld(const std::vector<math::Mat4>& world, std::vector<math::Mat4>& out) const;

private:
    const data::Skeleton& skeleton_;
    const data::Model& model_;
    std::vector<int> entryJoint_;   // palette entry -> skeleton joint (-1 = none)
};

}   // namespace anim
