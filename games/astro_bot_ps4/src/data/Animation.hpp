// Animation clips (.anim): the animation library's clip format (tag "EA08", stored as "80AE").
// Findings and checks: .analysis/docs/animation.md.
//
// Header (little-endian): u32 tag; f32 duration, f32 sample rate; u16 header size (frame data
// start), u16 joints, u16 frames, u16 frame sets, u16; u16 constant rotation/translation/scale/user
// channel counts; u16 animated rotation/translation/scale/user counts; u16 skeleton user channels.
// Self-relative u32 offsets: +0x38 frame-set table {u32, u32 file offset}, +0x3c frame-set info
// {u16 first frame, u16 frames after the first}, +0x40/+0x44/+0x48/+0x4c constant rotation /
// translation / scale / user values. At 0x60 the joint lists (u16): constant R, T, S, user, then
// animated R, T, S, user; padded to a multiple of 4 entries (constant R: 8).
//
// Values: rotations are 48-bit big-endian quaternions (three 15-bit components centred on 16384,
// scaled by 1/(16384*sqrt 2), then a 2-bit index of the omitted largest component, rebuilt
// positive); translations and scales float3; user channels float.
//
// Frame set: 32-byte header of section byte sizes {R0, T0, S0, U0, R, T, S, U} (sizes include
// alignment padding), the values of the set's first frame (R0..U0), a key bitmask (one bit per
// channel and later frame, channel-major, MSB first), then the keys channel by channel.
#pragma once

#include "data/Bytes.hpp"
#include "data/Skeleton.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

struct AnimationClip {
    enum class Source : std::uint8_t { Base, Constant, Animated };
    struct Track {
        Source source = Source::Base;
        std::uint32_t index = 0;   // into the constant or animated arrays
    };
    struct JointTracks {
        Track rotation, translation, scale;
    };

    float duration = 0, sampleRate = 30;
    std::uint32_t frameCount = 0;
    std::vector<JointTracks> joints;

    std::vector<math::Quat> constRotation;
    std::vector<math::Vec3> constTranslation, constScale;
    // Animated values, frame-major: value[frame * channels + channel].
    std::uint32_t animRotationCount = 0, animTranslationCount = 0, animScaleCount = 0;
    std::vector<math::Quat> rotations;
    std::vector<math::Vec3> translations, scales;
    // User channels (skeleton-defined float parameters, e.g. a material's uvOffset0X).
    std::vector<Track> userChannels;   // per skeleton user channel
    std::vector<float> constUser, userValues;
    std::uint32_t animUserCount = 0;

    // Value of a user channel at `time`; false if the clip does not drive it.
    bool sampleUser(std::size_t channel, float time, float& value) const;

    // Local joint poses at `time` seconds (clamped), starting from the skeleton's base pose.
    void sample(const Skeleton& skeleton, float time, std::vector<JointPose>& out) const;
};

std::optional<AnimationClip> loadAnimation(ByteView file, std::string* error = nullptr);

}   // namespace data
