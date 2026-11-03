#include "render/vulkan/renderer.hpp"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace vc {
static void check(VkResult r, const char *what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(what) + ": VkResult " + std::to_string(r));
}
#define VK_OK(call) check((call), #call)
struct Buffer {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkDeviceSize size{};
};
struct Image {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
};
struct Renderer::Impl {
    RenderConfig config;
    GLFWwindow *window{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    std::atomic<unsigned> errors{0};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceProperties properties{};
    VkDevice device{};
    uint32_t family{};
    VkQueue queue{};
    uint32_t timestamp_bits = 64;
    VkSurfaceKHR surface{};
    VkSwapchainKHR swapchain{};
    VkExtent2D extent{};
    std::vector<VkImage> swap_images;
    std::vector<bool> presented;
    std::vector<VkSemaphore> finished;
    VkSemaphore acquired{};
    VkFence fence{};
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkQueryPool queries{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    Image color, depth;
    Buffer vertices, indices, readback;
    uint32_t index_count = 0;
    bool submitted = false;
    double gpu = -1;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                VkDebugUtilsMessageTypeFlagsEXT,
                                                const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                void *user) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            ++static_cast<Impl *>(user)->errors;
        std::cerr << "Vulkan validation: " << data->pMessage << '\n';
        return VK_FALSE;
    }
    explicit Impl(RenderConfig c) : config(c) {
        try {
            init();
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~Impl() {
        cleanup();
    }
    void cleanup() {
        if (device)
            vkDeviceWaitIdle(device);
        destroy(vertices);
        destroy(indices);
        destroy(readback);
        destroy(color);
        destroy(depth);
        if (device) {
            if (pipeline)
                vkDestroyPipeline(device, pipeline, nullptr);
            if (layout)
                vkDestroyPipelineLayout(device, layout, nullptr);
            if (queries)
                vkDestroyQueryPool(device, queries, nullptr);
            if (fence)
                vkDestroyFence(device, fence, nullptr);
            if (acquired)
                vkDestroySemaphore(device, acquired, nullptr);
            for (auto s : finished)
                vkDestroySemaphore(device, s, nullptr);
            if (pool)
                vkDestroyCommandPool(device, pool, nullptr);
            if (swapchain)
                vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger) {
            auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (fn)
                fn(instance, messenger, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
        if (window) {
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    }
    uint32_t memory_type(uint32_t mask, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties p;
        vkGetPhysicalDeviceMemoryProperties(physical, &p);
        for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
            if ((mask & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        throw std::runtime_error("required Vulkan memory type unavailable");
    }
    Buffer buffer(VkDeviceSize bytes, VkBufferUsageFlags usage) {
        Buffer b{};
        b.size = std::max<VkDeviceSize>(bytes, 4);
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = b.size;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_OK(vkCreateBuffer(device, &ci, nullptr, &b.buffer));
        VkMemoryRequirements r;
        vkGetBufferMemoryRequirements(device, b.buffer, &r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = r.size;
        ai.memoryTypeIndex = memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_OK(vkAllocateMemory(device, &ai, nullptr, &b.memory));
        VK_OK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
        return b;
    }
    void destroy(Buffer &b) {
        if (b.buffer)
            vkDestroyBuffer(device, b.buffer, nullptr);
        if (b.memory)
            vkFreeMemory(device, b.memory, nullptr);
        b = {};
    }
    void destroy(Image &i) {
        if (i.view)
            vkDestroyImageView(device, i.view, nullptr);
        if (i.image)
            vkDestroyImage(device, i.image, nullptr);
        if (i.memory)
            vkFreeMemory(device, i.memory, nullptr);
        i = {};
    }
    Image image(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
        Image i{};
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {extent.width, extent.height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = format;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = usage;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        VK_OK(vkCreateImage(device, &ci, nullptr, &i.image));
        VkMemoryRequirements r;
        vkGetImageMemoryRequirements(device, i.image, &r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = r.size;
        ai.memoryTypeIndex = memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_OK(vkAllocateMemory(device, &ai, nullptr, &i.memory));
        VK_OK(vkBindImageMemory(device, i.image, i.memory, 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = i.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {aspect, 0, 1, 0, 1};
        VK_OK(vkCreateImageView(device, &vi, nullptr, &i.view));
        return i;
    }
    void init() {
        if (config.width < 64 || config.width > 7680 || config.height < 64 || config.height > 4320)
            throw std::runtime_error("resolution outside supported test range");
        std::vector<const char *> extensions;
        if (!config.headless) {
            glfwSetErrorCallback(
                [](int code, const char *text) { std::cerr << "GLFW " << code << ": " << text << "\n"; });
            if (!glfwInit())
                throw std::runtime_error("GLFW initialization failed: use --headless without a display");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            window =
                glfwCreateWindow(config.width, config.height,
                                 "VibeCraft test bed | WASD fly | arrows look | E remove | Q place | F5 save",
                                 nullptr, nullptr);
            if (!window)
                throw std::runtime_error("window creation failed");
            uint32_t count = 0;
            auto ex = glfwGetRequiredInstanceExtensions(&count);
            if (!ex)
                throw std::runtime_error("GLFW Vulkan support missing");
            extensions.assign(ex, ex + count);
        }
        std::vector<const char *> layers;
        if (config.validation) {
            uint32_t count = 0;
            VK_OK(vkEnumerateInstanceLayerProperties(&count, nullptr));
            std::vector<VkLayerProperties> available(count);
            VK_OK(vkEnumerateInstanceLayerProperties(&count, available.data()));
            if (std::none_of(available.begin(), available.end(), [](auto &p) {
                    return std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
                }))
                throw std::runtime_error("validation layer requested but unavailable");
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "VibeCraft Test Bed";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        validation.enabledValidationFeatureCount = 1;
        validation.pEnabledValidationFeatures = &sync;
        if (config.validation)
            ci.pNext = &validation;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = uint32_t(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = uint32_t(layers.size());
        ci.ppEnabledLayerNames = layers.data();
        VK_OK(vkCreateInstance(&ci, nullptr, &instance));
        if (config.validation) {
            VkDebugUtilsMessengerCreateInfoEXT di{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            di.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            di.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            di.pfnUserCallback = debug;
            di.pUserData = this;
            auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            VK_OK(fn(instance, &di, nullptr, &messenger));
        }
        if (window)
            VK_OK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
        uint32_t count = 0;
        VK_OK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        VK_OK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(candidate, &props);
            if (props.apiVersion < VK_API_VERSION_1_3)
                continue;
            VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            f2.pNext = &features;
            vkGetPhysicalDeviceFeatures2(candidate, &f2);
            if (!features.dynamicRendering || !features.synchronization2)
                continue;
            uint32_t n = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> queues(n);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, queues.data());
            for (uint32_t f = 0; f < n; ++f) {
                VkBool32 present = true;
                if (surface)
                    VK_OK(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, f, surface, &present));
                if ((queues[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present &&
                    queues[f].timestampValidBits) {
                    physical = candidate;
                    family = f;
                    properties = props;
                    timestamp_bits = queues[f].timestampValidBits;
                    break;
                }
            }
            if (physical)
                break;
        }
        if (!physical)
            throw std::runtime_error(
                "Vulkan 1.3 + dynamic rendering/synchronization2/timestamps device required");
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features.dynamicRendering = true;
        features.synchronization2 = true;
        const char *swap_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.pNext = &features;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.enabledExtensionCount = surface ? 1 : 0;
        di.ppEnabledExtensionNames = &swap_extension;
        VK_OK(vkCreateDevice(physical, &di, nullptr, &device));
        vkGetDeviceQueue(device, family, 0, &queue);
        extent = {uint32_t(config.width), uint32_t(config.height)};
        if (surface) {
            VkSurfaceCapabilitiesKHR caps;
            VK_OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
            if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
                throw std::runtime_error("surface transfer destination unsupported");
            uint32_t n = 0;
            VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr));
            std::vector<VkSurfaceFormatKHR> formats(n);
            VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats.data()));
            if (formats.empty())
                throw std::runtime_error("no surface formats");
            auto format = formats[0];
            for (auto f : formats)
                if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
                    format = f;
                    break;
                }
            if (caps.currentExtent.width != UINT32_MAX)
                extent = caps.currentExtent;
            else {
                extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
                extent.height =
                    std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
            }
            VkFormatProperties destination;
            vkGetPhysicalDeviceFormatProperties(physical, format.format, &destination);
            if (!(destination.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
                throw std::runtime_error("surface blit unsupported");
            VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
            si.surface = surface;
            si.minImageCount = caps.minImageCount + 1;
            if (caps.maxImageCount)
                si.minImageCount = std::min(si.minImageCount, caps.maxImageCount);
            si.imageFormat = format.format;
            si.imageColorSpace = format.colorSpace;
            si.imageExtent = extent;
            si.imageArrayLayers = 1;
            si.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            si.preTransform = caps.currentTransform;
            si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            if (!(caps.supportedCompositeAlpha & si.compositeAlpha))
                si.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
                    caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
            si.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            si.clipped = true;
            VK_OK(vkCreateSwapchainKHR(device, &si, nullptr, &swapchain));
            VK_OK(vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr));
            swap_images.resize(n);
            VK_OK(vkGetSwapchainImagesKHR(device, swapchain, &n, swap_images.data()));
            presented.resize(n, false);
            finished.resize(n);
            for (auto &s : finished) {
                VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                VK_OK(vkCreateSemaphore(device, &sc, nullptr, &s));
            }
            VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_OK(vkCreateSemaphore(device, &sc, nullptr, &acquired));
        }
        for (auto [format, needed] :
             {std::pair{VK_FORMAT_R8G8B8A8_UNORM,
                        VkFormatFeatureFlags(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                             VK_FORMAT_FEATURE_BLIT_SRC_BIT)},
              std::pair{VK_FORMAT_D32_SFLOAT,
                        VkFormatFeatureFlags(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)}}) {
            VkFormatProperties support;
            vkGetPhysicalDeviceFormatProperties(physical, format, &support);
            if ((support.optimalTilingFeatures & needed) != needed)
                throw std::runtime_error("required attachment format unsupported");
        }
        color = image(VK_FORMAT_R8G8B8A8_UNORM,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT);
        depth = image(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      VK_IMAGE_ASPECT_DEPTH_BIT);
        readback = buffer(VkDeviceSize(extent.width) * extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = family;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_OK(vkCreateCommandPool(device, &pi, nullptr, &pool));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_OK(vkAllocateCommandBuffers(device, &ai, &command));
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_OK(vkCreateFence(device, &fi, nullptr, &fence));
        VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        q.queryType = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = 2;
        VK_OK(vkCreateQueryPool(device, &q, nullptr, &queries));
        create_pipeline();
    }
    VkShaderModule shader(const char *name) {
        std::ifstream f(std::string(VC_SHADER_DIR) + "/terrain." + name + ".spv",
                        std::ios::binary | std::ios::ate);
        if (!f)
            throw std::runtime_error("shader missing");
        auto bytes = f.tellg();
        std::vector<uint32_t> code(size_t(bytes) / 4);
        f.seekg(0);
        f.read(reinterpret_cast<char *>(code.data()), bytes);
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = size_t(bytes);
        ci.pCode = code.data();
        VkShaderModule module;
        VK_OK(vkCreateShaderModule(device, &ci, nullptr, &module));
        return module;
    }
    void create_pipeline() {
        auto vs = shader("vert"), fs = shader("frag");
        VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                     {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.pViewports = &viewport;
        vp.scissorCount = 1;
        vp.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multi{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = true;
        ds.depthWriteEnable = true;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = 1;
        bs.pAttachments = &ba;
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 80};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &push;
        VK_OK(vkCreatePipelineLayout(device, &li, nullptr, &layout));
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.pNext = &rendering;
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &multi;
        pi.pDepthStencilState = &ds;
        pi.pColorBlendState = &bs;
        pi.layout = layout;
        auto result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline);
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
        check(result, "create terrain pipeline");
    }
    void barrier(VkImage img, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 src,
                 VkAccessFlags2 sa, VkPipelineStageFlags2 dst, VkAccessFlags2 da,
                 VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = src;
        b.srcAccessMask = sa;
        b.dstStageMask = dst;
        b.dstAccessMask = da;
        b.oldLayout = old_layout;
        b.newLayout = new_layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {aspect, 0, 1, 0, 1};
        VkDependencyInfo d{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        d.imageMemoryBarrierCount = 1;
        d.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(command, &d);
    }
    void wait() {
        VK_OK(vkWaitForFences(device, 1, &fence, true, UINT64_MAX));
        if (submitted) {
            uint64_t values[2];
            VK_OK(vkGetQueryPoolResults(device, queries, 0, 2, sizeof(values), values, sizeof(uint64_t),
                                        VK_QUERY_RESULT_64_BIT));
            gpu = double((values[1] - values[0]) &
                         (timestamp_bits == 64 ? UINT64_MAX : ((uint64_t(1) << timestamp_bits) - 1))) *
                  properties.limits.timestampPeriod / 1e6;
        }
    }
    void upload(const Mesh &m) {
        wait();
        destroy(vertices);
        destroy(indices);
        vertices = buffer(m.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        indices = buffer(m.indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        void *data;
        VK_OK(vkMapMemory(device, vertices.memory, 0, vertices.size, 0, &data));
        std::memcpy(data, m.vertices.data(), m.vertices.size() * sizeof(Vertex));
        vkUnmapMemory(device, vertices.memory);
        VK_OK(vkMapMemory(device, indices.memory, 0, indices.size, 0, &data));
        std::memcpy(data, m.indices.data(), m.indices.size() * 4);
        vkUnmapMemory(device, indices.memory);
        index_count = uint32_t(m.indices.size());
    }
    bool draw(const Camera &camera) {
        wait();
        uint32_t index = 0;
        if (swapchain) {
            auto r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
            if (r == VK_ERROR_OUT_OF_DATE_KHR)
                return false;
            if (r != VK_SUBOPTIMAL_KHR)
                check(r, "acquire image");
        }
        VK_OK(vkResetFences(device, 1, &fence));
        VK_OK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_OK(vkBeginCommandBuffer(command, &bi));
        vkCmdResetQueryPool(command, queries, 0, 2);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queries, 0);
        barrier(color.image, submitted ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        barrier(depth.image, submitted ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT);
        VkRenderingAttachmentInfo ca{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ca.imageView = color.view;
        ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ca.clearValue.color = {{.52f, .70f, .87f, 1.f}};
        VkRenderingAttachmentInfo da{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        da.imageView = depth.view;
        da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        da.clearValue.depthStencil = {1, 0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, extent};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &ca;
        ri.pDepthAttachment = &da;
        vkCmdBeginRendering(command, &ri);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        struct Push {
            float matrix[16];
            float eye[4];
        } push{};
        float cy = std::cos(camera.yaw), sy = std::sin(camera.yaw), cp = std::cos(camera.pitch),
              sp = std::sin(camera.pitch);
        float forward[3] = {sy * cp, sp, cy * cp}, right[3] = {-cy, 0, sy}, up[3] = {-sy * sp, cp, -cy * sp};
        float view[16] = {};
        for (int a = 0; a < 3; ++a) {
            view[a * 4] = right[a];
            view[a * 4 + 1] = up[a];
            view[a * 4 + 2] = -forward[a];
        }
        view[15] = 1;
        double eye[3] = {camera.x, camera.y, camera.z};
        for (int a = 0; a < 3; ++a) {
            view[12] -= right[a] * float(eye[a]);
            view[13] -= up[a] * float(eye[a]);
            view[14] += forward[a] * float(eye[a]);
        }
        float projection[16] = {};
        float f = 1 / std::tan(.6f);
        projection[0] = f / (float(extent.width) / extent.height);
        projection[5] = -f;
        projection[10] = 50000.f / (.1f - 50000.f);
        projection[11] = -1;
        projection[14] = (.1f * 50000.f) / (.1f - 50000.f);
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                for (int k = 0; k < 4; ++k)
                    push.matrix[col * 4 + row] += projection[k * 4 + row] * view[col * 4 + k];
        push.eye[0] = float(camera.x);
        push.eye[1] = float(camera.y);
        push.eye[2] = float(camera.z);
        vkCmdPushConstants(command, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        if (index_count) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &offset);
            vkCmdBindIndexBuffer(command, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(command, index_count, 1, 0, 0, 0);
        }
        vkCmdEndRendering(command);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, queries, 1);
        barrier(color.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        if (swapchain) {
            barrier(swap_images[index],
                    presented[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstSubresource = blit.srcSubresource;
            blit.srcOffsets[1] = {int(extent.width), int(extent.height), 1};
            blit.dstOffsets[1] = blit.srcOffsets[1];
            vkCmdBlitImage(command, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap_images[index],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
            barrier(swap_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_NONE, 0);
            presented[index] = true;
        }
        VK_OK(vkEndCommandBuffer(command));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        if (swapchain) {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &acquired;
            si.pWaitDstStageMask = &stage;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &finished[index];
        }
        si.commandBufferCount = 1;
        si.pCommandBuffers = &command;
        VK_OK(vkQueueSubmit(queue, 1, &si, fence));
        submitted = true;
        if (swapchain) {
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &finished[index];
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain;
            pi.pImageIndices = &index;
            auto r = vkQueuePresentKHR(queue, &pi);
            if (r == VK_ERROR_OUT_OF_DATE_KHR)
                return false;
            if (r != VK_SUBOPTIMAL_KHR)
                check(r, "present image");
        }
        return true;
    }
    void screenshot(const std::string &path) {
        wait();
        if (!submitted)
            throw std::runtime_error("no frame to capture");
        VK_OK(vkResetFences(device, 1, &fence));
        VK_OK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_OK(vkBeginCommandBuffer(command, &begin));
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(command, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                               &copy);
        VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &host;
        vkCmdPipelineBarrier2(command, &dep);
        VK_OK(vkEndCommandBuffer(command));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        VK_OK(vkQueueSubmit(queue, 1, &submit, fence));
        VK_OK(vkWaitForFences(device, 1, &fence, true, UINT64_MAX));
        if (!std::filesystem::path(path).parent_path().empty())
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
        void *data;
        VK_OK(vkMapMemory(device, readback.memory, 0, readback.size, 0, &data));
        auto *p = static_cast<uint8_t *>(data);
        for (size_t i = 0; i < size_t(extent.width) * extent.height; ++i)
            f.write(reinterpret_cast<char *>(p + i * 4), 3);
        vkUnmapMemory(device, readback.memory);
        if (!f)
            throw std::runtime_error("screenshot write failed");
    }
};
Renderer::Renderer(RenderConfig c) : impl_(std::make_unique<Impl>(c)) {}
Renderer::~Renderer() = default;
void Renderer::upload(const Mesh &m) {
    impl_->upload(m);
}
bool Renderer::draw(const Camera &c) {
    return impl_->draw(c);
}
void Renderer::screenshot(const std::string &p) {
    impl_->screenshot(p);
}
GLFWwindow *Renderer::window() const {
    return impl_->window;
}
double Renderer::gpu_ms() const {
    return impl_->gpu;
}
unsigned Renderer::validation_errors() const {
    return impl_->errors;
}
std::string Renderer::device_name() const {
    return impl_->properties.deviceName;
}
} // namespace vc
