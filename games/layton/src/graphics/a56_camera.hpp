#pragma once
// Camera motions of the 3DS adventures (folders ".xv": CMR.bin, an index,
// and one XCMA clip per %03d.cmr2). Event scenes and the puzzle verdicts
// fly the camera along these.
//
// XCMA (a clip): "XCMA", ..., the clip's name hash (0x20), its last frame
// (0x28), the rate (0x30: clip frames per game frame); channel descriptors
// from 0x60 (8 bytes each, {?, ?, ?, components, 0, 0, frames}) up to a zero
// one; then one block per channel {u32 0x10, u32 4, u32 offset of the
// values in the stream, u32 block size} followed by a method-word stream:
// {u16 ?, u16 key count}, key frames u16, values (components floats each).
// Channels in order: eye (3), target (3), vertical field of view in
// radians (1), roll (1; hypothesis: always 0 in the clips seen).
//
// Reconstructed from the fifth adventure's verdict cameras
// (evt/cmr/c103_judge.xv) and the services 4500 / 4600 / 4602.
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace picarat::adventure56 {

class Pack;

struct CameraClip {
    std::uint32_t hash = 0;
    int lastFrame = 0;
    float rate = 1;
    struct Channel {
        int components = 0;
        std::vector<std::uint16_t> frames;
        std::vector<std::array<float, 3>> values;

        std::array<float, 3> at(float frame) const; // linear between keys
    };
    Channel eye, target, fov, roll;
};

struct CameraSet {
    std::vector<CameraClip> clips;

    const CameraClip* clip(std::uint32_t hash) const;
};

CameraClip parseCameraClip(std::span<const std::uint8_t> data);
std::shared_ptr<CameraSet> loadCameraSet(const Pack& pack, const std::filesystem::path& folder);

} // namespace picarat::adventure56
