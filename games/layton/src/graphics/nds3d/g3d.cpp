// The 3D model library: loads the hamster minigame's models and animations
// and draws them through the 3D hardware model (gx.cpp), as the game's own
// model runtime does.
//
//   .nsbmd  models: a tree of nodes (joints), materials, textures and meshes
//           (display lists), plus a small structure program ("SBC") that
//           says in which order to set node matrices, materials and meshes;
//   .nsbca  joint animations: per-node translation, rotation and scale over
//           frames (stepped or interpolated);
//   .nsbma  material colour animations.
//
// Drawing a model runs its SBC program: for each node the animated
// transform is combined with its parent's, materials are set, and the mesh
// display lists are sent to the geometry engine.
#include "graphics/nds3d/g3d.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace picarat::nds3d {
namespace {
std::uint32_t rd32(const Bytes& d, std::size_t at) {
    if (at + 4 > d.size()) {
        throw std::runtime_error("g3d: read past end");
    }
    return std::uint32_t(d[at]) | std::uint32_t(d[at + 1]) << 8 | std::uint32_t(d[at + 2]) << 16 |
           std::uint32_t(d[at + 3]) << 24;
}

std::uint16_t rd16(const Bytes& d, std::size_t at) {
    if (at + 2 > d.size()) {
        throw std::runtime_error("g3d: read past end");
    }
    return std::uint16_t(d[at] | d[at + 1] << 8);
}

// NNS resource dictionary: u8 revision, u8 count, u16 size, u16 ?, u16 entry
// block offset; entry block: u16 unit size, u16 name offset, entries, 16-byte names.
struct Dict {
    std::vector<std::string> names;
    std::vector<std::size_t> entries;
};

Dict readDict(const Bytes& d, std::size_t at) {
    Dict r;
    const unsigned n = d.at(at + 1);
    const std::size_t block = at + rd16(d, at + 6);
    const unsigned unit = rd16(d, block);
    const std::size_t names = block + rd16(d, block + 2);
    for (unsigned i = 0; i < n; ++i) {
        r.entries.push_back(block + 4 + std::size_t(i) * unit);
        std::string name(reinterpret_cast<const char*>(&d.at(names + 16 * i)), 16);
        r.names.push_back(name.substr(0, name.find('\0')));
    }
    return r;
}

std::size_t findBlock(const Bytes& d, const char* tag) {
    const unsigned blocks = rd16(d, 0xe);
    for (unsigned i = 0; i < blocks; ++i) {
        const std::size_t at = rd32(d, 0x10 + 4 * i);
        if (std::memcmp(&d.at(at), tag, 4) == 0) {
            return at;
        }
    }
    return 0;
}

// Pivot-compressed rotation layout (0203e4ac == 0203e4d0): for a pivot index,
// the matrix slots of A, B, ±B and ±A.
constexpr std::uint8_t kPivot[9][4] = {{4, 5, 7, 8}, {3, 5, 6, 8}, {3, 4, 6, 7},
                                       {1, 2, 7, 8}, {0, 2, 6, 8}, {0, 1, 6, 7},
                                       {1, 2, 4, 5}, {0, 2, 3, 5}, {0, 1, 3, 4}};
// Material colour masks (0203e48c), indexed by the material flag bits.
constexpr std::uint32_t kMatMask[8] = {0,      0x7fff, 0x7fff0000, 0x7fff7fff,
                                       0x8000, 0xffff, 0x7fff8000, 0x7fffffff};

std::uint32_t mtxWord(fx32 v) {
    return std::uint32_t(v);
}
} // namespace

// ---------------------------------------------------------------------------
// Resources

ModelSet::ModelSet(Bytes file) : data_(std::move(file)) {
    if (data_.size() < 0x14 || std::memcmp(data_.data(), "BMD0", 4) != 0) {
        throw std::runtime_error("not a BMD0 file");
    }
    const std::size_t mdl = findBlock(data_, "MDL0");
    const auto models = readDict(data_, mdl + 8);
    if (models.entries.empty()) {
        throw std::runtime_error("BMD0 without models");
    }
    const std::size_t m = mdl + rd32(data_, models.entries[0]);
    model_ = m;
    sbc = m + rd32(data_, m + 4);
    matBase_ = m + rd32(data_, m + 8);
    const std::size_t shp = m + rd32(data_, m + 12);
    evp = m + rd32(data_, m + 16);
    scalingRule = data_.at(m + 0x15);
    texMtxMode = data_.at(m + 0x16);
    nodeCount = data_.at(m + 0x17);
    posScale = fx32(rd32(data_, m + 0x1c));
    invPosScale = fx32(rd32(data_, m + 0x20));
    nodeInfo = m + 0x40;
    const auto nodeDict = readDict(data_, nodeInfo);
    for (std::size_t i = 0; i < nodeDict.entries.size(); ++i) {
        nodes.push_back(nodeInfo + rd32(data_, nodeDict.entries[i]));
        nodeNames.push_back(nodeDict.names[i]);
    }
    const auto matDict = readDict(data_, matBase_ + 4);
    for (std::size_t i = 0; i < matDict.entries.size(); ++i) {
        const std::size_t at = matBase_ + rd32(data_, matDict.entries[i]);
        Material mat;
        mat.difAmb = rd32(data_, at + 4);
        mat.speEmi = rd32(data_, at + 8);
        mat.polyAttr = rd32(data_, at + 0xc);
        mat.polyAttrMask = rd32(data_, at + 0x10);
        mat.texImageParam = rd32(data_, at + 0x14);
        mat.texImageParamMask = rd32(data_, at + 0x18);
        mat.texPlttBase = rd16(data_, at + 0x1c);
        mat.flag = rd16(data_, at + 0x1e);
        mat.originalWidth = rd16(data_, at + 0x20);
        mat.originalHeight = rd16(data_, at + 0x22);
        mat.magW = fx32(rd32(data_, at + 0x24));
        mat.magH = fx32(rd32(data_, at + 0x28));
        materials.push_back(mat);
        materialNames.push_back(matDict.names[i]);
    }
    const auto shpDict = readDict(data_, shp);
    for (auto e : shpDict.entries) {
        shapes.push_back(shp + rd32(data_, e));
    }
    tex0_ = findBlock(data_, "TEX0");
}

std::array<fx32, 6> ModelSet::box() const {
    const fx32 scale = fx32(rd32(data_, model_ + 0x38));
    std::array<fx32, 6> r{};
    for (int k = 0; k < 6; ++k) {
        r[std::size_t(k)] = fx32(
            (std::int64_t(std::int16_t(rd16(data_, model_ + 0x2c + 2 * std::size_t(k)))) * scale +
             0x800) >>
            12);
    }
    return r;
}

std::uint32_t ModelSet::u32(std::size_t at) const {
    return rd32(data_, at);
}

std::uint16_t ModelSet::u16(std::size_t at) const {
    return rd16(data_, at);
}

// Uploads the model's textures and palettes to video memory.
void ModelSet::setup(Gpu3D& gpu, VramAllocator& vram) {
    if (!tex0_) {
        return;
    }
    const std::size_t t = tex0_;
    // Texture image data (normal textures) and 4x4 data with its palette indices.
    const std::uint32_t texSize = std::uint32_t(rd16(data_, t + 0xc)) << 3,
                        tex4x4Size = std::uint32_t(rd16(data_, t + 0x1c)) << 3,
                        plttSize = std::uint32_t(rd16(data_, t + 0x30)) << 3;
    std::uint32_t texKey = 0, tex4x4Key = 0, plttKey = 0;
    if (texSize) {
        const std::uint32_t at = vram.texture + 0x40000;
        vram.texture += texSize; // slots 2/3 for plain textures
        std::copy_n(data_.begin() + std::ptrdiff_t(t + rd32(data_, t + 0x14)), texSize,
                    gpu.textureVram.begin() + at);
        texKey = at >> 3;
    }
    if (tex4x4Size) {
        const std::uint32_t at = vram.texture4x4;
        vram.texture4x4 += tex4x4Size; // slot 0, indices in slot 1
        std::copy_n(data_.begin() + std::ptrdiff_t(t + rd32(data_, t + 0x24)), tex4x4Size,
                    gpu.textureVram.begin() + at);
        const std::uint32_t indexAt = ((0x1ffff & at) >> 1) + 0x20000 + ((at & 0x40000) >> 2);
        std::copy_n(data_.begin() + std::ptrdiff_t(t + rd32(data_, t + 0x28)), tex4x4Size / 2,
                    gpu.textureVram.begin() + indexAt);
        tex4x4Key = at >> 3;
    }
    if (plttSize) {
        const std::uint32_t at = vram.palette;
        vram.palette += plttSize;
        std::copy_n(data_.begin() + std::ptrdiff_t(t + rd32(data_, t + 0x38)), plttSize,
                    gpu.paletteVram.begin() + at);
        plttKey = at >> 3;
    }
    const auto texDict = readDict(data_, t + rd16(data_, t + 0xe));
    const auto plttDict = readDict(data_, t + rd16(data_, t + 0x34));
    // Material bindings: texture/palette name -> list of material indices.
    const auto texToMat = readDict(data_, matBase_ + rd16(data_, matBase_));
    for (std::size_t i = 0; i < texToMat.entries.size(); ++i) {
        const auto& name = texToMat.names[i];
        const auto it = std::find(texDict.names.begin(), texDict.names.end(), name);
        if (it == texDict.names.end()) {
            continue;
        }
        const std::size_t e = texDict.entries[std::size_t(it - texDict.names.begin())];
        const std::uint32_t param = rd32(data_, e), extra = rd32(data_, e + 4);
        const std::uint32_t key = ((param & 0x1c000000) == 0x14000000) ? tex4x4Key : texKey;
        const std::size_t list = matBase_ + rd16(data_, texToMat.entries[i]);
        const unsigned count = data_.at(texToMat.entries[i] + 2);
        for (unsigned k = 0; k < count; ++k) {
            auto& mat = materials.at(data_.at(list + k));
            mat.texImageParam |= param + (key & 0xffff);
            const unsigned w = extra & 0x7ff, h = (extra >> 11) & 0x7ff;
            mat.magW = w == mat.originalWidth ? 0x1000
                                              : fxDiv(fx32(w << 12), fx32(mat.originalWidth) << 12);
            mat.magH = h == mat.originalHeight
                           ? 0x1000
                           : fxDiv(fx32(h << 12), fx32(mat.originalHeight) << 12);
        }
    }
    const auto plttToMat = readDict(data_, matBase_ + rd16(data_, matBase_ + 2));
    for (std::size_t i = 0; i < plttToMat.entries.size(); ++i) {
        const auto& name = plttToMat.names[i];
        const auto it = std::find(plttDict.names.begin(), plttDict.names.end(), name);
        if (it == plttDict.names.end()) {
            continue;
        }
        const std::size_t e = plttDict.entries[std::size_t(it - plttDict.names.begin())];
        std::uint32_t offset = rd16(data_, e), key = plttKey & 0xffff;
        if (!(rd16(data_, e + 2) & 1)) {
            offset >>= 1;
            key >>= 1;
        }
        const std::size_t list = matBase_ + rd16(data_, plttToMat.entries[i]);
        const unsigned count = data_.at(plttToMat.entries[i] + 2);
        for (unsigned k = 0; k < count; ++k) {
            materials.at(data_.at(list + k)).texPlttBase = std::uint16_t(offset + key);
        }
    }
}

JointAnimation::JointAnimation(Bytes file) : data_(std::move(file)) {
    if (data_.size() < 0x14 || std::memcmp(data_.data(), "BCA0", 4) != 0) {
        throw std::runtime_error("not a BCA0 file");
    }
    const std::size_t jnt = findBlock(data_, "JNT0");
    const auto d = readDict(data_, jnt + 8);
    if (d.entries.empty()) {
        throw std::runtime_error("BCA0 without animations");
    }
    anim_ = jnt + rd32(data_, d.entries[0]);
    frames_ = rd16(data_, anim_ + 4);
    nodes_ = rd16(data_, anim_ + 6);
}

MaterialAnimation::MaterialAnimation(Bytes file) : data_(std::move(file)) {
    if (data_.size() < 0x14 || std::memcmp(data_.data(), "BMA0", 4) != 0) {
        throw std::runtime_error("not a BMA0 file");
    }
    const std::size_t mat = findBlock(data_, "MAT0");
    const auto d = readDict(data_, mat + 8);
    if (d.entries.empty()) {
        throw std::runtime_error("BMA0 without animations");
    }
    anim_ = mat + rd32(data_, d.entries[0]);
    frames_ = rd16(data_, anim_ + 4);
    const auto m = readDict(data_, anim_ + 8);
    names_ = m.names;
    entries_ = m.entries;
}

namespace {
// 0201483c (RGB555 colours, averaged per component) and 020149a4 (u8 alpha);
// tag: bit 29 constant (low 16 bits), else data offset with 1/2/4-frame steps.
std::uint32_t sampleMaterial(const Bytes& d, std::size_t anim, std::uint32_t tag, unsigned f,
                             bool color) {
    if (tag & 0x20000000) {
        return tag & 0xffff;
    }
    const std::size_t data = anim + (tag & 0xffff);
    auto at = [&](unsigned i) -> std::uint32_t {
        return color ? rd16(d, data + i * 2) : d.at(data + i);
    };
    auto mix = [&](std::uint32_t a, std::uint32_t b, int wa, int wb, int shift) -> std::uint32_t {
        if (!color) {
            return (a * std::uint32_t(wa) + b * std::uint32_t(wb)) >> shift;
        }
        return (0x7c1f &
                (((a & 0x7c1f) * std::uint32_t(wa) + (b & 0x7c1f) * std::uint32_t(wb)) >> shift)) |
               ((((a & 0x3e0) * std::uint32_t(wa) + (b & 0x3e0) * std::uint32_t(wb)) >> shift) &
                0x3e0);
    };
    if (!(tag & 0xc0000000)) {
        return at(f);
    }
    const unsigned last = (tag & 0x1fff0000) >> 16;
    if (!(tag & 0x40000000)) {
        if ((f & 3) == 0) {
            return at(f >> 2);
        }
        if (f > last) {
            return at((f & 3) + (last >> 2));
        }
        if (f & 1) {
            unsigned three, one;
            if (f & 2) {
                one = f >> 2;
                three = one + 1;
            } else {
                three = f >> 2;
                one = three + 1;
            }
            return mix(at(three), at(one), 3, 1, 2);
        }
        return mix(at(f >> 2), at((f >> 2) + 1), 1, 1, 1);
    }
    if ((f & 1) == 0) {
        return at(f >> 1);
    }
    if (f > last) {
        return at((last >> 1) + 1);
    }
    return mix(at(f >> 1), at((f >> 1) + 1), 1, 1, 1);
}
} // namespace

bool MaterialAnimation::apply(const std::string& material, unsigned f, std::uint32_t& difAmb,
                              std::uint32_t& speEmi, std::uint32_t& polyAttr) const {
    const auto it = std::find(names_.begin(), names_.end(), material);
    if (it == names_.end()) {
        return false;
    }
    const std::size_t e = entries_[std::size_t(it - names_.begin())];
    auto tag = [&](int k) {
        return rd32(data_, e + 4 * std::size_t(k));
    };
    difAmb = sampleMaterial(data_, anim_, tag(0), f, true) |
             (sampleMaterial(data_, anim_, tag(1), f, true) << 16) | (difAmb & 0x8000);
    speEmi = sampleMaterial(data_, anim_, tag(2), f, true) |
             (sampleMaterial(data_, anim_, tag(3), f, true) << 16) | (speEmi & 0x8000);
    polyAttr = (polyAttr & 0xffe0ffff) | (sampleMaterial(data_, anim_, tag(4), f, false) << 16);
    return true;
}

// ---------------------------------------------------------------------------
// Global state (0200f6d0 layout, 0200f824 flush)

// Sends the camera, projection and lights to the geometry engine.
void GlobalState::flush(Gpu3D& gpu) const {
    std::vector<std::uint32_t> w;
    w.push_back(0x17101610);
    w.push_back(0);
    for (int i = 0; i < 16; ++i) {
        w.push_back(mtxWord(projection.m[std::size_t(i)]));
    }
    w.push_back(2);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) {
            w.push_back(mtxWord(camera.m[std::size_t(r * 4 + c)]));
        }
    }
    w.push_back(0x32323232);
    for (auto v : lightVectors) {
        w.push_back(v);
    }
    w.push_back(0x60293130);
    w.push_back(difAmb);
    w.push_back(speEmi);
    w.push_back(polyAttr);
    w.push_back(viewport);
    w.push_back(0x33333333);
    for (auto v : lightColors) {
        w.push_back(v);
    }
    w.push_back(0x002a1b19);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            w.push_back(mtxWord(baseRotation.m[std::size_t(r * 4 + c)]));
        }
    }
    w.push_back(mtxWord(baseTranslation.x));
    w.push_back(mtxWord(baseTranslation.y));
    w.push_back(mtxWord(baseTranslation.z));
    w.push_back(mtxWord(baseScale.x));
    w.push_back(mtxWord(baseScale.y));
    w.push_back(mtxWord(baseScale.z));
    w.push_back(texImageParam);
    gpu.packed(w);
}

// ---------------------------------------------------------------------------
// Joint animation sampling (0201357c and its samplers).

namespace {
struct AnimView {
    const Bytes& d;
    std::size_t base;

    std::int32_t word(std::size_t at) const { return std::int32_t(rd32(d, at)); }

    std::int32_t half(std::size_t at) const { return std::int16_t(rd16(d, at)); }

    unsigned frames() const { return rd16(d, base + 4); }

    std::uint32_t flag() const { return rd32(d, base + 8); }
};

// 02013930 (stepped) / 02013a90 (interpolated) for one translation value.
fx32 sampleTranslation(const AnimView& a, std::uint32_t info, std::uint32_t offset, fx32 frame,
                       bool interpolate) {
    const std::size_t data = a.base + offset;
    const bool fx16 = info & 0x20000000;
    auto at = [&](unsigned i) -> std::int32_t {
        return fx16 ? a.half(data + i * 2) : a.word(data + i * 4);
    };
    unsigned f = unsigned(frame >> 12);
    const std::uint32_t step = info & 0xc0000000;
    if (!interpolate) {
        unsigned index = f;
        if (step) {
            const std::uint32_t last = info & 0x1fff0000;
            if (!(info & 0x40000000)) { // every 4th frame
                if ((f & 3) == 0) {
                    index = f >> 2;
                } else if (f <= last >> 16) {
                    if (f & 1) {
                        unsigned three, one;
                        if (!(f & 2)) {
                            three = f >> 2;
                            one = three + 1;
                        } else {
                            one = f >> 2;
                            three = one + 1;
                        }
                        if (!fx16) {
                            return fx32((std::int64_t(at(three)) * 3 + at(one)) >> 2);
                        }
                        return fx32((at(three) * 3 + at(one)) >> 2);
                    }
                    index = f >> 2;
                    return fx16 ? fx32((at(index) + at(index + 1)) >> 1)
                                : fx32((at(index + 1) >> 1) + (at(index) >> 1));
                } else {
                    index = (f & 3) + (last >> 18);
                }
            } else if ((f & 1) == 0) {
                index = f >> 1;
            } else if (f <= last >> 16) {
                index = f >> 1;
                return fx16 ? fx32((at(index) + at(index + 1)) >> 1)
                            : fx32((at(index + 1) >> 1) + (at(index) >> 1));
            } else {
                index = (last >> 17) + 1;
            }
        }
        return at(index);
    }
    std::uint32_t fraction = std::uint32_t(frame);
    if (a.frames() - 1 == f) {
        if (step) {
            f = (info & 0x40000000) ? (f & 1) + (f >> 1) : (f & 3) + (f >> 2);
        }
        if (!(a.flag() & 2)) {
            return at(f);
        }
        const std::int32_t v0 = at(f), v1 = at(0);
        return v0 + (std::int32_t((fraction & 0xfff) * std::uint32_t(v1 - v0)) >> 12);
    }
    unsigned index = f;
    int mul = 1, shift = 0;
    fraction &= 0xfff;
    if (step) {
        const unsigned last = (info & 0x1fff0000) >> 16;
        if (!(info & 0x40000000)) {
            if (f < last) {
                index = f >> 2;
                fraction = std::uint32_t(frame) & 0x3fff;
                mul = 4;
                shift = 2;
            } else {
                index = (f & 3) + (f >> 2);
            }
        } else {
            if (f < last) {
                index = f >> 1;
                fraction = std::uint32_t(frame) & 0x1fff;
                mul = 2;
                shift = 1;
            } else {
                index = (info & 0x1fff0000) >> 17;
            }
        }
    }
    const std::int32_t v0 = at(index), v1 = at(index + 1);
    return (v0 * mul + (std::int32_t(fraction * std::uint32_t(v1 - v0)) >> 12)) >> shift;
}

// 02013bec / 02013de0: (scale, inverse scale) pair.
std::array<fx32, 2> sampleScale(const AnimView& a, std::uint32_t info, std::uint32_t offset,
                                fx32 frame, bool interpolate) {
    const std::size_t data = a.base + offset;
    const bool fx16 = info & 0x20000000;
    auto at = [&](unsigned i, int k) -> std::int32_t {
        return fx16 ? a.half(data + i * 4 + std::size_t(k) * 2)
                    : a.word(data + i * 8 + std::size_t(k) * 4);
    };
    unsigned f = unsigned(frame >> 12);
    const std::uint32_t step = info & 0xc0000000;
    if (!interpolate) {
        unsigned index = f;
        if (step) {
            const std::uint32_t last = info & 0x1fff0000;
            if (!(info & 0x40000000)) {
                if ((f & 3) == 0) {
                    index = f >> 2;
                } else if (f <= last >> 16) {
                    if (f & 1) {
                        unsigned three, one;
                        if (!(f & 2)) {
                            three = f >> 2;
                            one = three + 1;
                        } else {
                            one = f >> 2;
                            three = one + 1;
                        }
                        std::array<fx32, 2> r{};
                        for (int k = 0; k < 2; ++k) {
                            r[std::size_t(k)] =
                                fx16 ? fx32((at(one, k) + at(three, k) * 3) >> 2)
                                     : fx32((std::int64_t(at(three, k)) * 3 + at(one, k)) >> 2);
                        }
                        return r;
                    }
                    index = f >> 2;
                    return {fx32((at(index, 0) + at(index + 1, 0)) >> 1),
                            fx32((at(index, 1) + at(index + 1, 1)) >> 1)};
                } else {
                    index = (f & 3) + (last >> 18);
                }
            } else if ((f & 1) == 0) {
                index = f >> 1;
            } else if (f <= last >> 16) {
                index = f >> 1;
                return {fx32((at(index, 0) + at(index + 1, 0)) >> 1),
                        fx32((at(index, 1) + at(index + 1, 1)) >> 1)};
            } else {
                index = (last >> 17) + 1;
            }
        }
        return {at(index, 0), at(index, 1)};
    }
    std::uint32_t fraction = std::uint32_t(frame);
    unsigned index = f, next = f + 1;
    int mul = 1, shift = 0;
    if (a.frames() - 1 == f) {
        if (step) {
            f = (info & 0x40000000) ? (f & 1) + (f >> 1) : (f & 3) + (f >> 2);
        }
        if (!(a.flag() & 2)) {
            return {at(f, 0), at(f, 1)};
        }
        index = f;
        next = 0;
        fraction &= 0xfff;
    } else if (!step) {
        fraction &= 0xfff;
    } else {
        const unsigned last = (info & 0x1fff0000) >> 16;
        if (!(info & 0x40000000)) {
            if (f < last) {
                index = f >> 2;
                next = index + 1;
                fraction = std::uint32_t(frame) & 0x3fff;
                mul = 4;
                shift = 2;
            } else {
                index = (f & 3) + (f >> 2);
                next = index + 1;
                fraction &= 0xfff;
            }
        } else {
            if (f < last) {
                index = f >> 1;
                next = index + 1;
                fraction = std::uint32_t(frame) & 0x1fff;
                mul = 2;
                shift = 1;
            } else {
                index = (info & 0x1fff0000) >> 17;
                next = index + 1;
                fraction &= 0xfff;
            }
        }
    }
    std::array<fx32, 2> r{};
    for (int k = 0; k < 2; ++k) {
        const std::int32_t v0 = at(index, k), v1 = at(next, k);
        r[std::size_t(k)] =
            (v0 * mul + (std::int32_t(fraction * std::uint32_t(v1 - v0)) >> 12)) >> shift;
    }
    return r;
}

// 020146d0: rotation index -> 3x3 matrix; returns true when the third row
// must be built from the cross product (rot9 entries).
bool decodeRotation(const AnimView& a, std::uint16_t index, std::array<fx32, 9>& m) {
    const std::size_t rot3 = a.base + rd32(a.d, a.base + 0xc),
                      rot9 = a.base + rd32(a.d, a.base + 0x10);
    if (index & 0x8000) {
        m.fill(0);
        const std::size_t e = rot3 + std::size_t(index & 0x7fff) * 6;
        const std::uint16_t info = rd16(a.d, e);
        const fx32 A = a.half(e + 2), B = a.half(e + 4);
        const unsigned pivot = info & 15;
        m[pivot] = (info & 0x10) ? -0x1000 : 0x1000;
        m[kPivot[pivot][0]] = A;
        m[kPivot[pivot][1]] = B;
        m[kPivot[pivot][2]] = (info & 0x20) ? -B : B;
        m[kPivot[pivot][3]] = (info & 0x40) ? -A : A;
        return false;
    }
    const std::size_t e = rot9 + std::size_t(index & 0x7fff) * 10;
    std::int16_t s[5];
    for (int k = 0; k < 5; ++k) {
        s[k] = std::int16_t(rd16(a.d, e + 2 * std::size_t(k)));
    }
    m[0] = s[0] >> 3;
    m[1] = s[1] >> 3;
    m[2] = s[2] >> 3;
    m[3] = s[3] >> 3;
    m[4] = s[4] >> 3;
    const int packed =
        ((s[4] & 7) << 12) | ((s[0] & 7) << 9) | ((s[1] & 7) << 6) | ((s[2] & 7) << 3) | (s[3] & 7);
    m[5] = std::int32_t(std::int16_t(std::uint16_t(packed << 3))) >> 3; // 13-bit signed
    return true;
}

void crossRow(std::array<fx32, 9>& m) {
    m[6] = (m[1] * m[5] - m[2] * m[4]) >> 12;
    m[7] = (m[2] * m[3] - m[0] * m[5]) >> 12;
    m[8] = (m[0] * m[4] - m[1] * m[3]) >> 12;
}

void normalizeRow(std::array<fx32, 9>& m, int row) {
    const Vec v = vecNormalize(
        {m[std::size_t(row * 3)], m[std::size_t(row * 3 + 1)], m[std::size_t(row * 3 + 2)]});
    m[std::size_t(row * 3)] = v.x;
    m[std::size_t(row * 3 + 1)] = v.y;
    m[std::size_t(row * 3 + 2)] = v.z;
}

// 02013f74 (stepped) / 0201438c (interpolated).
std::array<fx32, 9> sampleRotation(const AnimView& a, std::uint32_t info, std::uint32_t offset,
                                   fx32 frame, bool interpolate) {
    const std::size_t data = a.base + offset;
    auto idx = [&](unsigned i) {
        return rd16(a.d, data + i * 2);
    };
    std::array<fx32, 9> m{}, n{};
    auto blend = [&](unsigned i0, unsigned i1, int w0, int w1, std::uint32_t fraction, bool lerp) {
        const bool c0 = decodeRotation(a, idx(i0), m), c1 = decodeRotation(a, idx(i1), n);
        for (int k = 0; k < 9; ++k) {
            if (lerp) {
                m[std::size_t(k)] = m[std::size_t(k)] * w0 +
                                    (std::int32_t(fraction * std::uint32_t(n[std::size_t(k)] -
                                                                           m[std::size_t(k)])) >>
                                     12);
            } else {
                m[std::size_t(k)] = m[std::size_t(k)] * w0 + n[std::size_t(k)] * w1;
            }
        }
        normalizeRow(m, 0);
        normalizeRow(m, 1);
        if (c0 || c1) {
            crossRow(m);
        } else {
            normalizeRow(m, 2);
        }
    };
    auto single = [&](unsigned i) {
        if (decodeRotation(a, idx(i), m)) {
            crossRow(m);
        } else {
            normalizeRow(m, 2);
        }
    };
    unsigned f = unsigned(frame >> 12);
    const std::uint32_t step = info & 0xc0000000;
    if (!interpolate) {
        unsigned index = f;
        if (step) {
            const std::uint32_t last = info & 0x1fff0000;
            if (!(info & 0x40000000)) {
                if ((f & 3) == 0) {
                    index = f >> 2;
                } else if (f <= last >> 16) {
                    if (f & 1) {
                        unsigned three, one;
                        if (!(f & 2)) {
                            three = f >> 2;
                            one = three + 1;
                        } else {
                            one = f >> 2;
                            three = one + 1;
                        }
                        blend(three, one, 3, 1, 0, false);
                        return m;
                    }
                    index = f >> 2;
                    blend(index, index + 1, 1, 1, 0, false);
                    return m;
                } else {
                    index = (f & 3) + (last >> 18);
                }
            } else if ((f & 1) == 0) {
                index = f >> 1;
            } else if (f <= last >> 16) {
                index = f >> 1;
                blend(index, index + 1, 1, 1, 0, false);
                return m;
            } else {
                index = (last >> 17) + 1;
            }
        }
        single(index);
        return m;
    }
    std::uint32_t fraction = std::uint32_t(frame);
    unsigned index = f, next = f + 1;
    int mul = 1;
    if (a.frames() - 1 == f) {
        if (step) {
            f = (info & 0x40000000) ? (f & 1) + (f >> 1) : (f & 3) + (f >> 2);
        }
        if (!(a.flag() & 2)) {
            single(f);
            return m;
        }
        index = f;
        next = 0;
        fraction &= 0xfff;
    } else if (!step) {
        fraction &= 0xfff;
    } else {
        const unsigned last = (info & 0x1fff0000) >> 16;
        if (!(info & 0x40000000)) {
            if (f < last) {
                index = f >> 2;
                next = index + 1;
                fraction = std::uint32_t(frame) & 0x3fff;
                mul = 4;
            } else {
                index = (f & 3) + (f >> 2);
                next = index + 1;
                fraction &= 0xfff;
            }
        } else {
            if (f < last) {
                index = f >> 1;
                next = index + 1;
                fraction = std::uint32_t(frame) & 0x1fff;
                mul = 2;
            } else {
                index = (info & 0x1fff0000) >> 17;
                next = index + 1;
                fraction &= 0xfff;
            }
        }
    }
    blend(index, next, mul, 0, fraction, true);
    return m;
}
} // namespace

// ---------------------------------------------------------------------------
// Renderer

void Renderer::si3dCalc(Result& r, const fx32* s, std::uint8_t node, std::uint8_t parent,
                        std::uint32_t srtFlag) {
    auto bit = [&](unsigned i) {
        return (scaleOne_ >> i) & 1;
    };
    if (!(srtFlag & 4)) {
        r.scale = {s[0], s[1], s[2]};
        if (bit(parent)) {
            std::copy_n(s, 6, scaleCache_[node].values.begin());
            scaleOne_ &= ~(std::uint64_t(1) << node);
            r.flags |= 0x18;
            return;
        }
        scaleOne_ &= ~(std::uint64_t(1) << node);
        for (int k = 0; k < 6; ++k) {
            scaleCache_[node].values[std::size_t(k)] =
                fx32((std::int64_t(s[k]) * scaleCache_[parent].values[std::size_t(k)]) >> 12);
        }
        const auto& p = scaleCache_[parent].values;
        r.ex0 = {p[0], p[1], p[2]};
        r.ex1 = {p[3], p[4], p[5]};
        return;
    }
    r.flags |= 1;
    if (bit(parent)) {
        scaleOne_ |= std::uint64_t(1) << node;
        r.flags |= 0x18;
        return;
    }
    scaleCache_[node] = scaleCache_[parent];
    const auto& p = scaleCache_[parent].values;
    r.ex0 = {p[0], p[1], p[2]};
    r.ex1 = {p[3], p[4], p[5]};
}

void Renderer::si3dSend(Gpu3D& gpu, const Result& r) {
    auto send = [&](unsigned cmd, std::initializer_list<fx32> v) {
        std::vector<std::uint32_t> p;
        for (auto x : v) {
            p.push_back(std::uint32_t(x));
        }
        gpu.command(cmd, p);
    };
    const std::uint32_t noEx = r.flags & 0x18;
    bool transWithRot = false;
    if (noEx == 0) {
        send(MTX_SCALE, {r.ex1[0], r.ex1[1], r.ex1[2]});
    }
    if (!(r.flags & 4)) {
        transWithRot = noEx != 0;
        if (!transWithRot) {
            send(MTX_TRANS, {fx32((std::int64_t(r.translation[0]) * r.ex0[0]) >> 12),
                             fx32((std::int64_t(r.translation[1]) * r.ex0[1]) >> 12),
                             fx32((std::int64_t(r.translation[2]) * r.ex0[2]) >> 12)});
        }
    }
    if (!(r.flags & 2)) {
        std::vector<std::uint32_t> p;
        for (auto x : r.rotation) {
            p.push_back(std::uint32_t(x));
        }
        if (transWithRot) {
            for (auto x : r.translation) {
                p.push_back(std::uint32_t(x));
            }
            gpu.command(MTX_MULT_4x3, p);
        } else {
            gpu.command(MTX_MULT_3x3, p);
        }
    } else if (transWithRot) {
        send(MTX_TRANS, {r.translation[0], r.translation[1], r.translation[2]});
    }
    if (noEx == 0) {
        send(MTX_SCALE, {r.ex0[0], r.ex0[1], r.ex0[2]});
    }
    if (!(r.flags & 1)) {
        send(MTX_SCALE, {r.scale[0], r.scale[1], r.scale[2]});
    }
}

namespace {
// NNSG3dResNodeData fields after the 4-byte header: translation, rotation, scale.
struct NodeData {
    std::size_t trans = 0, rot = 0, scale = 0;
    std::uint16_t flag = 0;
};

NodeData nodeData(const ModelSet& m, unsigned node) {
    NodeData n;
    std::size_t at = m.nodes.at(node);
    n.flag = m.u16(at);
    std::size_t p = at + 4;
    if (!(n.flag & 1)) {
        n.trans = p;
        p += 12;
    }
    if (!(n.flag & 2)) {
        n.rot = p;
        p += (n.flag & 8) ? 4 : 16;
    }
    n.scale = p;
    return n;
}

void nodeRotation(const ModelSet& m, unsigned node, const NodeData& n, std::array<fx32, 9>& rot) {
    const std::size_t at = m.nodes.at(node);
    if (n.flag & 8) {
        rot.fill(0);
        const unsigned pivot = (n.flag & 0xf0) >> 4;
        const fx32 A = m.s16(n.rot), B = m.s16(n.rot + 2);
        rot[pivot] = (n.flag & 0x100) ? -0x1000 : 0x1000;
        rot[kPivot[pivot][0]] = A;
        rot[kPivot[pivot][1]] = B;
        rot[kPivot[pivot][2]] = (n.flag & 0x200) ? -B : B;
        rot[kPivot[pivot][3]] = (n.flag & 0x400) ? -A : A;
    } else {
        rot[0] = m.s16(at + 2);
        for (int k = 0; k < 8; ++k) {
            rot[std::size_t(k + 1)] = m.s16(n.rot + 2 * std::size_t(k));
        }
    }
}
} // namespace

// One node's transform for this frame: its rest pose or animation sample,
// with the model's scaling rules.
void Renderer::nodeResult(const ModelSet& m, unsigned node, Result& r, const std::uint8_t* sbc) {
    const NodeData n = nodeData(m, node);
    if (n.trans) {
        for (int k = 0; k < 3; ++k) {
            r.translation[std::size_t(k)] = fx32(m.u32(n.trans + 4 * std::size_t(k)));
        }
    } else {
        r.flags |= 4;
    }
    if (n.rot) {
        nodeRotation(m, node, n, r.rotation);
    } else {
        r.flags |= 2;
    }
    fx32 scale[6]{};
    if (!(n.flag & 4)) {
        for (int k = 0; k < 6; ++k) {
            scale[k] = fx32(m.u32(n.scale + 4 * std::size_t(k)));
        }
    }
    si3dCalc(r, scale, sbc[1], sbc[2], n.flag);
}

bool Renderer::jointResult(const RenderObject& o, unsigned node, Result& r,
                           const std::uint8_t* sbc) {
    const auto* j = o.joint;
    if (!j || !j->covers(node)) {
        return false;
    }
    const AnimView a{j->data_, j->anim_};
    const std::size_t tagAt = a.base + rd16(a.d, a.base + 0x14 + node * 2);
    const std::uint32_t tag = rd32(a.d, tagAt);
    std::size_t p = tagAt + 4;
    // 02013298: the frame is clamped to [0, frames*0x1000-1] before sampling.
    fx32 frame = o.jointFrame;
    if (frame >= fx32(a.frames()) << 12) {
        frame = (fx32(a.frames()) << 12) - 1;
    } else if (frame < 0) {
        frame = 0;
    }
    const bool interpolate = (frame & 0xfff) && (a.flag() & 1);
    fx32 scale[6]{};
    const ModelSet& m = *o.model;
    if (tag & 1) {
        r.flags = 7;
    } else {
        r.flags = 0;
        if ((tag & 6) == 0) {
            const std::uint32_t constBits[3] = {8, 0x10, 0x20};
            for (int k = 0; k < 3; ++k) {
                if (tag & constBits[k]) {
                    r.translation[std::size_t(k)] = fx32(rd32(a.d, p));
                    p += 4;
                } else {
                    r.translation[std::size_t(k)] =
                        sampleTranslation(a, rd32(a.d, p), rd32(a.d, p + 4), frame, interpolate);
                    p += 8;
                }
            }
        } else if (!(tag & 2)) { // base: the model's own translation (020132d4)
            const NodeData n = nodeData(m, node);
            if (n.trans) {
                for (int k = 0; k < 3; ++k) {
                    r.translation[std::size_t(k)] = fx32(m.u32(n.trans + 4 * std::size_t(k)));
                }
            } else {
                r.flags |= 4;
            }
        } else {
            r.flags |= 4;
        }
        if ((tag & 0xc0) == 0) {
            if (!(tag & 0x100)) {
                r.rotation = sampleRotation(a, rd32(a.d, p), rd32(a.d, p + 4), frame, interpolate);
                p += 8;
            } else {
                if (decodeRotation(a, std::uint16_t(rd32(a.d, p)), r.rotation)) {
                    crossRow(r.rotation);
                }
                p += 4;
            }
        } else if (!(tag & 0x40)) { // base rotation (020133fc)
            const NodeData n = nodeData(m, node);
            if (n.rot) {
                nodeRotation(m, node, n, r.rotation);
            } else {
                r.flags |= 2;
            }
        } else {
            r.flags |= 2;
        }
        if ((tag & 0x600) == 0) {
            const std::uint32_t constBits[3] = {0x800, 0x1000, 0x2000};
            for (int k = 0; k < 3; ++k) {
                if (tag & constBits[k]) {
                    scale[k] = fx32(rd32(a.d, p));
                    scale[k + 3] = fx32(rd32(a.d, p + 4));
                } else {
                    const auto v =
                        sampleScale(a, rd32(a.d, p), rd32(a.d, p + 4), frame, interpolate);
                    scale[k] = v[0];
                    scale[k + 3] = v[1];
                }
                p += 8;
            }
        } else if (!(tag & 0x200)) { // base scale (0201336c): the model's node scale and flag
            const NodeData n = nodeData(m, node);
            fx32 base[6]{};
            if (!(n.flag & 4)) {
                for (int k = 0; k < 6; ++k) {
                    base[k] = fx32(m.u32(n.scale + 4 * std::size_t(k)));
                }
            }
            si3dCalc(r, base, sbc[1], sbc[2], n.flag);
            return true;
        } else {
            r.flags |= 1;
        }
    }
    si3dCalc(r, scale, sbc[1], sbc[2], (r.flags & 1) ? 4 : 0);
    return true;
}

void Renderer::material(Gpu3D& gpu, const GlobalState& glb, const RenderObject& o, unsigned index,
                        bool& skipShapes) {
    const auto& mat = o.model->materials.at(index);
    std::uint32_t difAmb = (glb.difAmb & ~kMatMask[(mat.flag >> 6) & 7]) |
                           (mat.difAmb & kMatMask[(mat.flag >> 6) & 7]);
    std::uint32_t speEmi = (glb.speEmi & ~kMatMask[(mat.flag >> 9) & 7]) |
                           (mat.speEmi & kMatMask[(mat.flag >> 9) & 7]);
    std::uint32_t polyAttr = (glb.polyAttr & ~mat.polyAttrMask) | (mat.polyAttr & mat.polyAttrMask);
    if (o.material) {
        o.material->apply(o.model->materialNames.at(index), unsigned(o.materialFrame >> 12), difAmb,
                          speEmi, polyAttr);
    }
    if (!(polyAttr & 0x1f0000)) {
        skipShapes = true;
        return;
    }
    if (mat.flag & 0x20) {
        polyAttr &= ~0x1f0000u;
    }
    skipShapes = false;
    const std::uint32_t words[] = {0x00293130,        difAmb,         speEmi, polyAttr, 0x2b2a,
                                   mat.texImageParam, mat.texPlttBase};
    gpu.packed(words);
}

// Draws one model with its current animation frame (runs its SBC program).
void Renderer::draw(Gpu3D& gpu, const GlobalState& glb, const RenderObject& o) {
    const ModelSet& m = *o.model;
    const Bytes& d = m.data();
    scaleOne_ = 1;
    envelopeCached_ = 0;
    bool visible = true, materialCached = false, skipShapes = false;
    int currentMaterial = -1;
    std::size_t c = m.sbc;
    for (int guard = 0; guard < 100000; ++guard) {
        const std::uint8_t op = d.at(c), cmd = op & 0x1f, opt = op & 0xe0;
        const std::uint8_t* b = &d[c];
        switch (cmd) {
        case 0x00:
            c += 1;
            break;
        case 0x01:
            return;
        case 0x02:
            visible = b[2] & 1;
            c += 3;
            break;
        case 0x03:
            if (visible) {
                const std::uint32_t p = b[1];
                gpu.command(MTX_RESTORE, {&p, 1});
            }
            c += 2;
            break;
        case 0x04: {
            const unsigned idx = b[1];
            if (!(!visible && materialCached && int(idx) == currentMaterial)) {
                material(gpu, glb, o, idx, skipShapes);
                materialCached = true;
                currentMaterial = int(idx);
            }
            c += 2;
            break;
        }
        case 0x05: {
            if (visible && !skipShapes) {
                const std::size_t shp = m.shapes.at(b[1]);
                const std::size_t dl = shp + m.u32(shp + 8);
                const std::uint32_t size = m.u32(shp + 12);
                std::vector<std::uint32_t> words(size / 4);
                for (std::size_t i = 0; i < words.size(); ++i) {
                    words[i] = m.u32(dl + 4 * i);
                }
                gpu.packed(words);
            }
            c += 2;
            break;
        }
        case 0x06: {
            std::size_t length = 4;
            if (opt == 0x40 || opt == 0x60) {
                const std::uint32_t p = opt == 0x40 ? b[4] : b[5];
                gpu.command(MTX_RESTORE, {&p, 1});
                length = 5;
            }
            Result r;
            if (!jointResult(o, b[1], r, b)) {
                nodeResult(m, b[1], r, b);
            }
            si3dSend(gpu, r);
            if (opt == 0x20 || opt == 0x60) {
                ++length;
                const std::uint32_t p = b[4];
                gpu.command(MTX_STORE, {&p, 1});
            }
            c += length;
            break;
        }
        case 0x09: {
            // NODEMIX: weighted envelope of node matrices times their inverse binds.
            const unsigned count = b[2];
            auto cmd1 = [&](unsigned command, std::uint32_t v) {
                gpu.command(command, {&v, 1});
            };
            cmd1(MTX_MODE, 0);
            gpu.command(MTX_PUSH, {});
            gpu.command(MTX_IDENTITY, {});
            cmd1(MTX_MODE, 2);
            std::array<std::int32_t, 12> pos{};
            std::array<std::int32_t, 9> vec{};
            constexpr int posIndex[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
            constexpr int vecIndex[9] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
            for (unsigned i = 0; i < count; ++i) {
                const unsigned stack = b[3 + i * 3], node = b[4 + i * 3], weight = b[5 + i * 3];
                if (!((envelopeCached_ >> node) & 1)) {
                    envelopeCached_ |= std::uint64_t(1) << node;
                    cmd1(MTX_RESTORE, stack);
                    cmd1(MTX_MODE, 1);
                    std::vector<std::uint32_t> mm(12), nn(9);
                    for (int k = 0; k < 12; ++k) {
                        mm[std::size_t(k)] = m.u32(m.evp + node * 0x54 + 4 * std::size_t(k));
                    }
                    for (int k = 0; k < 9; ++k) {
                        nn[std::size_t(k)] = m.u32(m.evp + node * 0x54 + 0x30 + 4 * std::size_t(k));
                    }
                    gpu.command(MTX_MULT_4x3, mm);
                    envelopePosition_[node] = gpu.clipMatrix();
                    cmd1(MTX_MODE, 2);
                    gpu.command(MTX_MULT_3x3, nn);
                    envelopeVector_[node] = gpu.vectorMatrix();
                }
                const std::int64_t w = std::int64_t(weight) << 4;
                for (int k = 0; k < 12; ++k) {
                    pos[std::size_t(k)] += std::int32_t(
                        (w * envelopePosition_[node].m[std::size_t(posIndex[k])]) >> 12);
                }
                for (int k = 0; k < 9; ++k) {
                    vec[std::size_t(k)] +=
                        std::int32_t((w * envelopeVector_[node].m[std::size_t(vecIndex[k])]) >> 12);
                }
            }
            std::vector<std::uint32_t> v(12, 0), p(12);
            for (int k = 0; k < 9; ++k) {
                v[std::size_t(k)] = std::uint32_t(vec[std::size_t(k)]);
            }
            for (int k = 0; k < 12; ++k) {
                p[std::size_t(k)] = std::uint32_t(pos[std::size_t(k)]);
            }
            gpu.command(MTX_LOAD_4x3, v);
            cmd1(MTX_MODE, 1);
            gpu.command(MTX_LOAD_4x3, p);
            cmd1(MTX_MODE, 0);
            cmd1(MTX_POP, 1);
            cmd1(MTX_MODE, 2);
            cmd1(MTX_STORE, b[1]);
            c += (count + 1) * 3;
            break;
        }
        case 0x07: {
            // 02010fe8 NODEDESC_BB: keep the node's translation and axis
            // scales but drop its rotation, so the node faces the screen.
            // Clip matrix with an identity projection = the position matrix;
            // template 02045890: MTX_POP(1), MTX_MODE(2), MTX_LOAD_4x3, MTX_SCALE.
            std::size_t length = 2;
            if (opt == 0x40 || opt == 0x60) {
                length = 3;
                if (visible) {
                    const std::uint32_t p = opt == 0x40 ? b[2] : b[3];
                    gpu.command(MTX_RESTORE, {&p, 1});
                }
            }
            if (visible) {
                auto cmd1 = [&](unsigned command, std::uint32_t v) {
                    gpu.command(command, {&v, 1});
                };
                cmd1(MTX_MODE, 0);
                gpu.command(MTX_PUSH, {});
                gpu.command(MTX_IDENTITY, {});
                const Matrix clip = gpu.clipMatrix();
                auto row = [&](int r) {
                    return vecMag({clip.m[std::size_t(r * 4)], clip.m[std::size_t(r * 4 + 1)],
                                   clip.m[std::size_t(r * 4 + 2)]});
                };
                const std::uint32_t scale[3] = {std::uint32_t(row(0)), std::uint32_t(row(1)),
                                                std::uint32_t(row(2))};
                const std::uint32_t load[12] = {0x1000,
                                                0,
                                                0,
                                                0,
                                                0x1000,
                                                0,
                                                0,
                                                0,
                                                0x1000,
                                                std::uint32_t(clip.m[12]),
                                                std::uint32_t(clip.m[13]),
                                                std::uint32_t(clip.m[14])};
                cmd1(MTX_POP, 1);
                cmd1(MTX_MODE, 2);
                gpu.command(MTX_LOAD_4x3, load);
                gpu.command(MTX_SCALE, scale);
            }
            if (opt == 0x20 || opt == 0x60) {
                ++length;
                if (visible) {
                    const std::uint32_t p = b[2];
                    gpu.command(MTX_STORE, {&p, 1});
                }
            }
            c += length;
            break;
        }
        case 0x0b: {
            const fx32 s = opt == 0 ? m.posScale : m.invPosScale;
            const std::uint32_t p[3] = {std::uint32_t(s), std::uint32_t(s), std::uint32_t(s)};
            gpu.command(MTX_SCALE, p);
            c += 1;
            break;
        }
        default:
            throw std::runtime_error("g3d: unsupported SBC command " + std::to_string(cmd));
        }
    }
    throw std::runtime_error("g3d: SBC without RET");
}
} // namespace picarat::nds3d
