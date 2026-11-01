// Bounded reader for the BCH 0x22 resources of the opening (general BCH/PICA layout). Only the
// observed layout features are supported; anything else fails instead of vanishing. Values keep
// the exporters' semantics: float32 fields widen to double before any arithmetic.
#pragma once
#include "Binary.hpp"
#include "Json.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace captain_toad {

using Registers = std::map<uint32_t, uint32_t>;
using Vec4d = std::array<double, 4>;

struct CommandState {
    Registers regs;
    std::map<int, Vec4d> uniforms;          // float uniform registers (reversed component order)
    std::map<int, Vec4d> fixedAttributes;   // fixed vertex attributes by input slot
};

struct Sampler {
    uint32_t sourceOffset;
    uint8_t wrapS, wrapT, magFilter, minFilter;
    double lodBias;
    std::array<uint8_t, 4> border;
};

struct TexTransform {
    uint32_t flags;
    std::array<double, 5> v;   // scale u, scale v, rotation, translation u, translation v
    double operator[](int i) const { return i == 0 ? double(flags) : v[size_t(i - 1)]; }
};

struct Material {
    std::string name;
    std::array<std::optional<std::string>, 3> textures;
    std::vector<Sampler> samplers;
    uint32_t parameterOffset;
    json::Value metadata;   // {name: [int, ...]}
    Registers registers, fragment;
    uint16_t flags;
    double polygonOffset;
    std::array<TexTransform, 3> transforms;
    std::array<uint8_t, 48> colors;
    // Which material constant (0-5) each combiner stage's constant colour is (parameters +192,
    // three bytes of 4-bit indices, the even stage in the low nibble). Verified over every
    // ObjectData model: of the stages whose combiner mode reads the constant, 12003 hold exactly the
    // assigned material constant in their register; the other 53 have index 8 (no material
    // constant: the stage keeps its register value).
    std::array<uint8_t, 6> constantAssignment{};
    // Texture coordinate source per slot (0..2: the mesh's UV sets): vertex uniform c10, written
    // by the material's command list; the model vertex program picks v4/v5/v6 by it (routine at
    // its pc 309; traced: CollectItem glowMat c10 = (1, 0, 0), gpu-trace gem-a draw 338).
    std::array<int, 3> uvSet{};
    std::optional<int> shadowMapMode() const;   // $shadowmap metadata value, if any
};

struct Bone {
    std::string name;
    int parent;
    uint32_t flags = 0;   // H3D bone flags (bit 22: segment scale compensate)
    std::array<double, 3> scale, rotation, translation;
    std::array<double, 12> inverseBind;
};

struct Vertex {
    std::array<std::array<double, 4>, 16> v{};
    std::array<uint8_t, 16> n{};   // component count per semantic (0: absent)
    bool has(int s) const { return n[size_t(s)] != 0; }
};

struct Face {
    uint16_t skin;
    std::vector<uint16_t> palette;
    std::vector<uint32_t> indices;
    Registers registers;
};

struct Mesh {
    uint16_t material, flags, node, priority;
    bool visible;
    uint32_t commandOffset;
    CommandState commands;
    std::vector<Face> faces;
    std::vector<Vertex> vertices;
    const Vec4d& uniform(int i) const;
};

struct Model {
    std::string name;
    std::array<double, 12> transform;
    std::vector<Bone> bones;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
};

struct TextureRecord {
    std::string name;
    uint32_t offset, width, height, format;
    Registers registers;
};

json::Value registersJson(const Registers& r);
json::Value toJson(const Material& m);
json::Value toJson(const Bone& b);
json::Value toJson(const TextureRecord& t, bool withRegisters = true);
json::Value samplersJson(const std::vector<Sampler>& s);

class Bch {
public:
    explicit Bch(Bytes raw);

    const Bytes& bytes() const { return b_; }
    uint32_t u32(size_t p) const { return captain_toad::u32(b_, p); }
    uint16_t u16(size_t p) const { return captain_toad::u16(b_, p); }
    int16_t s16(size_t p) const { return captain_toad::s16(b_, p); }
    double f32(size_t p) const { return double(captain_toad::f32(b_, p)); }
    uint8_t u8(size_t p) const { return captain_toad::u8(b_, p); }
    std::optional<std::string> string(size_t p) const;
    std::optional<std::string> name(size_t p) const { return string(u32(p)); }
    std::string nameOf(size_t p) const;   // name that must exist
    std::vector<uint32_t> table(int slot) const;
    CommandState commands(size_t p, size_t n) const;
    json::Value metadata(size_t p) const;
    std::vector<Sampler> samplers(size_t p) const;
    std::vector<TextureRecord> textureRecords() const;
    std::vector<Model> models() const;
    // Element of table `slot` with the given name.
    uint32_t find(int slot, const std::string& name) const;

private:
    Bytes b_;
    uint32_t main_, strings_, gpu_, data_, ext_, rel_;
};

}   // namespace captain_toad
