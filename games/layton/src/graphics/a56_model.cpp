// 3D characters of the 3DS adventures. See a56_model.hpp.
#include "graphics/a56_model.hpp"
#include "graphics/a56_assets.hpp"
#include "import/asset_codecs.hpp"
#include "system/a56_xq.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
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
    check(at + 4 <= d.size(), "model data too short");
    return std::uint32_t(d[at]) | std::uint32_t(d[at + 1]) << 8 | std::uint32_t(d[at + 2]) << 16 |
           std::uint32_t(d[at + 3]) << 24;
}

float f32(std::span<const std::uint8_t> d, std::size_t at) {
    const std::uint32_t w = u32(d, at);
    float f;
    std::memcpy(&f, &w, sizeof f);
    return f;
}

std::string cstring(std::span<const std::uint8_t> d, std::size_t at, std::size_t length) {
    if (at >= d.size()) {
        return {};
    }
    const auto* p = reinterpret_cast<const char*>(d.data() + at);
    return std::string(p, strnlen(p, std::min(length, d.size() - at)));
}

std::string numbered(unsigned i, const char* extension) {
    char name[32];
    std::snprintf(name, sizeof name, "%03u.%s", i, extension);
    return name;
}

Affine affineAt(std::span<const std::uint8_t> d, std::size_t at) {
    // Stored by columns: the three axes, then the translation.
    Affine a;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 3; ++r) {
            a.m[std::size_t(r * 4 + c)] = f32(d, at + std::size_t(c * 3 + r) * 4);
        }
    }
    return a;
}

} // namespace

std::array<float, 3> Affine::apply(const float p[3]) const {
    return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
            m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]};
}

std::array<float, 3> Affine::rotate(const float v[3]) const {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[4] * v[0] + m[5] * v[1] + m[6] * v[2],
            m[8] * v[0] + m[9] * v[1] + m[10] * v[2]};
}

Affine Affine::operator*(const Affine& o) const {
    Affine r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = j == 3 ? m[std::size_t(i * 4 + 3)] : 0.0f;
            for (int k = 0; k < 3; ++k) {
                s += m[std::size_t(i * 4 + k)] * o.m[std::size_t(k * 4 + j)];
            }
            r.m[std::size_t(i * 4 + j)] = s;
        }
    }
    return r;
}

// XMPR: "XMPR", the XPRM's offset (4) and size (8), an offset at 0x0c (a
// block of hashes and the bounding box, not read), four (offset, size)
// pairs at 0x10..0x2c (the first holds a second XPVI in some character
// meshes - not read yet; the last is the bone palette), the mesh name's
// offset / length (0x30 / 0x34) and the material name's (0x38 / 0x3c). XPRM: "XPRM", its
// header size (0x14), the XPVB's size, the XPVI's offset from the XPRM and
// its size.
Mesh parseMesh(std::span<const std::uint8_t> data) {
    check(data.size() >= 0x40 && std::memcmp(data.data(), "XMPR", 4) == 0, "not an XMPR mesh");
    Mesh mesh;
    const std::size_t prm = u32(data, 4);
    check(prm + 0x14 <= data.size() && std::memcmp(data.data() + prm, "XPRM", 4) == 0,
          "XMPR without an XPRM");
    const std::size_t header = u32(data, prm + 4), vbSize = u32(data, prm + 8),
                      ibAt = prm + u32(data, prm + 12), ibSize = u32(data, prm + 16);
    check(prm + header + vbSize <= data.size() && ibAt + ibSize <= data.size(),
          "XPRM buffers outside the file");
    mesh.vertices = parseVertexBuffer(data.subspan(prm + header, vbSize));
    mesh.indices = parseIndexBuffer(data.subspan(ibAt, ibSize));
    const std::size_t paletteAt = u32(data, 0x28), paletteSize = u32(data, 0x2c);
    for (std::size_t i = 0; i + 4 <= paletteSize; i += 4) {
        mesh.palette.push_back(u32(data, paletteAt + i));
    }
    mesh.name = cstring(data, u32(data, 0x30), u32(data, 0x34));
    mesh.material = cstring(data, u32(data, 0x38), u32(data, 0x3c));
    // The mesh's block (offset at 0x0c, 15 words; 0x175fb8): technique and
    // pass mask (see Mesh).
    if (const std::size_t block = u32(data, 0x0c); block != 0 && block + 0x3c <= data.size()) {
        mesh.technique = u32(data, block + 2 * 4);
        mesh.passes = int(u32(data, block + 13 * 4) & 0xffff);
        mesh.layer = int(u32(data, block + 12 * 4) & 0xffff);
    }
    return mesh;
}

Bone parseBone(std::span<const std::uint8_t> data) {
    check(data.size() >= 168, "bone too short");
    Bone b;
    b.hash = u32(data, 0);
    b.parent = u32(data, 4);
    for (int i = 0; i < 3; ++i) {
        b.translation[i] = f32(data, std::size_t(3 + i) * 4);
        b.scale[i] = f32(data, std::size_t(15 + i) * 4);
    }
    for (int c = 0; c < 3; ++c) { // by columns, as the matrices
        for (int r = 0; r < 3; ++r) {
            b.rotation[r * 3 + c] = f32(data, std::size_t(6 + c * 3 + r) * 4);
        }
    }
    b.inverseBind = affineAt(data, 18 * 4);
    b.bind = affineAt(data, 30 * 4);
    return b;
}

Clip parseClip(std::span<const std::uint8_t> file) {
    check(file.size() >= 0x58 && std::memcmp(file.data(), "XMTN", 4) == 0, "not an XMTN clip");
    Clip clip;
    clip.hash = u32(file, 0x28);
    clip.lastFrame = int(u32(file, 0x54));
    const unsigned counts[3] = {u32(file, 0x14), u32(file, 0x18), u32(file, 0x1c)};
    const Bytes stream = import::decompressMethodWord(file.subspan(u32(file, 0x10)));
    const std::span<const std::uint8_t> d(stream);
    const std::size_t bonesAt = u32(d, 0), tableAt = u32(d, 8);
    const unsigned boneCount = std::max({counts[0], counts[1], counts[2]});
    for (unsigned i = 0; i < boneCount; ++i) {
        clip.bones.push_back(u32(d, bonesAt + i * 4));
    }
    std::vector<Clip::Track>* groups[3] = {&clip.translations, &clip.rotations, &clip.scales};
    std::size_t entry = tableAt;
    for (int g = 0; g < 3; ++g) {
        for (unsigned i = 0; i < counts[g]; ++i, entry += 16) {
            const std::size_t header = u32(d, entry), framesAt = u32(d, entry + 4),
                              valuesAt = u32(d, entry + 8);
            check(header + 4 <= d.size(), "XMTN track outside the stream");
            Clip::Track t;
            t.bone = d[header] | d[header + 1] << 8;
            // The count's top bits are flags (0x2000 on the characters'
            // tracks; meaning unknown).
            const unsigned keys = (d[header + 2] | d[header + 3] << 8) & 0x1fffu;
            for (unsigned k = 0; k < keys; ++k) {
                const std::size_t f = framesAt + k * 2;
                check(f + 2 <= d.size(), "XMTN key frames outside the stream");
                t.frames.push_back(std::uint16_t(d[f] | d[f + 1] << 8));
                std::array<float, 4> v{0, 0, 0, 1};
                if (g == 1) {
                    const std::size_t q = valuesAt + k * 8;
                    check(q + 8 <= d.size(), "XMTN rotation outside the stream");
                    for (int c = 0; c < 4; ++c) {
                        v[std::size_t(c)] =
                            float(std::int16_t(d[q + std::size_t(c) * 2] |
                                               d[q + std::size_t(c) * 2 + 1] << 8)) /
                            32767.0f;
                    }
                } else {
                    for (int c = 0; c < 3; ++c) {
                        v[std::size_t(c)] = f32(d, valuesAt + k * 12 + std::size_t(c) * 4);
                    }
                }
                t.values.push_back(v);
            }
            groups[g]->push_back(std::move(t));
        }
    }
    return clip;
}

// MINF: "MINF", the name hash (0x1c) and name (0x20, 32 bytes), the clip's
// hash (0x44), the motion that follows (0x48), the first and last frames
// (0x4c, 0x50), the rate (0x54).
Motion parseMotion(std::span<const std::uint8_t> d) {
    check(d.size() >= 0x58 && std::memcmp(d.data(), "MINF", 4) == 0, "not a MINF motion");
    Motion m;
    m.hash = u32(d, 0x1c);
    m.name = cstring(d, 0x20, 32);
    m.clip = u32(d, 0x44);
    m.next = u32(d, 0x48);
    m.first = int(u32(d, 0x4c));
    m.last = int(u32(d, 0x50));
    m.rate = f32(d, 0x54);
    return m;
}

namespace {

// A track's value at a frame: linear between keys, held outside them.
std::array<float, 4> sample(const Clip::Track& t, float frame, bool rotation) {
    if (t.frames.empty()) {
        return {0, 0, 0, 1};
    }
    if (frame <= float(t.frames.front()) || t.frames.size() == 1) {
        return t.values.front();
    }
    for (std::size_t k = 1; k < t.frames.size(); ++k) {
        if (frame <= float(t.frames[k])) {
            const float f0 = t.frames[k - 1], f1 = t.frames[k];
            const float a = f1 > f0 ? (frame - f0) / (f1 - f0) : 0.0f;
            auto v0 = t.values[k - 1], v1 = t.values[k];
            if (rotation) {
                // The shorter way round, then normalised (nlerp).
                const float dot = v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2] + v0[3] * v1[3];
                if (dot < 0) {
                    for (auto& c : v1) {
                        c = -c;
                    }
                }
            }
            std::array<float, 4> r;
            for (int c = 0; c < 4; ++c) {
                r[std::size_t(c)] =
                    v0[std::size_t(c)] + (v1[std::size_t(c)] - v0[std::size_t(c)]) * a;
            }
            if (rotation) {
                const float n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
                if (n > 0) {
                    for (auto& c : r) {
                        c /= n;
                    }
                }
            }
            return r;
        }
    }
    return t.values.back();
}

} // namespace

MaterialClip parseMaterialClip(std::span<const std::uint8_t> file) {
    check(file.size() >= 0x54 && std::memcmp(file.data(), "XMTM", 4) == 0,
          "not an XMTM material clip");
    MaterialClip clip;
    clip.hash = u32(file, 0x24);
    const unsigned count = u32(file, 0x1c);
    const Bytes stream = import::decompressMethodWord(file.subspan(u32(file, 0x18)));
    const std::span<const std::uint8_t> d(stream);
    const std::size_t targetsAt = u32(d, 0), tableAt = u32(d, 8);
    for (unsigned i = 0; i < count; ++i) {
        clip.targets.push_back(u32(d, targetsAt + i * 4));
    }
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t entry = tableAt + i * 16;
        const std::size_t header = u32(d, entry), framesAt = u32(d, entry + 4),
                          valuesAt = u32(d, entry + 8);
        check(header + 4 <= d.size(), "XMTM track outside the stream");
        MaterialClip::Track t;
        t.target = d[header] | d[header + 1] << 8;
        const unsigned keys = (d[header + 2] | d[header + 3] << 8) & 0x1fffu;
        for (unsigned k = 0; k < keys; ++k) {
            const std::size_t f = framesAt + k * 2;
            check(f + 2 <= d.size() && valuesAt + k * 4 + 4 <= d.size(),
                  "XMTM keys outside the stream");
            t.frames.push_back(std::uint16_t(d[f] | d[f + 1] << 8));
            t.values.push_back(f32(d, valuesAt + k * 4));
        }
        clip.tracks.push_back(std::move(t));
    }
    return clip;
}

bool Model::materialAlphaAt(const Motion& motion, float frame, std::uint32_t material,
                            float& alpha) const {
    for (const auto& clip : materialClips) {
        if (clip.hash != motion.clip) {
            continue;
        }
        for (const auto& t : clip.tracks) {
            if (t.target < 0 || t.target >= int(clip.targets.size()) ||
                clip.targets[std::size_t(t.target)] != material || t.frames.empty()) {
                continue;
            }
            if (frame <= float(t.frames.front()) || t.frames.size() == 1) {
                alpha = t.values.front();
                return true;
            }
            alpha = t.values.back();
            for (std::size_t k = 1; k < t.frames.size(); ++k) {
                if (frame <= float(t.frames[k])) {
                    const float f0 = t.frames[k - 1], f1 = t.frames[k];
                    const float a = f1 > f0 ? (frame - f0) / (f1 - f0) : 0.0f;
                    alpha = t.values[k - 1] + (t.values[k] - t.values[k - 1]) * a;
                    break;
                }
            }
            return true;
        }
    }
    return false;
}

FaceClip parseFaceClip(std::span<const std::uint8_t> file) {
    check(file.size() >= 0x54 && std::memcmp(file.data(), "XIMA", 4) == 0, "not an XIMA face clip");
    FaceClip clip;
    clip.hash = u32(file, 0x24);
    const unsigned count = u32(file, 0x14) + u32(file, 0x18) + u32(file, 0x1c) + u32(file, 0x20);
    const Bytes stream = import::decompressMethodWord(file.subspan(u32(file, 0x10)));
    const std::span<const std::uint8_t> d(stream);
    const std::size_t targetsAt = u32(d, 0), tableAt = u32(d, 8);
    for (unsigned i = 0; i < count; ++i) {
        clip.targets.push_back(u32(d, targetsAt + i * 4));
    }
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t entry = tableAt + i * 16;
        const std::size_t header = u32(d, entry), framesAt = u32(d, entry + 4),
                          valuesAt = u32(d, entry + 8);
        check(header + 4 <= d.size(), "XIMA track outside the stream");
        FaceClip::Track t;
        t.target = d[header] | d[header + 1] << 8;
        const unsigned keys = (d[header + 2] | d[header + 3] << 8) & 0x1fffu;
        for (unsigned k = 0; k < keys; ++k) {
            const std::size_t f = framesAt + k * 2;
            check(f + 2 <= d.size(), "XIMA key frames outside the stream");
            t.frames.push_back(std::uint16_t(d[f] | d[f + 1] << 8));
            t.values.push_back({f32(d, valuesAt + k * 8), f32(d, valuesAt + k * 8 + 4)});
        }
        clip.offsets.push_back(std::move(t));
    }
    return clip;
}

const Motion* Model::faceMotion(const std::string& name) const {
    for (const auto& m : faceMotions) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

std::array<float, 2> Model::textureOffset(std::uint32_t material, const Motion* face, float frame,
                                          bool* targeted) const {
    const TextureProjection* projection = nullptr;
    for (const auto& p : projections) {
        if (p.material == material) {
            projection = &p;
        }
    }
    if (!projection) {
        return {0, 0};
    }
    std::array<float, 2> offset{projection->offset[0], projection->offset[1]};
    if (!face) {
        return offset;
    }
    for (const auto& clip : faceClips) {
        if (clip.hash != face->clip) {
            continue;
        }
        for (const auto& t : clip.offsets) {
            if (t.target < 0 || t.target >= int(clip.targets.size()) ||
                clip.targets[std::size_t(t.target)] != projection->hash || t.frames.empty()) {
                continue;
            }
            // Steps: a key holds until the next (the face sheet's cells).
            std::size_t k = 0;
            while (k + 1 < t.frames.size() && frame >= float(t.frames[k + 1])) {
                ++k;
            }
            offset = t.values[k];
            if (targeted) {
                *targeted = true;
            }
        }
    }
    return offset;
}

const Motion* Model::materialMotion(const std::string& name) const {
    for (const auto& m : materialMotions) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

bool Model::attachMotions(std::shared_ptr<const Model> file, int slot) {
    if (slot > 7 || !file) {
        return false;
    }
    if (slot < 0) {
        std::uint32_t oldest = 0xffffffffu;
        for (int i = 0; i < 8; ++i) {
            const auto& s = motionSlots[std::size_t(i)];
            if (s) {
                if (s == file) {
                    return true;
                }
            } else if (slotStamps[std::size_t(i)] < oldest) {
                oldest = slotStamps[std::size_t(i)];
                slot = i;
            }
        }
        if (slot < 0) {
            return false;
        }
    }
    slotStamps[std::size_t(slot)] = ++slotClock;
    motionSlots[std::size_t(slot)] = std::move(file);
    return true;
}

const Motion* Model::motionByHash(std::uint32_t hash) const {
    if (hash == 0) {
        return nullptr;
    }
    for (const auto& m : motions) {
        if (m.hash == hash) {
            return &m;
        }
    }
    for (const auto& s : motionSlots) {
        if (s) {
            for (const auto& m : s->motions) {
                if (m.hash == hash) {
                    return &m;
                }
            }
        }
    }
    return nullptr;
}

const Motion* Model::motion(const std::string& name) const {
    for (const auto& m : motions) {
        if (m.name == name) {
            return &m;
        }
    }
    for (const auto& s : motionSlots) {
        if (s) {
            for (const auto& m : s->motions) {
                if (m.name == name) {
                    return &m;
                }
            }
        }
    }
    return nullptr;
}

std::vector<Affine> Model::restPose() const {
    return posed(nullptr, 0);
}

std::vector<Affine> Model::pose(const Motion& motion, float frame) const {
    const Clip* clip = clipFor(motion);
    if (!clip) {
        return {};
    }
    return posed(clip, frame);
}

const Clip* Model::clipFor(const Motion& motion) const {
    const Clip* clip = nullptr;
    for (const auto& c : clips) {
        if (c.hash == motion.clip) {
            clip = &c;
        }
    }
    for (const auto& s : motionSlots) {
        for (std::size_t i = 0; !clip && s && i < s->clips.size(); ++i) {
            if (s->clips[i].hash == motion.clip) {
                clip = &s->clips[i];
            }
        }
    }
    return clip;
}

std::vector<Affine> Model::posed(const Clip* clip, float frame) const {
    const std::pair<const Clip*, float> one{clip, frame};
    return posedLayers({&one, 1});
}

std::vector<Affine>
Model::posedLayers(std::span<const std::pair<const Clip*, float>> layers) const {
    // Each bone's local transform: the clip's tracks replace only the
    // channels they animate; translation, rotation and scale it leaves out
    // keep the bone's rest values from the .mbn file (puzzle 052's ladybug:
    // rotation and scale tracks for five bones, translation for one - the
    // shell halves keep their rest offsets; puzzle 069's pizza root, left
    // out entirely, keeps its rest scale 4).
    std::vector<std::array<float, 4>> t(bones.size(), {0, 0, 0, 1}), r(bones.size(), {0, 0, 0, 1}),
        sc(bones.size(), {1, 1, 1, 1});
    std::vector<unsigned> animated(bones.size(), 0); // 1 translation, 2 rotation, 4 scale
    for (const auto& [clip, frame] : layers) {
        if (!clip) {
            continue;
        }
        auto boneOf = [&](int clipBone) {
            return clipBone >= 0 && clipBone < int(clip->bones.size())
                       ? bone(clip->bones[std::size_t(clipBone)])
                       : -1;
        };
        for (const auto& tr : clip->translations) {
            if (const int b = boneOf(tr.bone); b >= 0) {
                t[std::size_t(b)] = sample(tr, frame, false);
                animated[std::size_t(b)] |= 1;
            }
        }
        for (const auto& tr : clip->rotations) {
            if (const int b = boneOf(tr.bone); b >= 0) {
                r[std::size_t(b)] = sample(tr, frame, true);
                animated[std::size_t(b)] |= 2;
            }
        }
        for (const auto& tr : clip->scales) {
            if (const int b = boneOf(tr.bone); b >= 0) {
                sc[std::size_t(b)] = sample(tr, frame, false);
                animated[std::size_t(b)] |= 4;
            }
        }
    }
    auto local = [&](std::size_t i) {
        const Bone& b = bones[i];
        const unsigned a = animated[i];
        float rot[9];
        if (a & 2) {
            const float x = r[i][0], y = r[i][1], z = r[i][2], w = r[i][3];
            const float q[9] = {
                1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
            std::copy(q, q + 9, rot);
        } else {
            for (int k = 0; k < 9; ++k) {
                rot[k] = b.rotation[std::size_t(k)];
            }
        }
        Affine m;
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t c = 0; c < 3; ++c) {
                m.m[row * 4 + c] = rot[row * 3 + c] * ((a & 4) ? sc[i][c] : b.scale[c]);
            }
            m.m[row * 4 + 3] = (a & 1) ? t[i][row] : b.translation[row];
        }
        return m;
    };
    std::vector<Affine> world(bones.size());
    std::vector<bool> done(bones.size(), false);
    // Parents come before children in the files; resolve in order, with a
    // few passes for safety.
    for (int pass = 0; pass < 4; ++pass) {
        for (std::size_t i = 0; i < bones.size(); ++i) {
            if (done[i]) {
                continue;
            }
            const Affine l = local(i);
            if (bones[i].parent == 0) {
                world[i] = l;
                done[i] = true;
            } else if (const int p = bone(bones[i].parent); p >= 0 && done[std::size_t(p)]) {
                world[i] = world[std::size_t(p)] * l;
                done[i] = true;
            } else if (p < 0) {
                world[i] = l;
                done[i] = true;
            }
        }
    }
    return world;
}

int Model::bone(std::uint32_t hash) const {
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].hash == hash) {
            return int(i);
        }
    }
    return -1;
}

float Model::Material::lookup(float x) const {
    return lookupLightTable(lightTable, x);
}

float lookupLightTable(std::span<const float> lightTable, float x) {
    if (lightTable.size() < 512) {
        return std::max(0.0f, x);
    }
    const float scaled = std::clamp(x, -1.0f, 1.0f) * 128.0f;
    const int i = std::clamp(int(std::floor(scaled)), -128, 127);
    const float frac = scaled - float(i);
    const std::size_t at = std::size_t(i & 0xff);
    return lightTable[at] + lightTable[256 + at] * frac;
}

std::vector<float> parseLightTable(std::span<const std::uint8_t> lutc) {
    if (lutc.size() < 0x14 || std::memcmp(lutc.data(), "LUTC", 4) != 0) {
        return {};
    }
    Bytes table;
    try {
        table = import::decompressMethodWord(lutc.subspan(0x10));
    } catch (const std::exception&) {
        return {};
    }
    if (table.size() < 2048) {
        return {};
    }
    std::vector<float> out(512);
    std::memcpy(out.data(), table.data(), 2048);
    return out;
}

const Model::Material* Model::material(const std::string& name) const {
    const std::uint32_t h = xq::hash(name);
    for (const auto& m : materials) {
        if (m.hash == h) {
            return &m;
        }
    }
    return materials.empty() ? nullptr : &materials.front();
}

std::shared_ptr<Model> loadModel(const Pack& pack, const fs::path& folder);

namespace {

std::shared_ptr<Model> loadModelFiles(const Pack& pack, const fs::path& folder);

} // namespace

std::shared_ptr<Model> loadModel(const Pack& pack, const fs::path& folder) {
    try {
        return loadModelFiles(pack, folder);
    } catch (const std::exception& e) {
        throw std::runtime_error(folder.string() + ": " + e.what());
    }
}

namespace {

// fix.xr's techniques (RES section 190, 16-byte entries: name hash, name,
// shader hash (section 110), combiner hash (section 120)): technique ->
// combiner. Read once per pack.
const std::map<std::uint32_t, std::uint32_t>& techniqueCombiners(const Pack& pack) {
    static std::mutex lock;
    static std::map<fs::path, std::map<std::uint32_t, std::uint32_t>> cache;
    const std::lock_guard guard(lock);
    auto [it, fresh] = cache.try_emplace(pack.root());
    if (fresh) {
        if (const fs::path fix = pack.find("fix", ".xr"); !fix.empty()) {
            try {
                ResContainer res(import::decompressMethodWord(pack.read(fix / "RES.bin")));
                if (const auto* s = res.section(190)) {
                    for (unsigned i = 0; i < s->count; ++i) {
                        it->second[res.word(*s, i, 0)] = res.word(*s, i, 3);
                    }
                }
            } catch (const std::exception&) {
            }
        }
    }
    return it->second;
}

std::shared_ptr<Model> loadModelFiles(const Pack& pack, const fs::path& folder) {
    ResContainer res(import::decompressMethodWord(pack.read(folder / "RES.bin")));
    check(res.magic() == "CHRC", "not a CHRC model");
    auto model = std::make_shared<Model>();
    std::vector<std::uint32_t> textureHashes;
    if (const auto* s = res.section(240)) {
        for (unsigned i = 0; i < s->count; ++i) {
            textureHashes.push_back(res.word(*s, i, 0));
            const fs::path file = folder / numbered(i, "xi");
            model->textures.push_back(fs::exists(file) ? loadTexture(pack, file) : nullptr);
            if (model->textures.back()) {
                applyTextureMode(*model->textures.back(), res.word(*s, i, 2));
            }
        }
    }
    if (const auto* s = res.section(290)) {
        for (unsigned i = 0; i < s->count; ++i) {
            Model::Material m;
            m.hash = res.word(*s, i, 0);
            const std::uint32_t t = res.word(*s, i, 4);
            for (std::size_t k = 0; k < textureHashes.size(); ++k) {
                if (textureHashes[k] == t) {
                    m.texture = int(k);
                }
            }
            model->materials.push_back(m);
        }
    }
    // The materials' blends (%03d.atr, one per material in list order).
    for (std::size_t i = 0; i < model->materials.size(); ++i) {
        const fs::path file = folder / numbered(unsigned(i), "atr");
        if (!fs::exists(file)) {
            continue;
        }
        const Bytes d = pack.read(file);
        if (d.size() < 16 || std::memcmp(d.data(), "ATRC", 4) != 0) {
            continue;
        }
        try {
            const Bytes block = import::decompressMethodWord(std::span(d).subspan(12));
            if (block.size() >= 7 * 4) {
                auto& m = model->materials[i];
                m.blendEquation = block[24];
                m.blendSource = block[25];
                m.blendDest = block[26];
            }
            if (block.size() >= 9 * 4 && block[12] == 1) {
                float amount;
                std::memcpy(&amount, block.data() + 32, 4);
                if (amount > 0) {
                    model->materials[i].depthOffset = amount * 1000.0f / 16777216.0f;
                }
            }
        } catch (const std::exception&) {
        }
    }
    // The materials' lighting tables, numbered by section 220 (names as
    // section 290's).
    if (const auto* s = res.section(220)) {
        for (unsigned i = 0; i < s->count; ++i) {
            const std::uint32_t hash = res.word(*s, i, 0);
            const fs::path file = folder / numbered(i, "mtr");
            if (!fs::exists(file)) {
                continue;
            }
            const Bytes d = pack.read(file);
            auto u32 = [&](std::size_t at) {
                return at + 4 <= d.size() ? std::uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                          d[at + 3] << 24)
                                          : 0u;
            };
            const std::size_t lut = u32(0x10);
            if (d.size() < 0x20 || std::memcmp(d.data(), "MTRC", 4) != 0) {
                continue;
            }
            // The parameter block (offset in word 2): the colours.
            try {
                const Bytes block = import::decompressMethodWord(std::span(d).subspan(u32(0x08)));
                if (block.size() >= 44 * 4) {
                    for (auto& m : model->materials) {
                        if (m.hash == hash) {
                            std::memcpy(m.emission, block.data() + 32 * 4, 12);
                            std::memcpy(m.ambient, block.data() + 35 * 4, 12);
                            std::memcpy(m.diffuse, block.data() + 38 * 4, 12);
                            std::memcpy(m.specular, block.data() + 41 * 4, 12);
                        }
                    }
                }
            } catch (const std::exception&) {
            }
            if (lut == 0 || lut >= d.size()) {
                continue;
            }
            const auto table = parseLightTable(std::span(d).subspan(lut));
            if (table.empty()) {
                continue;
            }
            for (auto& m : model->materials) {
                if (m.hash == hash) {
                    m.lightTable = table;
                }
            }
        }
    }
    if (const auto* s = res.section(100)) {
        for (unsigned i = 0; i < s->count; ++i) {
            const fs::path file = folder / numbered(i, "prm");
            if (fs::exists(file)) {
                try {
                    model->meshes.push_back(parseMesh(pack.read(file)));
                } catch (const std::exception& e) {
                    throw std::runtime_error(file.filename().string() + ": " + e.what());
                }
            }
        }
    }
    for (auto& mesh : model->meshes) {
        if (const auto& table = techniqueCombiners(pack); table.count(mesh.technique)) {
            mesh.combiner = table.at(mesh.technique);
        }
    }
    // Outlines: each silhouette file names its meshes, width and colour.
    for (unsigned i = 0;; ++i) {
        const fs::path file = folder / numbered(i, "sil");
        if (!fs::exists(file)) {
            break;
        }
        const Bytes d = pack.read(file);
        if (d.size() < 12 || std::memcmp(d.data(), "XCSL", 4) != 0) {
            continue;
        }
        auto u32 = [&](std::span<const std::uint8_t> b, std::size_t at) {
            return at + 4 <= b.size()
                       ? std::uint32_t(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24)
                       : 0u;
        };
        Bytes block;
        try {
            const std::size_t at = u32(d, 4), size = u32(d, 8);
            if (at + size <= d.size()) {
                block = import::decompressMethodWord(std::span(d).subspan(at, size));
            }
        } catch (const std::exception&) {
            continue;
        }
        if (block.size() < 32 * 4) {
            continue;
        }
        // Word 1 is the record's key (four values in the fifth adventure's
        // files); the loaders (0x1a2050, sixth 0x1f6584) skip a record whose
        // key is 0, so it draws no outline. Every sixth-adventure file has
        // key 0 (and the reference draws no silhouette in puzzle 050).
        if (u32(block, 4) == 0) {
            continue;
        }
        float width = 0;
        std::memcpy(&width, block.data() + 3 * 4, 4);
        // The colour: the first "CMBC" combiner's constant (RGBA bytes).
        float colour[3] = {0, 0, 0};
        for (std::size_t k = 0; k + 4 <= d.size(); ++k) {
            if (std::memcmp(d.data() + k, "CMBC", 4) == 0) {
                try {
                    const Bytes stage =
                        import::decompressMethodWord(std::span(d).subspan(k + 0x0c));
                    if (stage.size() >= 32) {
                        for (int c = 0; c < 3; ++c) {
                            colour[c] = float(stage[24 + std::size_t(c)]) / 255.0f;
                        }
                    }
                } catch (const std::exception&) {
                }
                break;
            }
        }
        const std::uint32_t meshes = u32(block, 2 * 4);
        for (std::uint32_t k = 0; k < meshes && (31 + k) * 4 + 4 <= block.size(); ++k) {
            const std::uint32_t h = u32(block, (31 + k) * 4);
            for (auto& mesh : model->meshes) {
                if (xq::hash(mesh.name) == h) {
                    mesh.outlineWidth = width;
                    mesh.outline = u32(block, 0);
                    std::memcpy(mesh.outlineColour, colour, sizeof colour);
                }
            }
        }
    }
    // Silhouette edges: vertices welded by position, each edge with the
    // triangles on its two sides.
    for (auto& mesh : model->meshes) {
        if (mesh.outlineWidth <= 0 || !mesh.vertices.has(VertexBuffer::Position)) {
            continue;
        }
        std::map<std::array<std::int32_t, 3>, std::uint16_t> weld;
        std::vector<std::uint16_t> id(mesh.vertices.count);
        for (unsigned v = 0; v < mesh.vertices.count; ++v) {
            const float* p = mesh.vertices.at(VertexBuffer::Position, v);
            const std::array<std::int32_t, 3> key{std::int32_t(std::lround(p[0] * 1024)),
                                                  std::int32_t(std::lround(p[1] * 1024)),
                                                  std::int32_t(std::lround(p[2] * 1024))};
            id[v] = weld.try_emplace(key, std::uint16_t(v)).first->second;
        }
        std::map<std::pair<std::uint16_t, std::uint16_t>, std::size_t> at;
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            for (int e = 0; e < 3; ++e) {
                const std::uint16_t a = mesh.indices[t + std::size_t(e)],
                                    b = mesh.indices[t + std::size_t((e + 1) % 3)];
                if (a >= id.size() || b >= id.size() || id[a] == id[b]) {
                    continue;
                }
                const auto key = std::minmax(id[a], id[b]);
                const auto it = at.find(key);
                if (it == at.end()) {
                    at[key] = mesh.edges.size();
                    mesh.edges.push_back({a, b, int(t / 3), -1});
                } else if (mesh.edges[it->second].right < 0) {
                    mesh.edges[it->second].right = int(t / 3);
                }
            }
        }
    }
    for (unsigned i = 0;; ++i) {
        const fs::path file = folder / numbered(i, "mtn2");
        if (!fs::exists(file)) {
            break;
        }
        try {
            model->clips.push_back(parseClip(pack.read(file)));
        } catch (const std::exception& e) {
            throw std::runtime_error(file.filename().string() + ": " + e.what());
        }
    }
    for (unsigned i = 0;; ++i) {
        const fs::path file = folder / numbered(i, "mtninf");
        if (!fs::exists(file)) {
            break;
        }
        model->motions.push_back(parseMotion(pack.read(file)));
    }
    if (const auto* s = res.section(140)) {
        for (unsigned i = 0; i < s->count; ++i) {
            const fs::path file = folder / numbered(i, "txp");
            if (!fs::exists(file)) {
                continue;
            }
            const Bytes d = pack.read(file);
            check(d.size() >= 36, "texture projection too short");
            TextureProjection p;
            p.hash = u32(d, 0);
            p.material = u32(d, 4);
            p.offset[0] = f32(d, 8);
            p.offset[1] = f32(d, 12);
            p.scale[0] = f32(d, 28);
            p.scale[1] = f32(d, 32);
            model->projections.push_back(p);
        }
    }
    for (unsigned i = 0;; ++i) {
        const fs::path file = folder / numbered(i, "imm2");
        if (!fs::exists(file)) {
            break;
        }
        model->faceClips.push_back(parseFaceClip(pack.read(file)));
    }
    for (unsigned i = 0;; ++i) {
        const fs::path file = folder / numbered(i, "imminf");
        if (!fs::exists(file)) {
            break;
        }
        model->faceMotions.push_back(parseMotion(pack.read(file)));
    }
    for (unsigned i = 0;; ++i) {
        const fs::path file = folder / numbered(i, "mtminf");
        if (!fs::exists(file)) {
            break;
        }
        model->materialMotions.push_back(parseMotion(pack.read(file)));
    }
    for (unsigned i = 0;; ++i) {
        const fs::path file = folder / numbered(i, "mtm2");
        if (!fs::exists(file)) {
            break;
        }
        model->materialClips.push_back(parseMaterialClip(pack.read(file)));
    }
    if (const auto* s = res.section(110)) {
        for (unsigned i = 0; i < s->count; ++i) {
            const fs::path file = folder / numbered(i, "mbn");
            if (fs::exists(file)) {
                model->bones.push_back(parseBone(pack.read(file)));
            }
        }
    }
    return model;
}

} // namespace

} // namespace picarat::adventure56
