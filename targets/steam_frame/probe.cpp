#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "floor.hpp"
#include "platform/host.hpp"
#ifdef REPRISE_FRAME_LAUNCHER
#include "app/catalog.hpp"
#include "app/library.hpp"
#include "app/quick_save.hpp"
#include "gameplay.hpp"
#endif
#include <memory>
#ifdef REPRISE_FRAME_GAME_BRIDGE
#include "game_bridge.hpp"
#include "ring.hpp"
#include "stereo_study.hpp"
#endif

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }

constexpr auto stereo = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
constexpr auto color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
constexpr const char* frame_extension = "XR_VALVE_frame_controller_interaction";
constexpr std::array hands{"/user/hand/left", "/user/hand/right"};
constexpr XrQuaternionf identity{0, 0, 0, 1};

void check(XrResult result, const char* operation) {
    if (XR_FAILED(result))
        throw std::runtime_error(std::string(operation) + " (XR " + std::to_string(result) + ")");
}
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " (Vk " + std::to_string(result) + ")");
}

XrVector3f operator+(XrVector3f a, XrVector3f b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
XrVector3f operator-(XrVector3f a, XrVector3f b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
XrVector3f operator*(XrVector3f a, float s) { return {a.x*s, a.y*s, a.z*s}; }
XrVector3f cross(XrVector3f a, XrVector3f b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
XrVector3f rotate(XrQuaternionf q, XrVector3f v) {
    const XrVector3f axis{q.x, q.y, q.z};
    const auto twice = cross(axis, v) * 2.f;
    return v + twice * q.w + cross(axis, twice);
}

struct Hand {
    XrPath path{};
    XrSpace space{};
    std::optional<XrPosef> aim;
    bool armed{}, pressed{}, clicked{}, back_armed{}, center_armed{}, back_clicked{};
};

struct Eye {
    XrSwapchain chain{};
    VkExtent2D size{};
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> targets;
};

class Probe {
public:
    ~Probe();
    void run(int seconds, const char* content, bool room, bool benchmark, const char* catalog, bool reduced, const char* data, const char* stereo);

private:
    void open_xr();
    void open_vulkan();
    void open_session();
    void open_actions();
    void open_targets();
    void events();
    void input(XrTime time);
    void frame();
    void draw_eye(const Eye& eye, uint32_t image, const XrView& view);
    void profiles();

    template<class Proc> Proc proc(const char* name) {
        PFN_xrVoidFunction result{};
        check(xrGetInstanceProcAddr(instance_, name, &result), name);
        return reinterpret_cast<Proc>(result);
    }
    XrPath path(const char* value) {
        XrPath result{};
        check(xrStringToPath(instance_, value, &result), "xrStringToPath");
        return result;
    }

    XrInstance instance_{};
    XrSystemId system_{};
    XrSession session_{};
    XrSpace space_{};
    XrActionSet actions_{};
    XrAction aim_{}, trigger_{}, back_{}, recenter_{}, scroll_{};
    bool back_pressed_{}, recenter_pending_{};
    float scroll_value_{};
    std::array<XrAction,11> game_buttons_{};
    std::array<bool,11> game_armed_{}, game_previous_{};
    platform::Controls game_controls_{};
    XrSessionState state_{XR_SESSION_STATE_UNKNOWN};
    XrEnvironmentBlendMode blend_{};
    bool frame_profile_{}, running_{}, exiting_{};
    bool cylinder_supported_{};
    std::array<Hand, 2> hands_{};
    std::array<Eye, 2> eyes_{};
    VkInstance vk_instance_{};
    VkPhysicalDevice physical_{};
    VkDevice device_{};
    VkQueue queue_{};
    uint32_t queue_family_{};
    VkRenderPass pass_{};
    VkCommandPool pool_{};
    VkCommandBuffer commands_{};
    VkFence fence_{};
    VkQueryPool room_queries_{};
    float timestamp_period_{};
    unsigned timestamp_bits_{};
    std::vector<double> room_gpu_times_,room_ownership_times_,room_fence_times_;
    uint64_t submitted_{};
    uint64_t waited_{}, skipped_{};
    std::vector<double> render_times_, end_times_, begin_times_, locate_times_, room_times_, content_times_;
    std::unique_ptr<Floor> floor_;
    bool benchmark_{};
    bool reduced_motion_{};
    uint32_t max_layers_{};
#ifdef REPRISE_FRAME_GAME_BRIDGE
    std::unique_ptr<GameBridge> bridge_;
    std::unique_ptr<Ring> ring_;
    std::unique_ptr<StereoStudy> stereo_;
#endif
};

Probe::~Probe() {
    if (device_) vkDeviceWaitIdle(device_);
    floor_.reset();
#ifdef REPRISE_FRAME_GAME_BRIDGE
    ring_.reset();
    stereo_.reset();
    bridge_.reset();
#endif
    if (device_) vkDeviceWaitIdle(device_);
    if (room_queries_) vkDestroyQueryPool(device_,room_queries_,nullptr);
    if (fence_) vkDestroyFence(device_, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    for (auto& eye : eyes_) {
        for (auto target : eye.targets) vkDestroyFramebuffer(device_, target, nullptr);
        for (auto view : eye.views) vkDestroyImageView(device_, view, nullptr);
        if (eye.chain) xrDestroySwapchain(eye.chain);
    }
    if (pass_) vkDestroyRenderPass(device_, pass_, nullptr);
    for (auto& hand : hands_) if (hand.space) xrDestroySpace(hand.space);
    if (space_) xrDestroySpace(space_);
    if (session_) xrDestroySession(session_);
    if (actions_) xrDestroyActionSet(actions_);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (vk_instance_) vkDestroyInstance(vk_instance_, nullptr);
    if (instance_) xrDestroyInstance(instance_);
}

void Probe::open_xr() {
    uint32_t count{};
    check(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr), "enumerate XR extensions");
    std::vector<XrExtensionProperties> available(count, {XR_TYPE_EXTENSION_PROPERTIES});
    check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, available.data()), "read XR extensions");
    for (const auto& ext : available) std::printf("XR extension %s v%u\n", ext.extensionName, ext.extensionVersion);
    const auto has = [&](const char* name) {
        return std::ranges::any_of(available, [=](const auto& ext) {
            return std::strcmp(ext.extensionName, name) == 0;
        });
    };
    if (!has(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))
        throw std::runtime_error("Runtime does not support XR_KHR_vulkan_enable2");
    std::vector<const char*> extensions{XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    cylinder_supported_ = has(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    if (cylinder_supported_) extensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    frame_profile_ = has(frame_extension);
    if (frame_profile_) extensions.push_back(frame_extension);

    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(info.applicationInfo.applicationName, "Reprise Steam Frame Probe");
    info.applicationInfo.applicationVersion = 1;
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.enabledExtensionNames = extensions.data();
    check(xrCreateInstance(&info, &instance_), "xrCreateInstance");

    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    check(xrGetInstanceProperties(instance_, &properties), "xrGetInstanceProperties");
    const auto version = properties.runtimeVersion;
    std::printf("XR runtime %s %u.%u.%u\n", properties.runtimeName,
                XR_VERSION_MAJOR(version), XR_VERSION_MINOR(version), XR_VERSION_PATCH(version));
    XrSystemGetInfo system_info{XR_TYPE_SYSTEM_GET_INFO};
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    check(xrGetSystem(instance_, &system_info, &system_), "xrGetSystem");
    XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};
    check(xrGetSystemProperties(instance_, system_, &system_properties), "xrGetSystemProperties");
    max_layers_ = system_properties.graphicsProperties.maxLayerCount;
    std::printf("XR max layers %u, max swapchain %ux%u\n", max_layers_,
                system_properties.graphicsProperties.maxSwapchainImageWidth,
                system_properties.graphicsProperties.maxSwapchainImageHeight);

    check(xrEnumerateEnvironmentBlendModes(instance_, system_, stereo, 0, &count, nullptr), "enumerate blend modes");
    std::vector<XrEnvironmentBlendMode> modes(count);
    check(xrEnumerateEnvironmentBlendModes(instance_, system_, stereo, count, &count, modes.data()), "read blend modes");
    if (std::ranges::find(modes, XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == modes.end())
        throw std::runtime_error("The probe requires opaque stereo presentation");
    blend_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
}

void Probe::open_vulkan() {
    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    check(proc<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR")
              (instance_, system_, &requirements), "xrGetVulkanGraphicsRequirements2KHR");
    uint32_t loader_api = VK_API_VERSION_1_0;
    const auto query_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if (query_version) check(query_version(&loader_api), "vkEnumerateInstanceVersion");
    const XrVersion loader_version = XR_MAKE_VERSION(VK_VERSION_MAJOR(loader_api),
                                                    VK_VERSION_MINOR(loader_api), 0);
    const XrVersion api = XR_MAKE_VERSION(XR_VERSION_MAJOR(requirements.minApiVersionSupported),
                                         XR_VERSION_MINOR(requirements.minApiVersionSupported), 0);
    if (api > loader_version || api > requirements.maxApiVersionSupported)
        throw std::runtime_error("Vulkan loader is below the OpenXR runtime's API requirement");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Reprise Steam Frame Probe";
    app.apiVersion = VK_MAKE_VERSION(XR_VERSION_MAJOR(api), XR_VERSION_MINOR(api), 0);
    VkInstanceCreateInfo vk_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vk_info.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR xr_info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xr_info.systemId = system_;
    xr_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xr_info.vulkanCreateInfo = &vk_info;
    VkResult vk_result{};
    check(proc<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR")
              (instance_, &xr_info, &vk_instance_, &vk_result), "xrCreateVulkanInstanceKHR");
    check(vk_result, "create Vulkan instance");

    XrVulkanGraphicsDeviceGetInfoKHR gpu{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gpu.systemId = system_;
    gpu.vulkanInstance = vk_instance_;
    check(proc<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR")
              (instance_, &gpu, &physical_), "xrGetVulkanGraphicsDevice2KHR");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    std::printf("Vulkan device %s, API %u.%u\n", properties.deviceName,
                VK_VERSION_MAJOR(app.apiVersion), VK_VERSION_MINOR(app.apiVersion));

    uint32_t count{};
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, families.data());
    const auto family = std::ranges::find_if(families, [](const auto& q) {
        return q.queueCount && (q.queueFlags & VK_QUEUE_GRAPHICS_BIT);
    });
    if (family == families.end()) throw std::runtime_error("No Vulkan graphics queue");
    queue_family_ = static_cast<uint32_t>(family - families.begin());

    constexpr float priority = 1.f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = queue_family_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo dev{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dev.queueCreateInfoCount = 1;
    dev.pQueueCreateInfos = &queue;
    XrVulkanDeviceCreateInfoKHR xr_dev{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xr_dev.systemId = system_;
    xr_dev.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xr_dev.vulkanPhysicalDevice = physical_;
    xr_dev.vulkanCreateInfo = &dev;
    check(proc<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR")
              (instance_, &xr_dev, &device_, &vk_result), "xrCreateVulkanDeviceKHR");
    check(vk_result, "create Vulkan device");
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = queue_family_;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &alloc, &commands_), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device_, &fence, nullptr, &fence_), "vkCreateFence");
    timestamp_bits_=families.at(queue_family_).timestampValidBits;
    timestamp_period_=properties.limits.timestampPeriod;
    if(timestamp_bits_) {
        VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};query.queryType=VK_QUERY_TYPE_TIMESTAMP;query.queryCount=2;
        check(vkCreateQueryPool(device_,&query,nullptr,&room_queries_),"room GPU timestamp pool");
    }
}

void Probe::open_session() {
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = vk_instance_;
    binding.physicalDevice = physical_;
    binding.device = device_;
    binding.queueFamilyIndex = queue_family_;
    XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};
    create.next = &binding;
    create.systemId = system_;
    check(xrCreateSession(instance_, &create, &session_), "xrCreateSession");

    XrReferenceSpaceCreateInfo ref{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    ref.poseInReferenceSpace.orientation = identity;
    check(xrCreateReferenceSpace(session_, &ref, &space_), "create local space");
}

void Probe::open_actions() {
    XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(set.actionSetName, "probe");
    std::strcpy(set.localizedActionSetName, "Reprise Probe");
    check(xrCreateActionSet(instance_, &set, &actions_), "xrCreateActionSet");
    const std::array paths{path(hands[0]), path(hands[1])};
    const auto action = [&](XrActionType type, const char* name, const char* label, XrAction& out) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType = type;
        std::strcpy(info.actionName, name);
        std::strcpy(info.localizedActionName, label);
        info.countSubactionPaths = paths.size();
        info.subactionPaths = paths.data();
        check(xrCreateAction(actions_, &info, &out), "xrCreateAction");
    };
    action(XR_ACTION_TYPE_POSE_INPUT, "aim", "Aim", aim_);
    action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", trigger_);
    action(XR_ACTION_TYPE_BOOLEAN_INPUT, "back", "Back", back_);
    action(XR_ACTION_TYPE_BOOLEAN_INPUT, "recenter", "Recenter", recenter_);
    action(XR_ACTION_TYPE_VECTOR2F_INPUT, "scroll", "Scroll", scroll_);

    const std::array<const char*,11> names{"game_a","game_b","game_x","game_y","game_menu","game_lb","game_rb","game_up","game_down","game_left","game_right"};
    for(size_t i=0;i<names.size();++i) action(XR_ACTION_TYPE_BOOLEAN_INPUT,names[i],names[i],game_buttons_[i]);
    std::vector<XrActionSuggestedBinding> bindings(10);
    for (std::size_t i = 0; i < hands_.size(); ++i) {
        const std::string hand = hands[i];
        bindings[5*i] = {aim_, path((hand + "/input/aim/pose").c_str())};
        bindings[5*i+1] = {trigger_, path((hand + "/input/trigger/value").c_str())};
        bindings[5*i+2] = {back_, path((hand + (i==0 ? "/input/y/click" : "/input/b/click")).c_str())};
        bindings[5*i+3] = {recenter_, path((hand + "/input/thumbstick/click").c_str())};
        bindings[5*i+4] = {scroll_, path((hand + "/input/thumbstick").c_str())};
    }
    const auto suggest = [&](const char* profile) {
        XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        info.interactionProfile = path(profile);
        info.countSuggestedBindings = bindings.size();
        info.suggestedBindings = bindings.data();
        return xrSuggestInteractionProfileBindings(instance_, &info);
    };
    const std::array<const char*,5> touch_buttons{"/user/hand/right/input/a/click","/user/hand/right/input/b/click","/user/hand/left/input/x/click","/user/hand/left/input/y/click","/user/hand/left/input/menu/click"};
    for(size_t i=0;i<touch_buttons.size();++i) bindings.push_back({game_buttons_[i],path(touch_buttons[i])});
    check(suggest("/interaction_profiles/oculus/touch_controller"), "suggest Touch controller bindings");
    if (frame_profile_) {
        // Frame has its D-pad and View button on the left; ABXY are on the right.
        bindings[2] = {back_, path("/user/hand/left/input/view/click")};
        bindings.resize(10);
        const std::array<const char*,11> frame_buttons{"/user/hand/right/input/a/click","/user/hand/right/input/b/click","/user/hand/right/input/x/click","/user/hand/right/input/y/click","/user/hand/right/input/menu/click","/user/hand/left/input/bumper/click","/user/hand/right/input/bumper/click","/user/hand/left/input/dpad_up/click","/user/hand/left/input/dpad_down/click","/user/hand/left/input/dpad_left/click","/user/hand/left/input/dpad_right/click"};
        for(size_t i=0;i<frame_buttons.size();++i) bindings.push_back({game_buttons_[i],path(frame_buttons[i])});
        const auto result = suggest("/interaction_profiles/valve/frame_controller_valve");
        if (XR_FAILED(result)) std::printf("Frame bindings rejected (XR %d), Touch bindings remain\n", result);
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actions_;
    check(xrAttachSessionActionSets(session_, &attach), "xrAttachSessionActionSets");
    for (std::size_t i = 0; i < hands_.size(); ++i) {
        auto& hand = hands_[i];
        hand.path = paths[i];
        XrActionSpaceCreateInfo pose{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        pose.action = aim_;
        pose.subactionPath = hand.path;
        pose.poseInActionSpace.orientation = identity;
        check(xrCreateActionSpace(session_, &pose, &hand.space), "xrCreateActionSpace");
    }
}

void Probe::open_targets() {
    std::array<XrViewConfigurationView, 2> views{};
    for (auto& view : views) view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    uint32_t count{};
    check(xrEnumerateViewConfigurationViews(instance_, system_, stereo, views.size(), &count,
                                             views.data()), "enumerate stereo views");
    if (count != views.size()) throw std::runtime_error("Expected two OpenXR views");

    check(xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "enumerate formats");
    std::vector<int64_t> formats(count);
    check(xrEnumerateSwapchainFormats(session_, count, &count, formats.data()), "read formats");
    constexpr std::array preferred{VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                                   VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM};
    const auto selected = std::ranges::find_if(preferred, [&](VkFormat candidate) {
        return std::ranges::find(formats, candidate) != formats.end();
    });
    if (selected == preferred.end()) throw std::runtime_error("No supported RGBA/BGRA swapchain format");
    const auto format = *selected;
    std::printf("Swapchain VkFormat %d, queue family %u\n", format, queue_family_);

    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = color_layout;
    const VkAttachmentReference color{0, color_layout};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    check(vkCreateRenderPass(device_, &pass, nullptr, &pass_), "vkCreateRenderPass");

    for (std::size_t i = 0; i < eyes_.size(); ++i) {
        auto& eye = eyes_[i];
        eye.size = {views[i].recommendedImageRectWidth, views[i].recommendedImageRectHeight};
        XrSwapchainCreateInfo chain{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        chain.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        chain.format = format;
        chain.width = eye.size.width;
        chain.height = eye.size.height;
        chain.sampleCount = chain.arraySize = chain.faceCount = chain.mipCount = 1;
        check(xrCreateSwapchain(session_, &chain, &eye.chain), "xrCreateSwapchain");
        check(xrEnumerateSwapchainImages(eye.chain, 0, &count, nullptr), "enumerate images");
        std::vector<XrSwapchainImageVulkan2KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        check(xrEnumerateSwapchainImages(eye.chain, count, &count,
              reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())), "read images");
        eye.views.resize(count);
        eye.targets.resize(count);
        for (uint32_t j = 0; j < count; ++j) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = images[j].image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device_, &view, nullptr, &eye.views[j]), "vkCreateImageView");
            VkFramebufferCreateInfo target{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            target.renderPass = pass_;
            target.attachmentCount = 1;
            target.pAttachments = &eye.views[j];
            target.width = eye.size.width;
            target.height = eye.size.height;
            target.layers = 1;
            check(vkCreateFramebuffer(device_, &target, nullptr, &eye.targets[j]), "vkCreateFramebuffer");
        }
        std::printf("Eye %zu: %ux%u, %u images\n", i, eye.size.width, eye.size.height, count);
    }
}

void Probe::profiles() {
    for (const auto& hand : hands_) {
        XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
        check(xrGetCurrentInteractionProfile(session_, hand.path, &profile), "xrGetCurrentInteractionProfile");
        if (profile.interactionProfile == XR_NULL_PATH) continue;
        std::array<char, XR_MAX_PATH_LENGTH> name{};
        uint32_t length{};
        check(xrPathToString(instance_, profile.interactionProfile, name.size(), &length, name.data()),
              "xrPathToString");
        std::printf("Hand %s profile %s\n", &hand == &hands_[0] ? "left" : "right", name.data());
    }
}

void Probe::events() {
    for (;;) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        const auto result = xrPollEvent(instance_, &event);
        if (result == XR_EVENT_UNAVAILABLE) return;
        check(result, "xrPollEvent");
        if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            exiting_ = true;
            return;
        }
        if (event.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) profiles();
        if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            std::printf("Reference space change pending; clearing controller input\n");
            for (auto& hand : hands_) {
                hand.aim.reset();
                hand.armed = hand.pressed = hand.back_armed = hand.center_armed = false;
                game_armed_.fill(false); game_previous_.fill(false);
            }
        }
        if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
        state_ = reinterpret_cast<const XrEventDataSessionStateChanged&>(event).state;
        std::printf("Session state %d\n", state_);
        if (state_ != XR_SESSION_STATE_FOCUSED) {
            for (auto& hand : hands_) {
                hand.aim.reset();
                hand.armed = hand.pressed = hand.back_armed = hand.center_armed = false;
                game_armed_.fill(false); game_previous_.fill(false);
            }
        }
        switch (state_) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
            begin.primaryViewConfigurationType = stereo;
            check(xrBeginSession(session_, &begin), "xrBeginSession");
            running_ = true;
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            check(xrEndSession(session_), "xrEndSession");
            running_ = false;
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            exiting_ = true;
            return;
        default:
            break;
        }
    }
}

void Probe::input(XrTime time) {
    for (auto& hand : hands_) hand.clicked = false;
    back_pressed_ = recenter_pending_ = false;
    scroll_value_ = 0;
    game_controls_={};
    if (state_ != XR_SESSION_STATE_FOCUSED) return;
    const XrActiveActionSet active{actions_, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    const auto result = xrSyncActions(session_, &sync);
    check(result, "xrSyncActions");
    for (std::size_t i = 0; i < hands_.size(); ++i) {
        auto& hand = hands_[i];
        const bool had_aim = hand.aim.has_value();
        hand.aim.reset();
        if (result != XR_SESSION_NOT_FOCUSED) {
            XrActionStateGetInfo action{XR_TYPE_ACTION_STATE_GET_INFO};
            action.action = aim_;
            action.subactionPath = hand.path;
            XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
            check(xrGetActionStatePose(session_, &action, &pose), "xrGetActionStatePose");
            if (pose.isActive) {
                XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
                check(xrLocateSpace(hand.space, space_, time, &location), "xrLocateSpace");
                constexpr auto tracked = XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                                         XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                                         XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                         XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
                if ((location.locationFlags & tracked) == tracked) hand.aim = location.pose;
            }
        }
        if (had_aim != hand.aim.has_value())
            std::printf("%s aim %s\n", i == 0 ? "Left" : "Right", hand.aim ? "tracked" : "lost");

        XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
        if (hand.aim) {
            XrActionStateGetInfo action{XR_TYPE_ACTION_STATE_GET_INFO};
            action.action = trigger_;
            action.subactionPath = hand.path;
            check(xrGetActionStateFloat(session_, &action, &trigger), "xrGetActionStateFloat");
        }
        if (!trigger.isActive) {
            hand.armed = hand.pressed = false;
        } else if (trigger.currentState <= .35f) {
            hand.armed = true;
            hand.pressed = false;
        } else if (hand.armed && trigger.currentState >= .65f) {
            hand.armed = false;
            hand.pressed = true;
            hand.clicked = true;
            std::printf("%s trigger pressed (%.2f)\n", i == 0 ? "Left" : "Right", trigger.currentState);
        }
        const auto button = [&](XrAction handle, bool& armed, bool& pressed) {
            if (!hand.aim) { armed=false; return; }
            XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
            get.action=handle; get.subactionPath=hand.path;
            XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
            check(xrGetActionStateBoolean(session_, &get, &value), "get navigation button");
            if (!value.isActive) armed=false;
            else if (!value.currentState) armed=true;
            else if (armed) { pressed=true; armed=false; }
        };
        hand.back_clicked=false;
        button(back_,hand.back_armed,hand.back_clicked);
        back_pressed_ |= hand.back_clicked;
        if(i==0) game_controls_.menu=hand.back_clicked;
        button(recenter_,hand.center_armed,recenter_pending_);
        if (hand.aim) {
            XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
            get.action=scroll_; get.subactionPath=hand.path;
            XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F};
            check(xrGetActionStateVector2f(session_, &get, &value), "get scroll stick");
            if(value.isActive) {
                const std::array stick{value.currentState.x,-value.currentState.y};
                if(i==0) { game_controls_.analog_stick=stick; game_controls_.left=stick[0]<-.3f; game_controls_.right=stick[0]>.3f; game_controls_.up=stick[1]<-.3f; game_controls_.down=stick[1]>.3f; }
                else game_controls_.camera_stick=stick;
            }
            if (value.isActive && std::abs(value.currentState.y) > std::max(.25f,std::abs(scroll_value_)))
                scroll_value_=value.currentState.y;
        }
    }
    std::array<bool,11> held{};
    for(size_t i=0;i<game_buttons_.size();++i) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO}; get.action=game_buttons_[i];
        XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};
        check(xrGetActionStateBoolean(session_,&get,&value),"game button state");
        const bool tracked=(i==5 || i>=7) ? hands_[0].aim.has_value() : hands_[1].aim.has_value();
        if(!tracked || !value.isActive) game_armed_[i]=false;
        else if(!value.currentState) game_armed_[i]=true;
        held[i]=tracked && value.isActive && value.currentState && game_armed_[i];
    }
    game_controls_.fire=held[0]; game_controls_.button2=held[1];
    game_controls_.face_x=held[2]; game_controls_.face_y=held[3];
    game_controls_.menu |= held[4] && !game_previous_[4];
    game_controls_.start=held[3]; // Y also supplies legacy Start/F1.
    game_controls_.shoulder_left=held[5]; game_controls_.shoulder_right=held[6];
    game_controls_.up |= held[7]; game_controls_.down |= held[8];
    game_controls_.left |= held[9]; game_controls_.right |= held[10];
    game_controls_.trigger_left=hands_[0].pressed; game_controls_.trigger_right=hands_[1].pressed;
    game_previous_=held;
}

// Project world-space markers through each eye's asymmetric field of view.
// Clear rectangles keep the smoke test independent of a shader toolchain.
void Probe::draw_eye(const Eye& eye, uint32_t image, const XrView& view) {
    const VkClearValue black{{{0.f, 0.f, 0.f, 1.f}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = pass_;
    pass.framebuffer = eye.targets.at(image);
    pass.renderArea.extent = eye.size;
    pass.clearValueCount = 1;
    pass.pClearValues = &black;
    vkCmdBeginRenderPass(commands_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    if (floor_) floor_->draw(commands_, eye.size, view, -1.35f);

    const auto q = view.pose.orientation;
    const XrQuaternionf inverse{-q.x, -q.y, -q.z, q.w};
    const float left = std::tan(view.fov.angleLeft), right = std::tan(view.fov.angleRight);
    const float down = std::tan(view.fov.angleDown), up = std::tan(view.fov.angleUp);
    const float width = static_cast<float>(eye.size.width), height = static_cast<float>(eye.size.height);
    const auto marker = [&](XrVector3f world, float size, std::array<float, 3> rgb) {
        const auto p = rotate(inverse, world - view.pose.position);
        const float depth = -p.z;
        if (depth < .08f) return;
        const float x = (p.x / depth - left) / (right - left) * width;
        const float y = (up - p.y / depth) / (up - down) * height;
        const float radius = std::clamp(size / depth * height / (up - down), 1.f, height / 4.f);
        const auto clip = [](float value, float limit) { return static_cast<int32_t>(std::clamp(value, 0.f, limit)); };
        const int32_t x0 = clip(x - radius, width), x1 = clip(x + radius, width);
        const int32_t y0 = clip(y - radius, height), y1 = clip(y + radius, height);
        if (x0 >= x1 || y0 >= y1) return;
        const VkClearAttachment color{VK_IMAGE_ASPECT_COLOR_BIT, 0, {{{rgb[0], rgb[1], rgb[2], 1.f}}}};
        const VkClearRect rect{{{x0, y0}, {uint32_t(x1-x0), uint32_t(y1-y0)}}, 0, 1};
        vkCmdClearAttachments(commands_, 1, &color, 1, &rect);
    };
    for (int z = -6; !floor_ && z <= 6; ++z) {
        for (int x = -6; x <= 6; ++x) {
            const float light = .24f / (1.f + .12f * (x*x + z*z));
            marker({x * .55f, -1.35f, z * .55f}, .013f, {light, light * .27f, light * .035f});
        }
    }
    marker({-.35f, .12f, -1.6f}, .06f, {.06f, .46f, .9f});
    marker({.38f, .12f, -2.3f}, .09f, {.94f, .28f, .04f});
    for (std::size_t i = 0; i < hands_.size(); ++i) {
        const auto& hand = hands_[i];
        if (!hand.aim) continue;
        const auto direction = rotate(hand.aim->orientation, {0, 0, -1});
        const std::array<float, 3> tint = hand.pressed ? std::array{1.f, 1.f, 1.f}
            : i == 0 ? std::array{.08f, .8f, .35f} : std::array{.68f, .18f, .95f};
#ifdef REPRISE_FRAME_GAME_BRIDGE
        if (ring_) continue; // The room draws continuous compositor-layer beams.
#endif
        for (int dot = 1; dot <= 14; ++dot)
            marker(hand.aim->position + direction * (dot * .09f), hand.pressed ? .007f : .004f, tint);
    }
    vkCmdEndRenderPass(commands_);
}

void Probe::frame() {
    using Clock = std::chrono::steady_clock;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState info{XR_TYPE_FRAME_STATE};
    check(xrWaitFrame(session_, &wait, &info), "xrWaitFrame");
    ++waited_;
    const auto start = Clock::now();
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    const auto began = xrBeginFrame(session_, &begin);
    check(began, "xrBeginFrame");
    const auto after_begin=Clock::now();
    input(info.predictedDisplayTime);

    std::array<XrView, 2> views{};
    for (auto& view : views) view.type = XR_TYPE_VIEW;
    XrViewState located{XR_TYPE_VIEW_STATE};
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = stereo;
    locate.displayTime = info.predictedDisplayTime;
    locate.space = space_;
    uint32_t count{};
    if (info.shouldRender && began != XR_FRAME_DISCARDED)
        check(xrLocateViews(session_, &locate, &located, views.size(), &count, views.data()), "xrLocateViews");
    constexpr auto valid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    const bool render = count == views.size() && (located.viewStateFlags & valid) == valid;

    const auto after_locate=Clock::now();
    std::array<XrCompositionLayerProjectionView, 2> layer_views{};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    if (render) {
        check(vkResetCommandBuffer(commands_, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        command.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(commands_, &command), "vkBeginCommandBuffer");
        if(room_queries_) {
            vkCmdResetQueryPool(commands_,room_queries_,0,2);
            vkCmdWriteTimestamp(commands_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,room_queries_,0);
        }
        double ownership_ms=0;
        for (std::size_t i = 0; i < eyes_.size(); ++i) {
            auto& eye = eyes_[i];
            const auto ownership_start=Clock::now();
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            uint32_t image{};
            check(xrAcquireSwapchainImage(eye.chain, &acquire, &image), "xrAcquireSwapchainImage");
            XrSwapchainImageWaitInfo ready{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            ready.timeout = XR_INFINITE_DURATION;
            check(xrWaitSwapchainImage(eye.chain, &ready), "xrWaitSwapchainImage");
            ownership_ms+=std::chrono::duration<double,std::milli>(Clock::now()-ownership_start).count();
            draw_eye(eye, image, views[i]);
            auto& out = layer_views[i];
            out.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            out.pose = views[i].pose;
            out.fov = views[i].fov;
            out.subImage.swapchain = eye.chain;
            out.subImage.imageRect.extent = {int32_t(eye.size.width), int32_t(eye.size.height)};
        }
        if(room_queries_) vkCmdWriteTimestamp(commands_,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,room_queries_,1);
        check(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");
        room_ownership_times_.push_back(ownership_ms);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands_;
        check(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit");
        // Keep this submission in flight while the CPU prepares game/UI
        // content. Independent content commands use the same ordered queue.
        // The fence and XR release happen below, before xrEndFrame.
        layer.space = space_;
        layer.viewCount = layer_views.size();
        layer.views = layer_views.data();
    }

    const auto after_room=Clock::now();
    std::vector<const XrCompositionLayerBaseHeader*> layers;
    if (render) layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer));
#ifdef REPRISE_FRAME_GAME_BRIDGE
    if (render && ring_) {
        if (recenter_pending_) { ring_->recenter(); std::printf("Room recentered\n"); }
        XrPosef head = views[0].pose;
        head.position = (views[0].pose.position + views[1].pose.position) * .5f;
        const unsigned reserved = 1 + (bridge_ ? 1 : 0) + 4; // two beams + two hit dots
        ring_->update(info.predictedDisplayTime/1e9, head, {hands_[0].aim, hands_[1].aim},
                      {hands_[0].clicked, hands_[1].clicked}, reduced_motion_,
                      max_layers_ > reserved ? max_layers_ - reserved : 0,
                      state_ == XR_SESSION_STATE_FOCUSED && (hands_[0].aim || hands_[1].aim), back_pressed_, scroll_value_,
                      game_controls_, {hands_[0].pressed,hands_[1].pressed}, {views[0],views[1]});
        ring_->append_pointers(head, {hands_[0].aim,hands_[1].aim},
                               {hands_[0].pressed,hands_[1].pressed});
        for (const auto& cylinder : ring_->cylinders())
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cylinder));
        for (const auto& card : ring_->layers())
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&card));
    }
    if(render && stereo_) {
        stereo_->update(views[0],views[1],recenter_pending_);
        for(const auto& eye:stereo_->layers()) layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&eye));
    }
    if(!render && ring_) ring_->suspend(info.predictedDisplayTime/1e9);
    if (!render && !benchmark_ && bridge_) bridge_->pause();
    if ((render || benchmark_) && bridge_) {
        bridge_->update(info.predictedDisplayTime, benchmark_ || state_ == XR_SESSION_STATE_FOCUSED);
        if (auto* screen = bridge_->layer(); render && screen) layers.push_back(screen);
    }
#endif
    if(render) {
        const auto fence_start=Clock::now();
        check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        check(vkResetFences(device_, 1, &fence_), "vkResetFences");
        room_fence_times_.push_back(std::chrono::duration<double,std::milli>(Clock::now()-fence_start).count());
        if(room_queries_) {
            uint64_t timestamps[2]{};
            check(vkGetQueryPoolResults(device_,room_queries_,0,2,sizeof(timestamps),timestamps,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"room GPU timestamps");
            const uint64_t mask=timestamp_bits_==64 ? UINT64_MAX : (uint64_t{1}<<timestamp_bits_)-1;
            room_gpu_times_.push_back(((timestamps[1]-timestamps[0])&mask)*timestamp_period_/1e6);
        }
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        for (auto& eye : eyes_)
            check(xrReleaseSwapchainImage(eye.chain, &release), "xrReleaseSwapchainImage");
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = info.predictedDisplayTime;
    end.environmentBlendMode = blend_;
    end.layerCount = static_cast<uint32_t>(layers.size());
    end.layers = layers.data();
    const auto before_end = Clock::now();
    check(xrEndFrame(session_, &end), "xrEndFrame");
    const auto finished = Clock::now();
    if (render) {
        const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        begin_times_.push_back(ms(start,after_begin));
        locate_times_.push_back(ms(after_begin,after_locate));
        room_times_.push_back(ms(after_locate,after_room));
        content_times_.push_back(ms(after_room,before_end));
        render_times_.push_back(std::chrono::duration<double, std::milli>(before_end-start).count());
        end_times_.push_back(std::chrono::duration<double, std::milli>(finished-before_end).count());
    } else ++skipped_;
    if (waited_ % 600 == 0) {
        const auto report = [](const char* label, std::vector<double>& samples) {
            if (samples.empty()) return;
            double sum = 0;
            for (auto v : samples) sum += v;
            std::sort(samples.begin(), samples.end());
            std::printf("Frame %s n=%zu mean=%.3f p95=%.3f max=%.3f ms\n", label,
                samples.size(), sum/samples.size(), samples[(samples.size()-1)*95/100], samples.back());
            samples.clear();
        };
        report("render-and-fences", render_times_);
        report("xrEndFrame", end_times_);
        report("xrBeginFrame", begin_times_);
        report("input-locate", locate_times_);
        report("room-record-submit", room_times_);
        report("content", content_times_);
        report("room-GPU", room_gpu_times_);
        report("room-image-ownership", room_ownership_times_);
        report("room-final-fence-wait", room_fence_times_);
        std::printf("Frame waits=%llu skipped=%llu state=%d\n", (unsigned long long)waited_,
                    (unsigned long long)skipped_, state_);
    }
    if (render && (++submitted_ == 1 || submitted_ % 600 == 0)) {
        const double wall_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        std::printf("Stereo frame %llu, predicted period %.2f ms, submit wall time %.2f ms\n",
                    static_cast<unsigned long long>(submitted_), info.predictedDisplayPeriod / 1e6, wall_ms);
    }
}

void Probe::run(int seconds, const char* content, bool room, bool benchmark, const char* catalog, bool reduced, const char* data, const char* stereo) {
    benchmark_ = benchmark;
    reduced_motion_ = reduced;
    if (benchmark_) std::printf("DIAGNOSTIC: run game bridge while unfocused; timings may exclude visible scene/compositor load\n");
    open_xr();
    open_vulkan();
    open_session();
    open_actions();
    open_targets();
    if (room) {
        floor_ = std::make_unique<Floor>();
        const auto executable = std::filesystem::read_symlink("/proc/self/exe");
        floor_->open(device_, pass_, executable.parent_path()/"shaders");
        std::printf("Room floor enabled: charcoal tiles, amber seams, radial fade\n");
    }
#ifdef REPRISE_FRAME_GAME_BRIDGE
    if(stereo) {
        if(catalog || content) throw std::runtime_error("Frozen stereo study cannot run with catalog/game bridge");
        if(max_layers_<3) throw std::runtime_error("Frozen stereo study needs three composition layers");
        stereo_=std::make_unique<StereoStudy>();
        stereo_->open(session_,space_,physical_,device_,queue_,queue_family_,stereo);
    }
    if (catalog) {
        ring_ = std::make_unique<Ring>();
        ring_->open(session_, space_, physical_, device_, queue_, queue_family_, catalog, data, cylinder_supported_);
    }
    if (content) {
        bridge_ = std::make_unique<GameBridge>();
        bridge_->open(session_, space_, physical_, device_, queue_, queue_family_, content);
    }
#else
    if (content || catalog || stereo) throw std::runtime_error("Build with REPRISE_FRAME_GAME_BRIDGE for SDL presentation");
#endif
    const auto started = std::chrono::steady_clock::now();
    bool exit_requested = false;
    while (!exiting_) {
        events();
        if (exiting_) break;
        if (!exit_requested && (stop_requested || (seconds > 0 &&
            std::chrono::steady_clock::now() - started >= std::chrono::seconds(seconds)))) {
            if (!running_) break;
            check(xrRequestExitSession(session_), "xrRequestExitSession");
            std::printf("Orderly exit requested\n");
            exit_requested = true;
        }
        if (running_) frame();
        else std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("XR exit after %llu stereo frames\n", static_cast<unsigned long long>(submitted_));
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        int seconds = 0;
        const char* content = nullptr;
        bool room = false;
        bool benchmark = false;
        bool reduced = false;
        const char* catalog = nullptr;
        const char* data = nullptr;
        const char* smoke = nullptr;
        const char* stereo = nullptr;
        bool smoke_save=false;
        std::vector<std::filesystem::path> imports;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
                seconds = std::stoi(argv[++i]);
            else if (std::strcmp(argv[i], "--content") == 0 && i + 1 < argc)
                content = argv[++i];
            else if (std::strcmp(argv[i], "--room") == 0) room = true;
            else if (std::strcmp(argv[i], "--benchmark") == 0) benchmark = true;
            else if (std::strcmp(argv[i], "--reduced-motion") == 0) reduced = true;
            else if (std::strcmp(argv[i], "--catalog") == 0 && i+1<argc) catalog = argv[++i];
            else if (std::strcmp(argv[i], "--stereo-study") == 0 && i+1<argc) stereo = argv[++i];
            else if (std::strcmp(argv[i], "--smoke-save") == 0) smoke_save=true;
            else if (std::strcmp(argv[i], "--game-smoke") == 0 && i+1<argc) smoke=argv[++i];
            else if (std::strcmp(argv[i], "--import") == 0 && i+1<argc) imports.emplace_back(argv[++i]);
            else if (std::strcmp(argv[i], "--data") == 0 && i+1<argc) data = argv[++i];
            else throw std::runtime_error("Usage: reprise-frame-probe [--seconds N] [--content PATH] [--room] [--catalog PATH] [--data PATH] [--reduced-motion] [--benchmark]");
        }
        if(smoke) {
#ifdef REPRISE_FRAME_LAUNCHER
            if(!data) throw std::runtime_error("--game-smoke requires --data");
            const auto entries=app::catalog();
            const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.id==smoke;});
            if(entry==entries.end()) throw std::runtime_error("Unknown smoke game");
            Gameplay game; game.start(*entry,std::filesystem::path(data)/"games"/smoke,false);
            const auto began=std::chrono::steady_clock::now();
            unsigned frames=0;
            while(game.active() && std::chrono::steady_clock::now()-began<std::chrono::seconds(seconds>0?seconds:15)) {
                const double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
                platform::Controls controls;
                if(now>3 && now<3.2) { controls.fire=true; controls.start=true; }
                game.update(controls,now,true);
                const auto picture=game.draw(now);
                if(++frames%120==0) std::printf("Gameplay smoke %s frames=%u image=%dx%d\n",smoke,frames,picture.width,picture.height);
                std::this_thread::sleep_for(std::chrono::milliseconds(14));
            }
            if(auto error=game.take_message(); !error.empty()) throw std::runtime_error(error);
            platform::Controls pause; pause.menu=true;
            game.update(pause,20,true); game.draw(20);
            if(smoke_save) {
                platform::Controls save; save.click=true; save.mouse_x=220; save.mouse_y=184+2*44+15;
                game.update(save,20.05,true);
                if(!app::save_preview(std::filesystem::path(data)/"games"/smoke,smoke).available)
                    throw std::runtime_error("Smoke did not produce a validated persistent save");
            }
            platform::Controls quit; quit.click=true; quit.mouse_x=220; quit.mouse_y=184+6*44+15;
            game.update(quit,20.1,true);
            if(game.active()) throw std::runtime_error("Smoke pause/return failed");
            if(smoke_save) {
                game.start(*entry,std::filesystem::path(data)/"games"/smoke,true);
                const auto resume_began=std::chrono::steady_clock::now();
                while(game.active() && std::chrono::steady_clock::now()-resume_began<std::chrono::seconds(3)) {
                    game.update({},21+std::chrono::duration<double>(std::chrono::steady_clock::now()-resume_began).count(),true);
                    game.draw(21); std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
                if(auto error=game.take_message(); !error.empty()) throw std::runtime_error(error);
                if(!game.active()) throw std::runtime_error("Smoke Continue lost the game");
                std::puts("Gameplay persistent save and fresh-instance Continue passed");
            }
            std::puts("Gameplay smoke passed: render, tick, audio submission, pause, return");
            return 0;
#else
            throw std::runtime_error("Smoke requires launcher build");
#endif
        }
        if(!imports.empty()) {
#ifdef REPRISE_FRAME_LAUNCHER
            if(!data) throw std::runtime_error("--import requires an explicit --data directory");
            app::Library library(data,app::catalog(),app::Library::StartupValidation::defer_large_imports);
            bool ok=true;
            for(const auto& path:imports) {
                std::printf("IMPORT %s\n",path.c_str());
                std::string last_phase;
                const auto result=library.import(path,std::nullopt,[&](const std::string& phase,double) {
                    if(phase!=last_phase) { std::printf("PROGRESS %s\n",phase.c_str()); last_phase=phase; }
                });
                std::printf("RESULT %s\n",result.message.c_str());
                for(const auto& detail:result.details) std::printf("FILE %s\n",detail.c_str());
                ok=ok && result.ok && !result.waiting;
            }
            for(size_t i=0;i<library.games().size();++i)
                std::printf("CATALOG %s %s\n",library.games()[i].id.c_str(),library.installed(i) ? "installed" : "not installed");
            return ok ? 0 : 1;
#else
            throw std::runtime_error("Imports require the full launcher build");
#endif
        }
        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        Probe probe;
        probe.run(seconds, content, room, benchmark, catalog, reduced, data, stereo);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "reprise-frame-probe: %s\n", error.what());
        return 1;
    }
}
