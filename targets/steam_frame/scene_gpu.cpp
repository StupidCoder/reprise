#include "scene_gpu.hpp"
#include "scene_textures.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>

namespace {
void check(VkResult r,const char* what) {
    if(r!=VK_SUCCESS) throw std::runtime_error(std::string(what)+" Vk "+std::to_string(r));
}
float linear(unsigned char value) {
    const float v=value/255.f;
    return v<=.04045f ? v/12.92f : std::pow((v+.055f)/1.055f,2.4f);
}
struct Vertex { float position[3],colour[3],uv[2],material; };
struct Push { std::array<float,4> eye,right,up,forward,tangent,planes; };
VkShaderModule shader(VkDevice device,const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    const auto bytes=input.tellg();
    if(!input || bytes<=0 || bytes%4) throw std::runtime_error("Invalid scene shader");
    std::vector<uint32_t> code(std::size_t(bytes)/4); input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(code.data()),bytes)) throw std::runtime_error("Read scene shader");
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=std::size_t(bytes);info.pCode=code.data();VkShaderModule result{};
    check(vkCreateShaderModule(device,&info,nullptr,&result),"scene shader"); return result;
}
}
struct SceneGpu::Impl {
    VkPhysicalDevice physical{};VkDevice device{};unsigned width{},height{};
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    std::unique_ptr<SceneTextures> textures;
    VkDescriptorSetLayout texture_layout{};
    std::filesystem::path resources;
    VkRenderPass pass{};VkPipelineLayout layout{};VkPipeline pipeline{};
    struct Attachment { VkImage image{};VkDeviceMemory memory{};VkImageView view{}; } depth,colour;
    struct Target { VkImageView view{};VkFramebuffer framebuffer{}; };
    std::map<VkImage,Target> targets;
    VkBuffer dynamic_vertices{};VkDeviceMemory dynamic_memory{};unsigned dynamic_count{};
    static constexpr unsigned dynamic_capacity=32768;
    VkBuffer vertices{};VkDeviceMemory vertex_memory{};unsigned count{};
    std::shared_ptr<const platform::scene3d::Mesh> mesh;
    uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)
            if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("No suitable scene memory");
    }
    VkImageView view(VkImage image,VkFormat format,VkImageAspectFlags aspect) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image=image;info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.format=format;
        info.subresourceRange={aspect,0,1,0,1};VkImageView result{};
        check(vkCreateImageView(device,&info,nullptr,&result),"scene image view");return result;
    }
    void attachment(Attachment& out,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType=VK_IMAGE_TYPE_2D;info.format=format;info.extent={width,height,1};
        info.mipLevels=info.arrayLayers=1;info.samples=samples;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;
        check(vkCreateImage(device,&info,nullptr,&out.image),"scene attachment");
        VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(device,out.image,&requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize=requirements.size;alloc.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device,&alloc,nullptr,&out.memory),"scene attachment memory");
        check(vkBindImageMemory(device,out.image,out.memory,0),"bind scene image");
        out.view=view(out.image,format,aspect);
    }
    ~Impl() {
        if(!device) return;
        vkDeviceWaitIdle(device);
        textures.reset();
        for(const auto& [image,target]:targets) {
            (void)image; if(target.framebuffer) vkDestroyFramebuffer(device,target.framebuffer,nullptr);
            if(target.view) vkDestroyImageView(device,target.view,nullptr);
        }
        if(pipeline) vkDestroyPipeline(device,pipeline,nullptr);
        if(layout) vkDestroyPipelineLayout(device,layout,nullptr);
        if(texture_layout) vkDestroyDescriptorSetLayout(device,texture_layout,nullptr);
        if(pass) vkDestroyRenderPass(device,pass,nullptr);
        for(auto* a:{&depth,&colour}) {
            if(a->view) vkDestroyImageView(device,a->view,nullptr);
            if(a->image) vkDestroyImage(device,a->image,nullptr);
            if(a->memory) vkFreeMemory(device,a->memory,nullptr);
        }
        if(dynamic_vertices) vkDestroyBuffer(device,dynamic_vertices,nullptr);
        if(dynamic_memory) vkFreeMemory(device,dynamic_memory,nullptr);
        if(vertices) vkDestroyBuffer(device,vertices,nullptr);
        if(vertex_memory) vkFreeMemory(device,vertex_memory,nullptr);
    }
};
SceneGpu::SceneGpu():impl_(std::make_unique<Impl>()) {}
SceneGpu::~SceneGpu()=default;
void SceneGpu::open(VkPhysicalDevice physical,VkDevice device,unsigned width,unsigned height,const std::filesystem::path& shaders) {
    auto& p=*impl_;p.physical=physical;p.device=device;p.width=width;p.height=height;
    VkImageFormatProperties color_properties{},depth_properties{};
    check(vkGetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_R8G8B8A8_SRGB,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,0,&color_properties),"scene colour support");
    check(vkGetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_D32_SFLOAT,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,0,&depth_properties),"scene depth support");
    const auto supported=color_properties.sampleCounts&depth_properties.sampleCounts;
    p.samples=supported&VK_SAMPLE_COUNT_4_BIT ? VK_SAMPLE_COUNT_4_BIT : supported&VK_SAMPLE_COUNT_2_BIT ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;
    const bool msaa=p.samples!=VK_SAMPLE_COUNT_1_BIT;
    p.attachment(p.depth,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
    if(msaa) p.attachment(p.colour,VK_FORMAT_R8G8B8A8_SRGB,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    std::array<VkAttachmentDescription,3> descriptions{};
    auto& color=descriptions[0];color.format=VK_FORMAT_R8G8B8A8_SRGB;color.samples=p.samples;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;color.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;color.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    auto& depth=descriptions[1];depth=color;depth.format=VK_FORMAT_D32_SFLOAT;
    depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;depth.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    descriptions[2]=color;descriptions[2].samples=VK_SAMPLE_COUNT_1_BIT;
    descriptions[2].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;descriptions[2].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    const VkAttachmentReference cr{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},dr{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},rr{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1;subpass.pColorAttachments=&cr;subpass.pDepthStencilAttachment=&dr;subpass.pResolveAttachments=msaa ? &rr : nullptr;
    VkSubpassDependency dependency{};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;
    dependency.srcStageMask=dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount=msaa ? 3 : 2;pass.pAttachments=descriptions.data();pass.subpassCount=1;pass.pSubpasses=&subpass;
    pass.dependencyCount=1;pass.pDependencies=&dependency;check(vkCreateRenderPass(device,&pass,nullptr,&p.pass),"scene render pass");
    p.resources=shaders.parent_path()/"resources";
    const VkDescriptorSetLayoutBinding texture_binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo texture_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    texture_layout.bindingCount=1;texture_layout.pBindings=&texture_binding;
    check(vkCreateDescriptorSetLayout(device,&texture_layout,nullptr,&p.texture_layout),"scene texture layout");
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(Push)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&p.texture_layout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
    check(vkCreatePipelineLayout(device,&layout,nullptr,&p.layout),"scene layout");
    VkShaderModule vert{},frag{};
    try {
        vert=shader(device,shaders/"scene.vert.spv");frag=shader(device,shaders/"scene.frag.spv");
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};
        for(auto& stage:stages) { stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main"; }
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vert;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=frag;
        const VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription,4> attributes{{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12},{2,0,VK_FORMAT_R32G32_SFLOAT,24},{3,0,VK_FORMAT_R32_SFLOAT,32}}};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;input.vertexAttributeDescriptionCount=unsigned(attributes.size());input.pVertexAttributeDescriptions=attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=p.samples;
        VkPipelineDepthStencilStateCreateInfo depth_state{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth_state.depthTestEnable=depth_state.depthWriteEnable=VK_TRUE;depth_state.depthCompareOp=VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blend_attachment{};blend_attachment.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&blend_attachment;
        const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_states.data();
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pipeline.stageCount=2;pipeline.pStages=stages.data();
        pipeline.pVertexInputState=&input;pipeline.pInputAssemblyState=&assembly;pipeline.pViewportState=&viewport;pipeline.pRasterizationState=&raster;
        pipeline.pMultisampleState=&samples;pipeline.pDepthStencilState=&depth_state;pipeline.pColorBlendState=&blend;pipeline.pDynamicState=&dynamic;
        pipeline.layout=p.layout;pipeline.renderPass=p.pass;
        check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline),"scene pipeline");
    } catch(...) { if(vert) vkDestroyShaderModule(device,vert,nullptr);if(frag) vkDestroyShaderModule(device,frag,nullptr);throw; }
    vkDestroyShaderModule(device,vert,nullptr);vkDestroyShaderModule(device,frag,nullptr);
    std::printf("Scene GPU: %ux%u, %u samples, D32 depth; direct XR Vulkan rendering\n",width,height,unsigned(p.samples));
}
void SceneGpu::set_mesh(std::shared_ptr<const platform::scene3d::Mesh> mesh) {
    auto& p=*impl_;if(mesh==p.mesh) return;
    if(!mesh || mesh->triangles.empty()) throw std::runtime_error("Empty scene mesh");
    std::vector<Vertex> data;data.reserve(mesh->triangles.size()*3);
    for(const auto& triangle:mesh->triangles) {
        const auto color=mesh->materials.at(triangle.material).colour;
        for(auto index:triangle.vertices) {
            const auto& vertex=mesh->vertices.at(index); const auto v=vertex.position;
            data.push_back({{float(v.x),float(v.y),float(v.z)},{linear(color.r),linear(color.g),linear(color.b)},
                {float(vertex.u),float(vertex.v)},float(triangle.material)});
        }
    }
    auto textures=std::make_unique<SceneTextures>();
    textures->open(p.physical,p.device,p.texture_layout,p.resources,mesh->materials);
    p.textures=std::move(textures);
    // Previous draw finishes before this call (XrTexture's fence).
    if(p.vertices) vkDestroyBuffer(p.device,p.vertices,nullptr);
    p.vertices={};
    if(p.vertex_memory) vkFreeMemory(p.device,p.vertex_memory,nullptr);
    p.vertex_memory={};
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=data.size()*sizeof(Vertex);buffer.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    check(vkCreateBuffer(p.device,&buffer,nullptr,&p.vertices),"scene vertices");
    VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(p.device,p.vertices,&requirements);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=requirements.size;
    alloc.memoryTypeIndex=p.memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(p.device,&alloc,nullptr,&p.vertex_memory),"scene vertex memory");
    check(vkBindBufferMemory(p.device,p.vertices,p.vertex_memory,0),"bind scene vertices");
    void* mapped{};check(vkMapMemory(p.device,p.vertex_memory,0,VK_WHOLE_SIZE,0,&mapped),"map scene vertices");
    std::memcpy(mapped,data.data(),data.size()*sizeof(Vertex));vkUnmapMemory(p.device,p.vertex_memory);
    p.count=unsigned(data.size());p.mesh=std::move(mesh);
    std::printf("Scene GPU uploaded %u vertices once\n",p.count);
}
void SceneGpu::set_dynamic(const platform::scene3d::Mesh* mesh) {
    auto& p=*impl_;p.dynamic_count=0;
    if(!mesh || mesh->triangles.empty()) return;
    if(mesh->triangles.size()*3>p.dynamic_capacity) throw std::runtime_error("Dynamic scene capacity exceeded");
    if(!p.dynamic_vertices) {
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size=p.dynamic_capacity*sizeof(Vertex);buffer.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        check(vkCreateBuffer(p.device,&buffer,nullptr,&p.dynamic_vertices),"dynamic scene buffer");
        VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(p.device,p.dynamic_vertices,&requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=requirements.size;
        alloc.memoryTypeIndex=p.memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(p.device,&alloc,nullptr,&p.dynamic_memory),"dynamic scene memory");
        check(vkBindBufferMemory(p.device,p.dynamic_vertices,p.dynamic_memory,0),"bind dynamic scene");
    }
    std::vector<Vertex> data;data.reserve(mesh->triangles.size()*3);
    for(const auto& triangle:mesh->triangles) {
        const auto color=p.mesh->materials.at(triangle.material).colour;
        for(auto index:triangle.vertices) {
            const auto& vertex=mesh->vertices.at(index);const auto v=vertex.position;
            data.push_back({{float(v.x),float(v.y),float(v.z)},{linear(color.r),linear(color.g),linear(color.b)},
                {float(vertex.u),float(vertex.v)},float(triangle.material)});
        }
    }
    void* mapped{};check(vkMapMemory(p.device,p.dynamic_memory,0,VK_WHOLE_SIZE,0,&mapped),"map dynamic scene");
    std::memcpy(mapped,data.data(),data.size()*sizeof(Vertex));vkUnmapMemory(p.device,p.dynamic_memory);
    p.dynamic_count=unsigned(data.size());
}
void SceneGpu::draw(VkCommandBuffer command,VkImage image,const platform::scene3d::View& view,platform::Color background) {
    auto& p=*impl_;
    auto& target=p.targets[image];
    if(!target.framebuffer) {
        target.view=p.view(image,VK_FORMAT_R8G8B8A8_SRGB,VK_IMAGE_ASPECT_COLOR_BIT);
        const bool msaa=p.samples!=VK_SAMPLE_COUNT_1_BIT;
        const std::array attachments{msaa ? p.colour.view : target.view,p.depth.view,target.view};
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=p.pass;
        framebuffer.attachmentCount=msaa ? 3 : 2;framebuffer.pAttachments=attachments.data();
        framebuffer.width=p.width;framebuffer.height=p.height;framebuffer.layers=1;
        check(vkCreateFramebuffer(p.device,&framebuffer,nullptr,&target.framebuffer),"scene framebuffer");
    }
    p.textures->upload(command);
    std::array<VkClearValue,2> clear{};
    clear[0].color={{linear(background.r),linear(background.g),linear(background.b),1}};clear[1].depthStencil={1,0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=p.pass;begin.framebuffer=target.framebuffer;
    begin.renderArea.extent={p.width,p.height};begin.clearValueCount=2;begin.pClearValues=clear.data();
    vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0,0,float(p.width),float(p.height),0,1};VkRect2D scissor{{0,0},{p.width,p.height}};
    vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,p.pipeline);
    const auto descriptor=p.textures->descriptor();
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,p.layout,0,1,&descriptor,0,nullptr);
    const VkDeviceSize offset=0;vkCmdBindVertexBuffers(command,0,1,&p.vertices,&offset);
    const auto vector=[](platform::scene3d::Vec3 v){return std::array<float,4>{float(v.x),float(v.y),float(v.z),0};};
    const Push push{vector(view.eye),vector(view.right),vector(view.up),vector(view.forward),
        {float(view.left),float(view.right_edge),float(view.bottom),float(view.top)},
        {float(view.near_z),float(view.far_z),0,0}};
    vkCmdPushConstants(command,p.layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
    vkCmdDraw(command,p.count,1,0,0);
    if(p.dynamic_count) {
        vkCmdBindVertexBuffers(command,0,1,&p.dynamic_vertices,&offset);
        vkCmdDraw(command,p.dynamic_count,1,0,0);
    }
    vkCmdEndRenderPass(command);
}
