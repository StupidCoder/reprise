#pragma once
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <memory>

// Diagnostic presentation adapter. Game logic remains in the existing module.
class GameBridge {
public:
    GameBridge();
    ~GameBridge();
    void open(XrSession session, XrSpace space, VkPhysicalDevice physical,
              VkDevice device, VkQueue queue, unsigned family, const char* content);
    void update(XrTime predicted_time, bool focused);
    void pause();
    const XrCompositionLayerBaseHeader* layer() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
