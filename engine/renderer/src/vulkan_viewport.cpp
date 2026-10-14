#include <lic/renderer/vulkan_viewport.hpp>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
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
    int run() {
        try {
            initialize_window();
            initialize_vulkan();
            main_loop();
            cleanup();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "[LIC][Vulkan] " << error.what() << '\n';
            cleanup();
            return 1;
        }
    }

private:
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
        VkAttachmentDescription color{};
        color.format = swapchainFormat_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference reference{};
        reference.attachment = 0;
        reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &color;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;
        check(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_), "vkCreateRenderPass");
    }

    void create_framebuffers() {
        framebuffers_.resize(imageViews_.size());
        for (std::size_t i = 0; i < imageViews_.size(); ++i) {
            VkFramebufferCreateInfo createInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            createInfo.renderPass = renderPass_;
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = &imageViews_[i];
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

            VkClearValue clear{};
            clear.color = {{0.075F, 0.090F, 0.115F, 1.0F}};
            VkRenderPassBeginInfo renderInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            renderInfo.renderPass = renderPass_;
            renderInfo.framebuffer = framebuffers_[i];
            renderInfo.renderArea.offset = {0, 0};
            renderInfo.renderArea.extent = swapchainExtent_;
            renderInfo.clearValueCount = 1;
            renderInfo.pClearValues = &clear;
            vkCmdBeginRenderPass(commandBuffers_[i], &renderInfo, VK_SUBPASS_CONTENTS_INLINE);
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
        create_framebuffers();
        create_command_buffers();
        for (VkSemaphore& semaphore : renderFinished_) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
        renderFinished_.assign(swapchainImages_.size(), VK_NULL_HANDLE);
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
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        framebuffers_.clear();
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

int run_vulkan_viewport() {
    return VulkanViewport{}.run();
}

} // namespace lic::renderer
