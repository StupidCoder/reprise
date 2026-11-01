// =============================================================================================
// SceneTypes.hpp - the scene's vertex and mesh records
// =============================================================================================
//
// The importer packs all static and skinned geometry of the opening into one vertex buffer
// (vertices.bin); meshes are ranges of it with a material and texture bindings (scene.json).
// Skinned models (CharacterAnimation.hpp) rewrite their ranges every frame.
// =============================================================================================
#pragma once
#include "gpu/Device.hpp"

#include <stdexcept>

// One vertex as stored in vertices.bin (48 bytes): position, normal, texture coordinate, colour.
struct Vertex {
    float p[3];
    float n[3];
    float uv[2];
    float c[4];
};
static_assert(sizeof(Vertex) == 48);

// Texture wrap modes (PICA: 0 clamp to edge, 1 clamp to border, 2 repeat, 3 mirrored repeat).
struct TextureSampler {
    int wrapS = 2;
    int wrapT = 2;
    float border[4] = {0, 0, 0, 1};
};

struct Mesh {
    int first;     // first vertex in the buffer
    int count;     // vertex count (triangles)
    int texture;   // texture slot 0 (-1: none)
    int wrapS;
    int wrapT;
    int detail;   // texture slot 2
    int material;
    int texture0;   // texture slot 1
    bool sky;
    bool visible = true;       // the mesh's visibility (animations, hidden objects)
    bool objectShown = true;   // its placed object is alive and visible (stage packages)
    bool drawn() const { return visible && lodVisible && objectShown; }
    TextureSampler samplers[3];
    bool shadowOnly = false;   // shadow proxy (_shd) mesh: drawn only into the shadow map
    // Level of detail (stage objects with _lod models): the mesh's level (-1: no LOD) and its
    // object's switch parameters (InitModel thresholds, axes mask, LodDistanceOnly, levels).
    int lod = -1;
    bool lodVisible = true;   // the object's current level is this mesh's (visible stays the animation's)
    float lodThreshold[2] = {800, 1100};
    int lodAxes = 7, lodLevels = 1;
    bool lodDistanceOnly = false;
    bool character = false;     // the protagonist
    int characterForm = -1;     // the protagonist's power form of the mesh (0 Super, 1 Mini; -1 not his)
    bool actor = false;         // another animated actor
    bool translucent = false;   // blended or depth-masked material (casts no shadow)
    bool water = false;         // a "$water" material: drawn last, over a copy of the scene
    int object = -1;            // stage packages: the placed object (Session stageWorld) it belongs to
    // Its material discards fragments (an alpha test, PICA register 0x104): the full-geometry shadow
    // pass then needs the material's program; otherwise a depth-only one gives the same depths.
    bool alphaTested = true;
};

// A mesh's sampler for texture slot `slot` (PICA wrap modes, border colour). Scene textures filter
// linearly; the 1 x 1 white stand-in for a missing texture reads nearest (`nearest`).
inline captain_toad::gpu::Sampler meshSampler(const Mesh& mesh, int slot, bool nearest = false) {
    const auto& s = mesh.samplers[slot];
    if (s.wrapS < 0 || s.wrapS > 3 || s.wrapT < 0 || s.wrapT > 3) {
        throw std::runtime_error("Unsupported texture wrap");
    }
    captain_toad::gpu::Sampler sampler;
    sampler.wrapS = captain_toad::gpu::Wrap(s.wrapS);
    sampler.wrapT = captain_toad::gpu::Wrap(s.wrapT);
    sampler.minFilter = sampler.magFilter =
        nearest ? captain_toad::gpu::Filter::Nearest : captain_toad::gpu::Filter::Linear;
    sampler.border = {s.border[0], s.border[1], s.border[2], s.border[3]};
    return sampler;
}
