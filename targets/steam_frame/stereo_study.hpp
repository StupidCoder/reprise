#pragma once
#include "xr_texture.hpp"
#include <array>
#include <filesystem>

// Frozen eye-pair diagnostic, not a gameplay rendering backend.
class StereoStudy {
public:
    void open(XrSession,XrSpace,VkPhysicalDevice,VkDevice,VkQueue,unsigned,const std::filesystem::path&);
    void update(const XrView& left,const XrView& right,bool recenter);
    const std::array<XrCompositionLayerQuad,2>& layers() const { return layers_; }
private:
    std::array<XrTexture,2> textures_;
    std::array<XrCompositionLayerQuad,2> layers_{};
    bool anchored_{};
    float distance_{}, nominal_ipd_{};
};
