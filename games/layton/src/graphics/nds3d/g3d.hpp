#pragma once
#include "graphics/nds3d/fx.hpp"
#include "graphics/nds3d/gx.hpp"
#include <string>
#include <vector>

// Native counterpart of the NitroSystem-style G3D runtime in the ARM9 binary
// (resource setup 020128bc, global state 020494f4, draw 02010168 and the SBC
// handlers at 02045920). It only emits GX commands into nds3d::Gpu3D, as the
// ROM's runtime writes them to the geometry FIFO. Formats: BMD0 (MDL0/TEX0),
// BCA0 (JNT0 joint animation), BMA0 (MAT0 material colour animation).
namespace picarat::nds3d {
using Bytes = std::vector<std::uint8_t>;

struct VramAllocator {
    std::uint32_t texture = 0, texture4x4 = 0x0, palette = 0;
};

class ModelSet {
public:
    explicit ModelSet(Bytes file);
    // 020128bc: copy TEX0 textures/palettes to VRAM and bind them to the
    // materials by name (0200efac / 0200f320).
    void setup(Gpu3D& gpu, VramAllocator& vram);

    struct Material {
        std::uint32_t difAmb = 0, speEmi = 0, polyAttr = 0, polyAttrMask = 0, texImageParam = 0,
                      texImageParamMask = 0;
        std::uint16_t texPlttBase = 0, flag = 0, originalWidth = 0, originalHeight = 0;
        fx32 magW = 0x1000, magH = 0x1000;
    };

    const Bytes& data() const { return data_; }

    // Model info box (x, y, z, w, h, d) scaled by boxPosScale and rounded (020f023c).
    std::array<fx32, 6> box() const;

    // 02012d08: polygon alpha of every material.
    void setAlphaAll(int alpha) {
        for (auto& m : materials) {
            m.polyAttr = (m.polyAttr & ~0x1f0000u) | (std::uint32_t(alpha & 31) << 16);
        }
    }

    std::size_t sbc = 0, evp = 0, nodeInfo = 0;
    std::uint8_t scalingRule = 0, texMtxMode = 0, nodeCount = 0;
    fx32 posScale = 0x1000, invPosScale = 0x1000;
    std::vector<std::size_t> nodes, shapes; // NNSG3dResNodeData / NNSG3dResShpData offsets
    std::vector<std::string> nodeNames, materialNames;
    std::vector<Material> materials;
    std::uint32_t u32(std::size_t at) const;
    std::uint16_t u16(std::size_t at) const;

    std::int16_t s16(std::size_t at) const { return std::int16_t(u16(at)); }

private:
    Bytes data_;
    std::size_t model_ = 0, matBase_ = 0, tex0_ = 0;
};

class JointAnimation {
public:
    explicit JointAnimation(Bytes file);

    // NNSG3dJntAnmResult (0x58 bytes in the ROM): flags (1 scale one, 2 rotation
    // zero, 4 translation zero, 8/0x10 no parent scale), scale, scale ex0/ex1,
    // 3x3 rotation, translation.
    unsigned frameCount() const { return frames_; }

    bool covers(unsigned node) const { return node < nodes_; }

    std::size_t base() const { return anim_; }

    const Bytes& data() const { return data_; }

private:
    friend class Renderer;
    Bytes data_;
    std::size_t anim_ = 0;
    unsigned frames_ = 0, nodes_ = 0;
};

class MaterialAnimation {
public:
    explicit MaterialAnimation(Bytes file);

    unsigned frameCount() const { return frames_; }

    // 02014b64: the material result at an integer frame (diffuse/ambient,
    // specular/emission, polygon alpha); false if the material is not animated.
    bool apply(const std::string& material, unsigned frame, std::uint32_t& difAmb,
               std::uint32_t& speEmi, std::uint32_t& polyAttr) const;

private:
    Bytes data_;
    std::size_t anim_ = 0;
    unsigned frames_ = 0;
    std::vector<std::string> names_;
    std::vector<std::size_t> entries_;
};

struct RenderObject {
    const ModelSet* model = nullptr;
    const JointAnimation* joint = nullptr;
    fx32 jointFrame = 0;
    const MaterialAnimation* material = nullptr;
    fx32 materialFrame = 0;
};

// NNS_G3dGlb (020494f4) as 0200f6d0 builds it, flushed by 0200f824 as one packet.
struct GlobalState {
    Matrix projection = Matrix::identity();
    Matrix camera = Matrix::identity(); // 4x3 look-at (02019688)
    std::array<std::uint32_t, 4> lightVectors{0x2d8b62d8, 0x40000200, 0x800001ff, 0xc0080000};
    std::uint32_t difAmb = 0x4210c210, speEmi = 0x4210c210, polyAttr = 0x1f008f,
                  viewport = 0xbfff0000;
    std::array<std::uint32_t, 4> lightColors{0x7fff, 0x4000001f, 0x800003e0, 0xc0007c00};
    Matrix baseRotation = Matrix::identity(); // 3x3 at +0xbc, translation +0xe0 (4x3 multiply)
    Vec baseTranslation{}, baseScale{0x1000, 0x1000, 0x1000};
    std::uint32_t texImageParam = 0;
    void flush(Gpu3D& gpu) const;
};

// Holds the runtime's cross-draw caches (Si3d scale cache 0204a55c, envelope
// caches 0204ab5c) like the ARM9 globals do.
class Renderer {
public:
    void draw(Gpu3D& gpu, const GlobalState& glb, const RenderObject& object); // 02010168
private:
    struct Result {
        std::uint32_t flags = 0;
        std::array<fx32, 3> scale{}, ex0{}, ex1{};
        std::array<fx32, 9> rotation{};
        std::array<fx32, 3> translation{};
    };

    struct ScaleCache {
        std::array<fx32, 6> values{};
    };

    std::array<ScaleCache, 64> scaleCache_{};
    std::array<Matrix, 64> envelopePosition_{}, envelopeVector_{};
    // Per-draw render state.
    std::uint64_t scaleOne_ = 0, envelopeCached_ = 0;
    void si3dCalc(Result& r, const fx32* scale, std::uint8_t node, std::uint8_t parent,
                  std::uint32_t srtFlag);       // 02015d6c
    void si3dSend(Gpu3D& gpu, const Result& r); // 02015c40
    void nodeResult(const ModelSet& m, unsigned node, Result& r,
                    const std::uint8_t* sbc); // NODEDESC model path
    bool jointResult(const RenderObject& o, unsigned node, Result& r,
                     const std::uint8_t* sbc); // 0201357c
    void material(Gpu3D& gpu, const GlobalState& glb, const RenderObject& o, unsigned index,
                  bool& skipShapes); // 020104dc
};
} // namespace picarat::nds3d
