#define XR_USE_GRAPHICS_API_VULKAN
#include "xr_texture.hpp"
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(what)+" Vk "+std::to_string(r));
}
void check(XrResult r, const char* what) {
    if (XR_FAILED(r)) throw std::runtime_error(std::string(what)+" XR "+std::to_string(r));
}
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}
}
struct XrTexture::Impl {
    VkDevice device{}; VkQueue queue{};
    VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapped{};
    VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{};
    XrSwapchain chain{};
    std::vector<XrSwapchainImageVulkanKHR> images;
    int width{}, height{};
    ~Impl() {
        if (device) vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        if (mapped) vkUnmapMemory(device, memory);
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        if (chain) xrDestroySwapchain(chain);
    }
};
XrTexture::XrTexture() : impl_(std::make_unique<Impl>()) {}
XrTexture::~XrTexture() = default;
XrSwapchain XrTexture::swapchain() const { return impl_->chain; }
void XrTexture::open(XrSession session, VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                     unsigned family, int width, int height) {
    auto& p = *impl_;
    p.device = device; p.queue = queue; p.width = width; p.height = height;
    uint32_t count{};
    check(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "texture formats");
    std::vector<int64_t> formats(count);
    check(xrEnumerateSwapchainFormats(session, count, &count, formats.data()), "texture formats");
    if (std::find(formats.begin(), formats.end(), VK_FORMAT_R8G8B8A8_SRGB) == formats.end())
        throw std::runtime_error("XR texture requires RGBA8 sRGB");
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    create.format = VK_FORMAT_R8G8B8A8_SRGB;
    create.width = width; create.height = height;
    create.sampleCount = create.faceCount = create.arraySize = create.mipCount = 1;
    check(xrCreateSwapchain(session, &create, &p.chain), "texture swapchain");
    check(xrEnumerateSwapchainImages(p.chain, 0, &count, nullptr), "texture images");
    p.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    check(xrEnumerateSwapchainImages(p.chain, count, &count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(p.images.data())), "texture images");
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = VkDeviceSize(width)*height*4; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    check(vkCreateBuffer(device, &buffer, nullptr, &p.buffer), "staging buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, p.buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t type = memory.memoryTypeCount;
    constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u<<i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
            type = i; break;
        }
    if (type == memory.memoryTypeCount) throw std::runtime_error("No coherent texture staging memory");
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = requirements.size; alloc.memoryTypeIndex = type;
    check(vkAllocateMemory(device, &alloc, nullptr, &p.memory), "staging allocation");
    check(vkBindBufferMemory(device, p.buffer, p.memory, 0), "bind staging");
    check(vkMapMemory(device, p.memory, 0, VK_WHOLE_SIZE, 0, &p.mapped), "map staging");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = family; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device, &pool, nullptr, &p.pool), "texture command pool");
    VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command.commandPool = p.pool; command.commandBufferCount = 1; command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    check(vkAllocateCommandBuffers(device, &command, &p.command), "texture commands");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device, &fence, nullptr, &p.fence), "texture fence");
}
XrTexture::Timing XrTexture::upload(const void* rgba, int pitch) {
    auto& p = *impl_;
    for (int y = 0; y < p.height; ++y)
        std::memcpy(static_cast<char*>(p.mapped)+size_t(y)*p.width*4,
                    static_cast<const char*>(rgba)+size_t(y)*pitch, size_t(p.width)*4);
    return render([&](VkCommandBuffer command,VkImage image) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Acquired image is fully overwritten.
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.imageExtent = {uint32_t(p.width),uint32_t(p.height),1};
    vkCmdCopyBufferToImage(command,p.buffer,barrier.image,barrier.newLayout,1,&copy);
    barrier.oldLayout = barrier.newLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // OpenXR release contract.
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,0,nullptr,0,nullptr,1,&barrier);
    });
}
XrTexture::Timing XrTexture::render(const std::function<void(VkCommandBuffer,VkImage)>& record) {
    auto& p=*impl_;
    const auto ownership_start = Clock::now();
    uint32_t index{};
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    check(xrAcquireSwapchainImage(p.chain, &acquire, &index), "acquire texture image");
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout = XR_INFINITE_DURATION;
    check(xrWaitSwapchainImage(p.chain, &wait), "wait texture image");
    const auto ownership_end = Clock::now();
    check(vkResetCommandBuffer(p.command, 0), "reset texture command");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(p.command, &begin), "begin texture command");
    try {
        record(p.command,p.images[index].image);
        check(vkEndCommandBuffer(p.command), "end texture command");
    } catch(...) {
        // No commands were submitted: discard recording and return ownership.
        vkResetCommandBuffer(p.command,0);
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(p.chain,&release);
        throw;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&p.command;
    const auto fence_start = Clock::now();
    check(vkQueueSubmit(p.queue,1,&submit,p.fence), "submit texture");
    check(vkWaitForFences(p.device,1,&p.fence,VK_TRUE,UINT64_MAX), "texture fence");
    const auto fence_end = Clock::now();
    check(vkResetFences(p.device,1,&p.fence), "reset texture fence");
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    check(xrReleaseSwapchainImage(p.chain,&release), "release texture image");
    return {elapsed(ownership_start,ownership_end),elapsed(fence_start,fence_end)};
}
