#pragma once
// Vertex and index buffers of the 3DS adventures (XPVB / XPVI). Every 2D
// animation, pop-up layer and 3D model draws triangles from these.
//
// XPVB: "XPVB", u16 offsets of three method-word streams (attribute list,
// constants, vertices), u16 vertex size, u32 vertex count. The attribute
// list has ten slots of {components, offset, size, kind}: kind 2 reads
// `components` floats per vertex at `offset`, kind 1 one constant from the
// constants stream at `offset`, kind 0 = absent. Slots seen: 0 position,
// 1 colour, 2 normal, 4 texture coordinates (2 or 3), 7 bone weights,
// 8 bone indices (all floats in every file of both games).
// XPVI: "XPVI", u16 primitive (0 a triangle list; 2 a strip with repeated
// indices joining its runs, the models' meshes), u16 offset of a
// method-word stream of u16 indices, u32 index count. Strips are returned
// as lists.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace picarat::adventure56 {

struct VertexBuffer {
    static constexpr int kSlots = 10;
    enum Slot { Position = 0, Colour = 1, Normal = 2, TexCoord = 4, Weights = 7, Bones = 8 };

    unsigned count = 0;
    std::array<int, kSlots> components{}; // 0: slot absent
    // Per slot, four floats per vertex (missing components 0, w 1).
    std::array<std::vector<float>, kSlots> values;

    bool has(int slot) const { return components[std::size_t(slot)] != 0; }

    const float* at(int slot, unsigned vertex) const {
        return values[std::size_t(slot)].data() + std::size_t(vertex) * 4;
    }
};

VertexBuffer parseVertexBuffer(std::span<const std::uint8_t> data);
std::vector<std::uint16_t> parseIndexBuffer(std::span<const std::uint8_t> data);

} // namespace picarat::adventure56
