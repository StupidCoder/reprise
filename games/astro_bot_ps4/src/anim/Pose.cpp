#include "anim/Pose.hpp"

#include <unordered_map>

namespace beacon::anim {

ModelPoser::ModelPoser(const data::Skeleton& skeleton, const data::Model& model) : skeleton_(skeleton), model_(model) {
    std::unordered_map<std::uint32_t, int> byHash;
    for (std::size_t j = 0; j < skeleton.jointCount(); ++j) {
        byHash.emplace(skeleton.nameHash[j], int(j));
    }
    entryJoint_.resize(model.palette.size(), -1);
    for (std::size_t e = 0; e < model.palette.size(); ++e) {
        auto it = byHash.find(model.palette[e].jointHash);
        if (model.palette[e].jointHash != 0 && it != byHash.end()) {
            entryJoint_[e] = it->second;
        }
    }
}

void jointWorldMatrices(const data::Skeleton& skeleton, const std::vector<data::JointPose>& locals,
                        std::vector<math::Mat4>& world) {
    world.assign(skeleton.jointCount(), math::identity());
    for (std::uint16_t j : skeleton.order) {
        const data::JointPose& p = locals[j];
        const int parent = skeleton.parent[j];
        math::Mat4 local = math::compose(p.rotation, p.translation, p.scale);
        if (parent >= 0 && skeleton.scaleCompensate[j]) {
            // Segment scale compensation: T * inverse(parent scale) * R * S. The translation stays in
            // the parent's (scaled) space; rotation and scale do not inherit the parent's scale.
            const math::Vec3& ps = locals[std::size_t(parent)].scale;
            const float inv[3] = {ps.x != 0 ? 1 / ps.x : 1, ps.y != 0 ? 1 / ps.y : 1, ps.z != 0 ? 1 / ps.z : 1};
            for (int c = 0; c < 3; ++c) {       // columns of the rotation-scale part
                for (int r = 0; r < 3; ++r) {   // rows: scale row r by the inverse parent scale
                    local[c * 4 + r] *= inv[r];
                }
            }
        }
        world[j] = parent < 0 ? local : math::multiply(world[std::size_t(parent)], local);
    }
}

void ModelPoser::worldMatrices(const std::vector<data::JointPose>& locals, std::vector<math::Mat4>& world) const {
    jointWorldMatrices(skeleton_, locals, world);
}

void ModelPoser::palette(const std::vector<data::JointPose>& locals, std::vector<math::Mat4>& out) const {
    std::vector<math::Mat4> world;
    worldMatrices(locals, world);
    paletteFromWorld(world, out);
}

void ModelPoser::paletteFromWorld(const std::vector<math::Mat4>& world, std::vector<math::Mat4>& out) const {
    out.assign(model_.palette.size(), math::identity());
    for (std::size_t e = 0; e < out.size(); ++e) {
        int j = entryJoint_[e];
        if (j < 0) {
            continue;
        }
        const data::PaletteEntry& entry = model_.palette[e];
        // Skinned: World(joint) x inverse bind. Rigid: the mesh is in its node's space, World(node)
        // (the cloud floor's node sits at z -14.188 and the original's vertex stage gets exactly
        // that matrix, run roof_cloud_vs; the controller's parts are centred on their nodes too).
        out[e] = entry.hasBind ? math::multiply(world[std::size_t(j)], entry.bind) : world[std::size_t(j)];
    }
}

}   // namespace anim
