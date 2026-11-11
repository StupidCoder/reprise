#pragma once
#include "platform/scene3d.hpp"
#include <vulkan/vulkan.h>
#include <filesystem>
#include <memory>

// Direct rendering on the XR Vulkan device. The portable mesh contains no API
// resources. Single sequential submission uses a shared depth/MSAA attachment.
class SceneGpu {
public:
    SceneGpu();
    ~SceneGpu();
    void open(VkPhysicalDevice,VkDevice,unsigned width,unsigned height,const std::filesystem::path& shaders);
    void set_mesh(std::shared_ptr<const platform::scene3d::Mesh>);
    void set_dynamic(const platform::scene3d::Mesh*);
    void draw(VkCommandBuffer,VkImage,const platform::scene3d::View&,platform::Color background);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
