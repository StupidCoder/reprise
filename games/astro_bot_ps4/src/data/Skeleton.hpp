// Skeletons (.skel): the animation library's skeleton format (tag "ES06", stored as "60SE").
//
// Header (little-endian): u32 tag, u32 total size, u32, u32; u16 jointCount, u16 userChannelCount,
// u16, u16 locomotion joint (hypothesis: 1 = "AR_Locomotion" in the player skeleton); then u32
// offsets that are relative to the field holding them:
//   +0x18 base pose: jointCount x { float4 rotation (x y z w), float4 translation, float4 scale }
//   +0x1c joint -> hierarchy-entry index (u16 per joint)
//   +0x20 joint name hashes (FNV-1a of the joint names, matching the model's node names)
//   +0x24 / +0x28 user channel name / node hashes (FNV-1a; e.g. "uvOffset0X" on node
//                 "hero_eyes_matSG": user channels drive material parameters)
//   +0x3c number of hierarchy entries; the entries start at 0x40
// Hierarchy entries: { u16 joint, u16 parent (0x7fff = none) | 0x8000 scale compensation }, in
// evaluation order
// (parents before children), processed four at a time; groups are padded by repeating the last
// entry. Verified on mup_hero.skel: the inverse table matches every entry and parents precede
// children; base-pose world matrices invert the model's bind matrices to within 1e-6.
#pragma once

#include "data/Bytes.hpp"
#include "math/Transform.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

struct JointPose {
    math::Quat rotation;
    math::Vec3 translation;
    math::Vec3 scale{1, 1, 1};
};

struct Skeleton {
    std::vector<std::int32_t> parent;      // -1 for roots
    std::vector<std::uint32_t> nameHash;   // FNV-1a joint names
    std::vector<JointPose> basePose;       // local transforms
    std::vector<std::uint16_t> order;      // evaluation order, parents first
    // Joint ignores its parent's scale for its own rotation and scale (Maya "segment scale
    // compensate"); its position still follows the parent. Evidence: mup_hero_anim_air_attack
    // stores each child's scale at its parent's value (2x on a whole hand chain); inheriting it
    // compounds to ~400x along four joints.
    std::vector<std::uint8_t> scaleCompensate;
    std::vector<std::uint32_t> userChannelName, userChannelNode;   // FNV-1a hashes
    std::uint32_t userChannelCount = 0;
    std::uint32_t locomotionJoint = 0;   // carries root motion (header field, see above)

    std::size_t jointCount() const { return parent.size(); }
};

std::optional<Skeleton> loadSkeleton(ByteView file, std::string* error = nullptr);

}   // namespace data
