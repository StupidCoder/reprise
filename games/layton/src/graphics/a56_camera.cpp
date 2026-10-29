// Camera motions of the 3DS adventures. See a56_camera.hpp.
#include "graphics/a56_camera.hpp"
#include "graphics/a56_assets.hpp"
#include "import/asset_codecs.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace picarat::adventure56 {

namespace fs = std::filesystem;

namespace {

void check(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t at) {
    check(at + 4 <= d.size(), "camera data too short");
    return std::uint32_t(d[at]) | std::uint32_t(d[at + 1]) << 8 | std::uint32_t(d[at + 2]) << 16 |
           std::uint32_t(d[at + 3]) << 24;
}

std::uint16_t u16(std::span<const std::uint8_t> d, std::size_t at) {
    check(at + 2 <= d.size(), "camera data too short");
    return std::uint16_t(d[at] | d[at + 1] << 8);
}

float f32(std::span<const std::uint8_t> d, std::size_t at) {
    const std::uint32_t w = u32(d, at);
    float f;
    std::memcpy(&f, &w, sizeof f);
    return f;
}

} // namespace

std::array<float, 3> CameraClip::Channel::at(float frame) const {
    if (values.empty()) {
        return {0, 0, 0};
    }
    if (frame <= float(frames.front()) || values.size() == 1) {
        return values.front();
    }
    for (std::size_t k = 1; k < frames.size() && k < values.size(); ++k) {
        if (frame <= float(frames[k])) {
            const float f0 = frames[k - 1], f1 = frames[k];
            const float a = f1 > f0 ? (frame - f0) / (f1 - f0) : 0.0f;
            std::array<float, 3> r;
            for (std::size_t c = 0; c < 3; ++c) {
                r[c] = values[k - 1][c] + (values[k][c] - values[k - 1][c]) * a;
            }
            return r;
        }
    }
    return values.back();
}

CameraClip parseCameraClip(std::span<const std::uint8_t> d) {
    check(d.size() >= 0x60 && std::memcmp(d.data(), "XCMA", 4) == 0, "not an XCMA camera clip");
    CameraClip clip;
    clip.hash = u32(d, 0x20);
    clip.lastFrame = int(u32(d, 0x28));
    clip.rate = f32(d, 0x30);
    std::vector<int> components;
    std::size_t at = 0x60;
    while (at + 8 <= d.size() && u32(d, at) != 0) {
        components.push_back(d[at + 3]);
        at += 8;
    }
    at += 8; // the zero descriptor
    CameraClip::Channel* channels[4] = {&clip.eye, &clip.target, &clip.fov, &clip.roll};
    for (std::size_t i = 0; i < components.size() && at + 16 <= d.size(); ++i) {
        const std::size_t header = u32(d, at), valuesAt = u32(d, at + 8), size = u32(d, at + 12);
        check(size >= header && at + size <= d.size(), "XCMA channel outside the clip");
        const Bytes stream = import::decompressMethodWord(d.subspan(at + header, size - header));
        const std::span<const std::uint8_t> s(stream);
        if (i < 4) {
            CameraClip::Channel& c = *channels[i];
            c.components = std::min(3, components[i]);
            const unsigned keys = u16(s, 2) & 0x1fffu;
            for (unsigned k = 0; k < keys; ++k) {
                c.frames.push_back(u16(s, 4 + k * 2));
                std::array<float, 3> v{0, 0, 0};
                for (int n = 0; n < c.components; ++n) {
                    v[std::size_t(n)] = f32(s, valuesAt + (k * unsigned(components[i]) + unsigned(n)) * 4);
                }
                c.values.push_back(v);
            }
        }
        at += size;
    }
    return clip;
}

const CameraClip* CameraSet::clip(std::uint32_t hash) const {
    for (const auto& c : clips) {
        if (c.hash == hash) {
            return &c;
        }
    }
    return nullptr;
}

std::shared_ptr<CameraSet> loadCameraSet(const Pack& pack, const fs::path& folder) {
    auto set = std::make_shared<CameraSet>();
    for (unsigned i = 0;; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "%03u.cmr2", i);
        const fs::path file = folder / name;
        if (!fs::exists(file)) {
            break;
        }
        set->clips.push_back(parseCameraClip(pack.read(file)));
    }
    return set;
}

} // namespace picarat::adventure56
