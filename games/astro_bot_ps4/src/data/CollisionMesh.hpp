// Serialized collision mesh ("PELM"): the pre-built form of large static triangle meshes inside
// .pfb files (shape kind 7) and level collision files (.lcb). Read as the engine's deserializer
// does (0xdf3cc0), packed little-endian:
//
//   "PELM", u8 0, u8 7, u8 layout (only 2 occurs: float vertices with per-island bounds)
//   f32 centre[3], f32 halfExtent[3], u32 islandCount, u32 x4 (in-memory sizes), u32 totalSize,
//   f32 (0.025), zero padding to 0x40
//   0x40: u32 length of the mesh tree block counted from 0x40, u32 node count, nodes of 11 bytes
//         {u8 quantized min[3], u8 quantized max[3], u8 child kinds, u16 left, u16 right}
//   islands: u32, u8 vertexCount, u8 edgeCount, u8 facetCount, f32 min[3], f32 max[3],
//            vertexCount x f32[3], edgeCount x u8[4],
//            facetCount x {f32 normal[3], f32, u8 vertex[3], u8 edge[3], u32 user},
//            max(facetCount - 1, 1) x tree node {u8 min steps[3], u8 max steps[3], u8 child kinds,
//            u8 left, u8 right}
// Island vertex indices are local to the island.
#pragma once

#include "data/Bytes.hpp"
#include "data/PhysicsFile.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace beacon::data {

// Bounding-volume tree node of a mesh or an island. Children are given as kinds (2 bits each,
// left in bits 0-1, right in bits 2-3: 0 inner node, 1 leaf, other values none) and indices
// (node or leaf). The node's box is its parent's box shrunk by quantized steps of 1/255 of the
// parent's extent: min + q * minStep, max - q * maxStep.
struct MeshTreeNode {
    std::array<std::uint8_t, 3> minStep{}, maxStep{};
    std::uint8_t childKinds = 0;
    std::uint16_t left = 0, right = 0;
};

// Mesh edge (4 bytes): byte 2 is its angle type (2 bits; 0 flat, nonzero convex or concave -
// which is which: open); the other bytes as stored (vertex ids and tilt: open).
struct MeshEdge {
    std::array<std::uint8_t, 4> raw{};
    std::uint8_t angleType() const { return raw[2] & 3; }
};

struct MeshFacet {
    math::Vec3 normal;
    float thickness = 0;   // distance below the plane still counted as touching
    std::array<std::uint8_t, 3> vertex{}, edge{};
    std::uint32_t user = 0;
};

// A group of at most 255 facets with their own vertices, edges and tree (leaves are facets).
struct MeshIsland {
    math::Vec3 min, max;
    std::vector<math::Vec3> vertices;
    std::vector<MeshEdge> edges;
    std::vector<MeshFacet> facets;
    std::vector<MeshTreeNode> tree;
};

// Large triangle mesh as the physics library keeps it (layout 2): islands in a tree whose
// leaves are islands; the root box is centre +- halfExtent.
struct LargeMesh {
    math::Vec3 centre, halfExtent;
    std::vector<MeshTreeNode> tree;
    std::vector<MeshIsland> islands;
};

bool decodeLargeMesh(ByteView blob, LargeMesh& out, std::string* error = nullptr);

// Appends the mesh's triangles to `out`.
bool decodeCollisionMesh(ByteView blob, TriangleSoup& out, std::string* error = nullptr);

}   // namespace data
