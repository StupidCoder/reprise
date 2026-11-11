#pragma once
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>
#include "platform/host.hpp"
class Ring {
public:
    Ring();
    ~Ring();
    void open(XrSession, XrSpace, VkPhysicalDevice, VkDevice, VkQueue, unsigned family,
              const std::filesystem::path& catalog, const char* data = nullptr, bool cylinder = false);
    void update(double time, const XrPosef& head, const std::array<std::optional<XrPosef>,2>& aim,
                const std::array<bool,2>& clicked, bool reduced_motion, unsigned layer_budget,
                bool focused, bool back, float scroll, const platform::Controls& game_controls,
                const std::array<bool,2>& held);
    void suspend(double now);
    const std::vector<XrCompositionLayerQuad>& layers() const;
    const std::vector<XrCompositionLayerCylinderKHR>& cylinders() const;
    void recenter();
    void append_pointers(const XrPosef& head, const std::array<std::optional<XrPosef>,2>& aim,
                         const std::array<bool,2>& pressed);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
