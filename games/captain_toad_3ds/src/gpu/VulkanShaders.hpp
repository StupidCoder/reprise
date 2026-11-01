// =============================================================================================
// VulkanShaders.hpp - the shader dialect compiled for Vulkan (SPIR-V) with its reflection
// =============================================================================================
//
// The renderers' shaders (gpu/Device.hpp "Shader dialect") are GLSL bodies with plain `uniform`
// declarations. For Vulkan they are compiled by glslang as GLSL 4.50 under its relaxed Vulkan
// rules: the loose uniforms of both stages form one uniform block (set 0, binding 0, std140),
// samplers and stage interfaces get automatic bindings and locations, matched by name between
// the stages. The reflection returned here tells the backend where each uniform lives in the
// block, which binding each sampler has and which location each vertex input has.
//
// Two backend additions are compiled in:
//   - depth range: the vertex stage's clip z is mapped from OpenGL's -w..w to Vulkan's 0..w
//     after the shader's own main (its matrices keep the OpenGL convention);
//   - clamp to border: Vulkan implementations without custom border colours (MoltenVK) only
//     border in black or white, so texture() reads through a function that returns the bound
//     sampler's border colour outside 0..1 on the axes that clamp to border (the backend sets
//     deviceBorder_<sampler> and deviceBorderAxes_<sampler> per draw; samplers clamp to edge).
// =============================================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace captain_toad::gpu::vulkan {

struct UniformInfo {
    int offset = 0;        // bytes into the block
    int type = 0;          // OpenGL type enumeration (GL_FLOAT, GL_FLOAT_VEC3, GL_FLOAT_MAT4, ...)
    int arraySize = 1;     // elements
    int arrayStride = 0;   // bytes between elements (0: not an array)
};

// Hashing names for lookups by string_view (renderers set uniforms by name for every draw).
struct NameHash {
    using is_transparent = void;
    size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
};

struct CompiledProgram {
    std::vector<uint32_t> vertex, fragment;   // SPIR-V
    int blockSize = 0;                        // the uniform block (0: none)
    std::unordered_map<std::string, UniformInfo, NameHash, std::equal_to<>> uniforms;
    std::map<std::string, int, std::less<>> samplers;   // name -> binding (set 0)
    std::map<std::string, int, std::less<>> inputs;     // vertex input name -> location
};

// Compiles a dialect program; throws with the compiler's log (`name` identifies it). With a cache
// folder set, compiled programs are kept there by a hash of their sources and read back.
CompiledProgram compileProgram(std::string_view vertex, std::string_view fragment, std::string_view name);
void setCacheDirectory(const std::filesystem::path& directory);

// Headsets (multiview): every vertex stage ends by mapping its clip position into the drawn view's
// with `uniform mat4 deviceView[2]` (indexed by gl_ViewIndex; the device writes identities for
// ordinary targets), before the depth remap. Off on other devices (their shaders stay as they are).
void setMultiview(bool on);

}   // namespace captain_toad::gpu::vulkan
