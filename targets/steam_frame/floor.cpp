#include "floor.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(what) + " Vk " + std::to_string(result));
}
VkShaderModule shader(VkDevice device, const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!input || length <= 0 || length % 4 != 0) throw std::runtime_error("Invalid shader: " + file.string());
    std::vector<uint32_t> bytes(static_cast<size_t>(length)/4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) throw std::runtime_error("Read shader failed");
    VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create.codeSize = bytes.size()*4;
    create.pCode = bytes.data();
    VkShaderModule result{};
    check(vkCreateShaderModule(device, &create, nullptr, &result), "floor shader");
    return result;
}
struct Push {
    XrQuaternionf rotation;
    std::array<float,4> eye, tangent;
};
static_assert(sizeof(Push) == 48);
}

Floor::~Floor() {
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
}
void Floor::open(VkDevice device, VkRenderPass pass, const std::filesystem::path& shaders) {
    device_ = device;
    VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &range;
    check(vkCreatePipelineLayout(device, &layout, nullptr, &layout_), "floor pipeline layout");
    VkShaderModule vertex{}, fragment{};
    try {
        vertex = shader(device, shaders/"floor.vert.spv");
        fragment = shader(device, shaders/"floor.frag.spv");
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};
        for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment;
        VkPipelineVertexInputStateCreateInfo vertices{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1; blend.pAttachments = &attachment;
        const std::array dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = dynamics.size(); dynamic.pDynamicStates = dynamics.data();
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = stages.size(); pipeline.pStages = stages.data();
        pipeline.pVertexInputState = &vertices; pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples; pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic; pipeline.layout = layout_; pipeline.renderPass = pass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_), "floor pipeline");
    } catch (...) {
        if (vertex) vkDestroyShaderModule(device, vertex, nullptr);
        if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, vertex, nullptr);
    vkDestroyShaderModule(device, fragment, nullptr);
}
void Floor::draw(VkCommandBuffer command, VkExtent2D size, const XrView& view, float height) {
    VkViewport viewport{0,0,float(size.width),float(size.height),0,1};
    VkRect2D scissor{{0,0},size};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const Push push{view.pose.orientation, {view.pose.position.x,view.pose.position.y,view.pose.position.z,height},
        {std::tan(view.fov.angleLeft),std::tan(view.fov.angleRight),std::tan(view.fov.angleDown),std::tan(view.fov.angleUp)}};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(command, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command, 3, 1, 0, 0);
}
