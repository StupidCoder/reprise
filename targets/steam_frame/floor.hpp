#pragma once
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <filesystem>

class Floor {
public:
    ~Floor();
    void open(VkDevice device, VkRenderPass pass, const std::filesystem::path& shaders);
    void draw(VkCommandBuffer command, VkExtent2D size, const XrView& view, float height);
private:
    VkDevice device_{};
    VkPipelineLayout layout_{};
    VkPipeline pipeline_{};
};
