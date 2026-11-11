#pragma once
#include "platform/scene3d.hpp"
#include <vulkan/vulkan.h>
#include <filesystem>
#include <memory>

// One repeatable image-array layer per material (white for untextured entries).
// Upload is recorded once on the same queue as the first draw. Lifetime is
// bounded by SceneGpu, whose completed per-eye fences protect replacement.
class SceneTextures {
public:
    SceneTextures();
    ~SceneTextures();
    void open(VkPhysicalDevice,VkDevice,VkDescriptorSetLayout,const std::filesystem::path&,
              const std::vector<platform::scene3d::Material>&);
    void upload(VkCommandBuffer);
    VkDescriptorSet descriptor() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
