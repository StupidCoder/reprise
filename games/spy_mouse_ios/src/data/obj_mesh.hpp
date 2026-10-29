// The world map's pictures: Wavefront OBJ meshes with MTL materials exported from 3ds Max
// (res/world/worldNN/*.obj, read by Mesh::loadObjFile @0x190830 and Mesh::loadMtlFile
// @0x190094). The map draws them flat: Mesh::render @0x19164c hands OpenGL each vertex as
// (x, z, y), and the map's orthographic view drops the third value, so a mesh is a set of
// textured triangles on the ground plane (x right, z up). Faces with more than three corners
// are drawn as fans from their first corner. Every coordinate is scaled on loading (the map
// uses 1.2: its 400 x 266 background fills the 480 x 320 screen).
#pragma once

#include "data/game_files.hpp"

#include <optional>
#include <string>
#include <vector>

namespace doc::data {

struct ObjMesh {
    struct Corner {
        float x = 0, z = 0;  // scaled, ground plane
        float u = 0, v = 0;  // texture coordinates as exported (v = 0 at the image's bottom)
    };
    // A run of triangles with one material: its texture (path under res/, empty for none).
    struct Part {
        std::string texture;
        std::vector<Corner> triangles;  // three corners per triangle
    };
    std::vector<Part> parts;
    float minX = 0, maxX = 0, minZ = 0, maxZ = 0;
};

// `path` is the .obj under res/; its material library sits next to it, and texture names
// are taken from the materials' map_Kd file names in the same folder.
std::optional<ObjMesh> loadObjMesh(const GameFiles& files, const std::string& path, float scale);

}  // namespace doc::data
