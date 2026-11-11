#pragma once
#include "app/game_module.hpp"
#include "xr_texture.hpp"
#include "scene_gpu.hpp"
#include <array>

class StereoScene {
public:
    void open(XrSession,XrSpace,VkPhysicalDevice,VkDevice,VkQueue,unsigned,const std::filesystem::path& shaders);
    void draw(const app::SceneSurface&,const XrPosef& panel,const std::array<XrView,2>& eyes);
    const std::array<XrCompositionLayerQuad,2>& layers() const { return layers_; }
private:
    std::array<XrTexture,2> textures_;
    SceneGpu gpu_; // destroyed before the runtime swapchains whose views it owns
    std::array<XrCompositionLayerQuad,2> layers_{};
    unsigned frames_{};
    double wall_ms_{}, fence_ms_{};
};
