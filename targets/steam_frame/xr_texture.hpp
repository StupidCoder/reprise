#pragma once
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <memory>
#include <functional>

// Owns only application resources. Swapchain images remain runtime-owned.
class XrTexture {
public:
    struct Timing { double ownership_ms{}, fence_ms{}; };
    XrTexture();
    ~XrTexture();
    void open(XrSession, VkPhysicalDevice, VkDevice, VkQueue, unsigned family, int width, int height);
    Timing upload(const void* rgba, int pitch);
    // Record a complete overwrite, ending in COLOR_ATTACHMENT_OPTIMAL. The
    // callback borrows the runtime image only inside acquire/wait/release.
    Timing render(const std::function<void(VkCommandBuffer,VkImage)>&);
    XrSwapchain swapchain() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
