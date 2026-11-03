// The physics library's large-mesh builder (0xde8550), which turns the run-time meshes of .pfb files
// (shape kind 6: a vertex block and an index block) into the layout-2 form the serialized meshes
// (kind 7) load into. Findings in .analysis/docs/physics.md ("Run-time meshes").
#pragma once

#include "data/CollisionMesh.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace beacon::physics::rigid {

// The parameters PhysicsWorldImpl (0xcb2170) passes for every run-time mesh.
struct MeshBuildParams {
    int facetLimit = 64;         // largest island before it is split
    float epsilon = 1e-5f;       // zero-area and merge tolerance
    float thickness = 0.025f;    // every facet's thickness
};

// Builds `out` from triangles (three indices each). Fails like the library: no vertices or
// triangles (0x80880001), an index out of range (0x80880009), every triangle eliminated
// (0x8088000d).
bool buildLargeMesh(const std::vector<math::Vec3>& vertices, const std::vector<std::uint32_t>& indices,
                    data::LargeMesh& out, std::string* error = nullptr, const MeshBuildParams& params = {});

}   // namespace beacon::physics::rigid
