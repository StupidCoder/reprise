#pragma once
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <memory>

// Owns only application resources. Swapchain images remain runtime-owned.
class XrTexture {
public:
    struct Timing { double ownership_ms{}, fence_ms{}; };
    XrTexture();
    ~XrTexture();
    void open(XrSession, VkPhysicalDevice, VkDevice, VkQueue, unsigned family, int width, int height);
    Timing upload(const void* rgba, int pitch);
    XrSwapchain swapchain() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
