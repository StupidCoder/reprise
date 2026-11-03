#include "data/Skeleton.hpp"

#include <cstring>

namespace beacon::data {

namespace {

constexpr std::uint16_t kNoParent = 0x7fff;

// Field at `at` holds an offset relative to itself.
std::size_t relative(ByteView d, std::size_t at) {
    std::uint32_t v = u32(d, at);
    return v ? at + v : 0;
}

}   // namespace

std::optional<Skeleton> loadSkeleton(ByteView d, std::string* error) {
    auto fail = [&](const char* message) -> std::optional<Skeleton> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    if (d.size() < 0x40 || std::memcmp(d.data(), "60SE", 4) != 0) {
        return fail("not a skeleton (ES06)");
    }
    const std::uint32_t joints = u16(d, 0x10);
    Skeleton s;
    s.userChannelCount = u16(d, 0x12);
    s.locomotionJoint = u16(d, 0x16) < joints ? u16(d, 0x16) : 0;
    const std::size_t basePose = relative(d, 0x18), names = relative(d, 0x20);
    const std::uint32_t entries = u32(d, 0x3c);
    if (!basePose || !names || basePose + joints * 48 > d.size() || names + joints * 4 > d.size() ||
        0x40 + entries * 4 > d.size()) {
        return fail("skeleton tables out of range");
    }
    s.parent.assign(joints, -2);
    s.scaleCompensate.assign(joints, 0);
    const std::size_t userNames = relative(d, 0x24), userNodes = relative(d, 0x28);
    for (std::uint32_t c = 0; c < s.userChannelCount; ++c) {
        s.userChannelName.push_back(userNames ? u32(d, userNames + c * 4) : 0);
        s.userChannelNode.push_back(userNodes ? u32(d, userNodes + c * 4) : 0);
    }
    s.nameHash.resize(joints);
    s.basePose.resize(joints);
    for (std::uint32_t j = 0; j < joints; ++j) {
        const std::size_t at = basePose + j * 48;
        JointPose& p = s.basePose[j];
        p.rotation = {read<float>(d, at), read<float>(d, at + 4), read<float>(d, at + 8), read<float>(d, at + 12)};
        p.translation = {read<float>(d, at + 16), read<float>(d, at + 20), read<float>(d, at + 24)};
        p.scale = {read<float>(d, at + 32), read<float>(d, at + 36), read<float>(d, at + 40)};
        s.nameHash[j] = u32(d, names + j * 4);
    }
    for (std::uint32_t e = 0; e < entries; ++e) {
        std::uint16_t joint = u16(d, 0x40 + e * 4), parent = u16(d, 0x40 + e * 4 + 2);
        if (joint >= joints || s.parent[joint] != -2) {
            continue;   // group padding repeats an entry
        }
        std::int32_t p = parent == kNoParent ? -1 : std::int32_t(parent & 0x7fff);
        if (p >= 0 && (p >= std::int32_t(joints) || s.parent[p] == -2)) {
            return fail("skeleton hierarchy is not parent-first");
        }
        s.parent[joint] = p;
        s.scaleCompensate[joint] = (parent & 0x8000) && parent != kNoParent ? 1 : 0;
        s.order.push_back(joint);
    }
    if (s.order.size() != joints) {
        return fail("skeleton hierarchy misses joints");
    }
    return s;
}

}   // namespace data
