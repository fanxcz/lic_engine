#include <lic/renderer/vulkan_viewport.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <stdexcept>
#include <string>
#include <vector>

namespace lic::renderer {
namespace {

constexpr std::uint32_t kWindowWidth = 1440;
constexpr std::uint32_t kWindowHeight = 900;
constexpr int kFramesInFlight = 2;

[[noreturn]] void fail(const char* operation, VkResult result) {
    throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                             std::to_string(static_cast<int>(result)) + ")");
}

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        fail(operation, result);
    }
}

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    [[nodiscard]] bool complete() const noexcept { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanViewport final {
public:
    explicit VulkanViewport(const std::filesystem::path& executablePath)
        : shaderDirectory_(executablePath.parent_path() / "shaders") {}

    int run() {
        try {
            initialize_window();
            initialize_vulkan();
            main_loop();
            cleanup();
            return 0;
        } catch (const std::exception& error) {
            cleanup();
            report_startup_error(error.what());
            return 1;
        }
    }

private:
    std::filesystem::path shaderDirectory_;

    void report_startup_error(std::string_view details) const noexcept {
        std::filesystem::path logPath = shaderDirectory_.parent_path() / "lic-engine-startup.log";
        const auto writeLog = [details](const std::filesystem::path& path) noexcept {
            try {
                std::ofstream log(path, std::ios::binary | std::ios::trunc);
                if (!log) return false;
                log << "LIC Engine failed to start the Vulkan viewport.\n"
                    << "Details: " << details << "\n\n"
                    << "If the error mentions a compiled shader or runtime DLL, download and extract the complete "
                       "Windows ZIP package. Keep lic_editor.exe, vulkan-1.dll, and the shaders folder together.\n";
                log.flush();
                return static_cast<bool>(log);
            } catch (...) { return false; }
        };

        bool logged = writeLog(logPath);
        if (!logged) {
            try {
                logPath = std::filesystem::temp_directory_path() / "lic-engine-startup.log";
                logged = writeLog(logPath);
            } catch (...) { logged = false; }
        }
        std::cerr << "[LIC][Vulkan] " << details << '\n';
        if (logged) std::cerr << "[LIC][Vulkan] Startup details were saved to lic-engine-startup.log.\n";
#ifdef _WIN32
        std::wstring message =
            L"LIC Engine could not start the 3D viewport.\n\n"
            L"Download and extract the complete Windows ZIP package. Do not move lic_editor.exe by itself.\n\n";
        if (logged) {
            message += L"Error details were saved to:\n";
            try { message += logPath.wstring(); }
            catch (...) { message += L"lic-engine-startup.log"; }
        } else {
            message += L"The error log could not be written. Run run-lic-engine.bat to keep console output visible.";
        }
        MessageBoxW(nullptr, message.c_str(), L"LIC Engine - startup error",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#endif
    }
    GLFWwindow* window_{};
    VkInstance instance_{};
    VkSurfaceKHR surface_{};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{};
    VkQueue graphicsQueue_{};
    VkQueue presentQueue_{};
    VkSwapchainKHR swapchain_{};
    VkFormat swapchainFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> imageViews_;
    VkRenderPass renderPass_{};
    VkPipelineLayout pipelineLayout_{};
    VkPipeline graphicsPipeline_{};
    VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
    std::vector<VkImage> depthImages_;
    std::vector<VkDeviceMemory> depthMemories_;
    std::vector<VkImageView> depthViews_;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_{};
    std::vector<VkCommandBuffer> commandBuffers_;
    std::array<VkSemaphore, kFramesInFlight> imageAvailable_{};
    std::vector<VkSemaphore> renderFinished_;
    std::array<VkFence, kFramesInFlight> inFlight_{};
    std::vector<VkFence> imageFences_;
    std::size_t frameIndex_{};
    bool glfwInitialized_{};

    void initialize_window() {
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("GLFW initialization failed. Install GLFW and a working desktop display driver.");
        }
        glfwInitialized_ = true;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window_ = glfwCreateWindow(static_cast<int>(kWindowWidth), static_cast<int>(kWindowHeight),
                                   "LIC Engine | Vulkan Viewport", nullptr, nullptr);
        if (window_ == nullptr) {
            throw std::runtime_error("Could not create a window. Check the desktop session and graphics drivers.");
        }
    }

    void initialize_vulkan() {
        create_instance();
        check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "glfwCreateWindowSurface");
        select_physical_device();
        create_device();
        create_swapchain();
        create_image_views();
        create_render_pass();
        create_graphics_pipeline();
        create_depth_resources();
        create_framebuffers();
        create_command_pool();
        create_command_buffers();
        create_sync_objects();
    }

    void create_instance() {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "LIC Engine";
        application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        application.pEngineName = "LIC Engine";
        application.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        application.apiVersion = VK_API_VERSION_1_0;

        std::uint32_t extensionCount{};
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (extensions == nullptr || extensionCount == 0) {
            throw std::runtime_error("GLFW did not provide the Vulkan surface extensions.");
        }

        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &application;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensions;
        check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
    }

    QueueFamilies find_queue_families(VkPhysicalDevice candidate) const {
        QueueFamilies families;
        std::uint32_t count{};
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, properties.data());

        for (std::uint32_t i = 0; i < count; ++i) {
            if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                families.graphics = i;
            }
            VkBool32 supported = VK_FALSE;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &supported),
                  "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (supported == VK_TRUE) {
                families.present = i;
            }
            if (families.complete()) {
                break;
            }
        }
        return families;
    }

    bool has_swapchain_extension(VkPhysicalDevice candidate) const {
        std::uint32_t count{};
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr),
              "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(count);
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()),
              "vkEnumerateDeviceExtensionProperties");
        return std::ranges::any_of(extensions, [](const VkExtensionProperties& extension) {
            return std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        });
    }

    SwapchainSupport query_swapchain_support(VkPhysicalDevice candidate) const {
        SwapchainSupport support;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface_, &support.capabilities),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        std::uint32_t count{};
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &count, nullptr),
              "vkGetPhysicalDeviceSurfaceFormatsKHR");
        support.formats.resize(count);
        if (count > 0) {
            check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &count, support.formats.data()),
                  "vkGetPhysicalDeviceSurfaceFormatsKHR");
        }

        count = 0;
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &count, nullptr),
              "vkGetPhysicalDeviceSurfacePresentModesKHR");
        support.presentModes.resize(count);
        if (count > 0) {
            check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &count, support.presentModes.data()),
                  "vkGetPhysicalDeviceSurfacePresentModesKHR");
        }
        return support;
    }

    void select_physical_device() {
        std::uint32_t count{};
        check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices");
        if (count == 0) {
            throw std::runtime_error("No Vulkan-capable GPU or software device was found.");
        }
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices");

        for (VkPhysicalDevice candidate : devices) {
            const QueueFamilies families = find_queue_families(candidate);
            if (!families.complete() || !has_swapchain_extension(candidate)) {
                continue;
            }
            const SwapchainSupport support = query_swapchain_support(candidate);
            if (!support.formats.empty() && !support.presentModes.empty()) {
                physicalDevice_ = candidate;
                break;
            }
        }
        if (physicalDevice_ == VK_NULL_HANDLE) {
            throw std::runtime_error("No GPU supports the graphics queues and swapchain features required by LIC.");
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        std::cout << "[LIC][Vulkan] GPU: " << properties.deviceName << '\n';
    }

    void create_device() {
        const QueueFamilies families = find_queue_families(physicalDevice_);
        const std::array<std::uint32_t, 2> indices{*families.graphics, *families.present};
        const float priority = 1.0F;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (std::uint32_t family : indices) {
            const bool alreadyAdded = std::ranges::any_of(queueInfos, [family](const VkDeviceQueueCreateInfo& info) {
                return info.queueFamilyIndex == family;
            });
            if (!alreadyAdded) {
                VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
                queueInfo.queueFamilyIndex = family;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &priority;
                queueInfos.push_back(queueInfo);
            }
        }

        constexpr std::array deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");
        vkGetDeviceQueue(device_, *families.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, *families.present, 0, &presentQueue_);
    }

    VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats) const {
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats.front();
    }

    VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes) const {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width{};
        int height{};
        glfwGetFramebufferSize(window_, &width, &height);
        VkExtent2D extent{static_cast<std::uint32_t>(std::max(width, 1)),
                          static_cast<std::uint32_t>(std::max(height, 1))};
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
    }

    void create_swapchain() {
        const SwapchainSupport support = query_swapchain_support(physicalDevice_);
        const VkSurfaceFormatKHR surfaceFormat = choose_surface_format(support.formats);
        const VkPresentModeKHR presentMode = choose_present_mode(support.presentModes);
        const VkExtent2D extent = choose_extent(support.capabilities);

        std::uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        const QueueFamilies families = find_queue_families(physicalDevice_);
        const std::array<std::uint32_t, 2> indices{*families.graphics, *families.present};
        VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (indices[0] != indices[1]) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(indices.size());
            createInfo.pQueueFamilyIndices = indices.data();
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;
        check(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR");

        check(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr), "vkGetSwapchainImagesKHR");
        swapchainImages_.resize(imageCount);
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data()),
              "vkGetSwapchainImagesKHR");
        swapchainFormat_ = surfaceFormat.format;
        swapchainExtent_ = extent;
        imageFences_.assign(imageCount, VK_NULL_HANDLE);
        renderFinished_.resize(imageCount, VK_NULL_HANDLE);
    }

    void create_image_views() {
        imageViews_.resize(swapchainImages_.size());
        for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
            VkImageViewCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image = swapchainImages_[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapchainFormat_;
            createInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                     VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;
            check(vkCreateImageView(device_, &createInfo, nullptr, &imageViews_[i]), "vkCreateImageView");
        }
    }

    void create_render_pass() {
        depthFormat_ = choose_depth_format();
        std::array<VkAttachmentDescription, 2> attachments{};

        VkAttachmentDescription& color = attachments[0];
        color.format = swapchainFormat_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription& depth = attachments[1];
        depth.format = depthFormat_;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depthReference{};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;
        check(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_), "vkCreateRenderPass");
    }

    VkFormat choose_depth_format() const {
        constexpr std::array candidates{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                        VK_FORMAT_D24_UNORM_S8_UINT};
        for (VkFormat format : candidates) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                return format;
            }
        }
        throw std::runtime_error("The selected Vulkan device has no supported depth attachment format.");
    }

    std::uint32_t find_memory_type(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
        for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            const bool allowed = (typeFilter & (1U << i)) != 0;
            const bool matches = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
            if (allowed && matches) {
                return i;
            }
        }
        throw std::runtime_error("No compatible Vulkan memory type was found.");
    }

    void create_depth_resources() {
        depthImages_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
        depthMemories_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
        depthViews_.resize(swapchainImages_.size(), VK_NULL_HANDLE);

        for (std::size_t i = 0; i < depthImages_.size(); ++i) {
            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = depthFormat_;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateImage(device_, &imageInfo, nullptr, &depthImages_[i]), "vkCreateImage(depth)");

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device_, depthImages_[i], &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device_, &allocation, nullptr, &depthMemories_[i]), "vkAllocateMemory(depth)");
            check(vkBindImageMemory(device_, depthImages_[i], depthMemories_[i], 0), "vkBindImageMemory(depth)");

            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = depthImages_[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat_;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            check(vkCreateImageView(device_, &viewInfo, nullptr, &depthViews_[i]), "vkCreateImageView(depth)");
        }
    }

    std::vector<std::uint32_t> read_shader(const std::string& filename) const {
        const std::filesystem::path shaderPath = shaderDirectory_ / filename;
        std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open compiled shader: " + shaderPath.string());
        }
        const auto end = file.tellg();
        const auto byteCount = static_cast<std::streamoff>(end);
        if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
            throw std::runtime_error("Compiled shader has an invalid SPIR-V size: " + shaderPath.string());
        }
        const auto wordCount = static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t);
        std::vector<std::uint32_t> code(wordCount);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byteCount));
        if (!file) {
            throw std::runtime_error("Failed while reading compiled shader: " + shaderPath.string());
        }
        return code;
    }

    VkShaderModule create_shader_module(const std::vector<std::uint32_t>& code) const {
        VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = code.size() * sizeof(std::uint32_t);
        createInfo.pCode = code.data();
        VkShaderModule module{};
        check(vkCreateShaderModule(device_, &createInfo, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    void create_graphics_pipeline() {
        const auto vertexCode = read_shader("viewport.vert.spv");
        const auto fragmentCode = read_shader("viewport.frag.spv");
        const VkShaderModule vertexModule = create_shader_module(vertexCode);
        VkShaderModule fragmentModule = VK_NULL_HANDLE;
        try {
            fragmentModule = create_shader_module(fragmentCode);
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertexModule;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragmentModule;
            stages[1].pName = "main";

            VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0F;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

            VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depthState.depthTestEnable = VK_TRUE;
            depthState.depthWriteEnable = VK_TRUE;
            depthState.depthCompareOp = VK_COMPARE_OP_LESS;

            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments = &blendAttachment;

            constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
            dynamicState.pDynamicStates = dynamicStates.data();

            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_), "vkCreatePipelineLayout");

            VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthState;
            pipelineInfo.pColorBlendState = &colorBlend;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipelineLayout_;
            pipelineInfo.renderPass = renderPass_;
            pipelineInfo.subpass = 0;
            check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_),
                  "vkCreateGraphicsPipelines");
        } catch (...) {
            if (fragmentModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, fragmentModule, nullptr);
            }
            vkDestroyShaderModule(device_, vertexModule, nullptr);
            throw;
        }
        vkDestroyShaderModule(device_, fragmentModule, nullptr);
        vkDestroyShaderModule(device_, vertexModule, nullptr);
    }

    void create_framebuffers() {
        framebuffers_.resize(imageViews_.size());
        for (std::size_t i = 0; i < imageViews_.size(); ++i) {
            const std::array<VkImageView, 2> attachments{imageViews_[i], depthViews_[i]};
            VkFramebufferCreateInfo createInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            createInfo.renderPass = renderPass_;
            createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
            createInfo.pAttachments = attachments.data();
            createInfo.width = swapchainExtent_.width;
            createInfo.height = swapchainExtent_.height;
            createInfo.layers = 1;
            check(vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[i]), "vkCreateFramebuffer");
        }
    }

    void create_command_pool() {
        const QueueFamilies families = find_queue_families(physicalDevice_);
        VkCommandPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = *families.graphics;
        check(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_), "vkCreateCommandPool");
    }

    void create_command_buffers() {
        commandBuffers_.resize(framebuffers_.size());
        VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = commandPool_;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = static_cast<std::uint32_t>(commandBuffers_.size());
        check(vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers_.data()), "vkAllocateCommandBuffers");

        for (std::size_t i = 0; i < commandBuffers_.size(); ++i) {
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            check(vkBeginCommandBuffer(commandBuffers_[i], &beginInfo), "vkBeginCommandBuffer");

            std::array<VkClearValue, 2> clear{};
            clear[0].color = {{0.075F, 0.090F, 0.115F, 1.0F}};
            clear[1].depthStencil = {1.0F, 0};
            VkRenderPassBeginInfo renderInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            renderInfo.renderPass = renderPass_;
            renderInfo.framebuffer = framebuffers_[i];
            renderInfo.renderArea.offset = {0, 0};
            renderInfo.renderArea.extent = swapchainExtent_;
            renderInfo.clearValueCount = static_cast<std::uint32_t>(clear.size());
            renderInfo.pClearValues = clear.data();
            vkCmdBeginRenderPass(commandBuffers_[i], &renderInfo, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport viewport{};
            viewport.width = static_cast<float>(swapchainExtent_.width);
            viewport.height = static_cast<float>(swapchainExtent_.height);
            viewport.minDepth = 0.0F;
            viewport.maxDepth = 1.0F;
            VkRect2D scissor{{0, 0}, swapchainExtent_};
            vkCmdSetViewport(commandBuffers_[i], 0, 1, &viewport);
            vkCmdSetScissor(commandBuffers_[i], 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffers_[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
            vkCmdDraw(commandBuffers_[i], 36, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffers_[i]);
            check(vkEndCommandBuffer(commandBuffers_[i]), "vkEndCommandBuffer");
        }
    }

    void create_sync_objects() {
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < kFramesInFlight; ++i) {
            check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_[static_cast<std::size_t>(i)]),
                  "vkCreateSemaphore(image available)");
            check(vkCreateFence(device_, &fenceInfo, nullptr, &inFlight_[static_cast<std::size_t>(i)]),
                  "vkCreateFence");
        }
        for (VkSemaphore& semaphore : renderFinished_) {
            check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
                  "vkCreateSemaphore(render finished)");
        }
    }

    void recreate_swapchain() {
        int width{};
        int height{};
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &width, &height);
            if (glfwWindowShouldClose(window_) == GLFW_TRUE) {
                return;
            }
        }
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
        cleanup_swapchain();
        create_swapchain();
        create_image_views();
        create_render_pass();
        create_graphics_pipeline();
        create_depth_resources();
        create_framebuffers();
        create_command_buffers();
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkSemaphore& semaphore : renderFinished_) {
            check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
                  "vkCreateSemaphore(render finished)");
        }
    }

    void main_loop() {
        std::cout << "[LIC][Vulkan] Swapchain ready (" << swapchainExtent_.width << 'x'
                  << swapchainExtent_.height << "). Close the window to exit.\n";
        while (glfwWindowShouldClose(window_) == GLFW_FALSE) {
            glfwPollEvents();
            draw_frame();
        }
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }

    void draw_frame() {
        const std::size_t frame = frameIndex_;
        check(vkWaitForFences(device_, 1, &inFlight_[frame], VK_TRUE, UINT64_MAX), "vkWaitForFences");

        std::uint32_t imageIndex{};
        const VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                        imageAvailable_[frame], VK_NULL_HANDLE, &imageIndex);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_swapchain();
            return;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            fail("vkAcquireNextImageKHR", acquire);
        }

        if (imageFences_[imageIndex] != VK_NULL_HANDLE) {
            check(vkWaitForFences(device_, 1, &imageFences_[imageIndex], VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(image)");
        }
        imageFences_[imageIndex] = inFlight_[frame];
        check(vkResetFences(device_, 1, &inFlight_[frame]), "vkResetFences");

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable_[frame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffers_[imageIndex];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished_[imageIndex];
        check(vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_[frame]), "vkQueueSubmit");

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished_[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &imageIndex;
        const VkResult result = vkQueuePresentKHR(presentQueue_, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            acquire == VK_SUBOPTIMAL_KHR) {
            recreate_swapchain();
        } else if (result != VK_SUCCESS) {
            fail("vkQueuePresentKHR", result);
        }
        frameIndex_ = (frameIndex_ + 1U) % static_cast<std::size_t>(kFramesInFlight);
    }

    void cleanup_swapchain() noexcept {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }
        if (commandPool_ != VK_NULL_HANDLE && !commandBuffers_.empty()) {
            vkFreeCommandBuffers(device_, commandPool_, static_cast<std::uint32_t>(commandBuffers_.size()),
                                 commandBuffers_.data());
            commandBuffers_.clear();
        }
        for (VkFramebuffer framebuffer : framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device_, framebuffer, nullptr);
            }
        }
        framebuffers_.clear();
        if (graphicsPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
            graphicsPipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        for (VkImageView view : depthViews_) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, view, nullptr);
            }
        }
        depthViews_.clear();
        for (VkImage image : depthImages_) {
            if (image != VK_NULL_HANDLE) {
                vkDestroyImage(device_, image, nullptr);
            }
        }
        depthImages_.clear();
        for (VkDeviceMemory memory : depthMemories_) {
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, memory, nullptr);
            }
        }
        depthMemories_.clear();
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
        for (VkImageView view : imageViews_) {
            vkDestroyImageView(device_, view, nullptr);
        }
        imageViews_.clear();
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        swapchainImages_.clear();
        imageFences_.clear();
        for (VkSemaphore semaphore : renderFinished_) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
        renderFinished_.clear();
    }

    void cleanup() noexcept {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            cleanup_swapchain();
            for (std::size_t i = 0; i < imageAvailable_.size(); ++i) {
                if (imageAvailable_[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
                }
                if (inFlight_[i] != VK_NULL_HANDLE) {
                    vkDestroyFence(device_, inFlight_[i], nullptr);
                }
            }
            if (commandPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, commandPool_, nullptr);
            }
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
    }
};

} // namespace

int run_vulkan_viewport(const std::filesystem::path& executable_path) {
    return VulkanViewport{executable_path}.run();
}

} // namespace lic::renderer
