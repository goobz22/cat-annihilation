#include "VulkanSwapchain.hpp"
#include "VulkanDebug.hpp"
#include "../../core/Window.hpp"
#include <iostream>
#include <algorithm>
#include <limits>

namespace CatEngine::RHI::Vulkan {

// Forward declaration of texture wrapper
class VulkanTexture : public IRHITexture {
public:
    VulkanTexture(VkImage image, VkFormat format, uint32_t width, uint32_t height)
        : m_image(image), m_format(format), m_width(width), m_height(height) {}

    TextureType GetType() const override { return TextureType::Texture2D; }
    TextureFormat GetFormat() const override { return VulkanSwapchain::VkFormatToRHIFormat(m_format); }
    TextureUsage GetUsage() const override { return TextureUsage::RenderTarget; }
    uint32_t GetWidth() const override { return m_width; }
    uint32_t GetHeight() const override { return m_height; }
    uint32_t GetDepth() const override { return 1; }
    uint32_t GetMipLevels() const override { return 1; }
    uint32_t GetArrayLayers() const override { return 1; }
    uint32_t GetSampleCount() const override { return 1; }
    const char* GetDebugName() const override { return "SwapchainImage"; }

    VkImage GetVkImage() const { return m_image; }

private:
    VkImage m_image;
    VkFormat m_format;
    uint32_t m_width;
    uint32_t m_height;
};

VulkanSwapchain::VulkanSwapchain(VulkanDevice* device, VkSurfaceKHR surface, const SwapchainDesc& desc)
    : m_device(device)
    , m_surface(surface)
    , m_width(desc.width)
    , m_height(desc.height)
    , m_vsync(desc.vsync)
    , m_desiredImageCount(desc.imageCount)
    , m_windowHandle(desc.windowHandle)
    , m_debugName(desc.debugName ? desc.debugName : "")
{
    // Create swapchain
    if (!CreateSwapchain(desc.width, desc.height)) {
        throw std::runtime_error("Failed to create Vulkan swapchain");
    }

    // Create synchronization objects
    if (!CreateSyncObjects()) {
        throw std::runtime_error("Failed to create synchronization objects");
    }
}

VulkanSwapchain::~VulkanSwapchain() {
    CleanupSyncObjects();
    CleanupSwapchain();

    if (m_surface != VK_NULL_HANDLE) {
        // Need instance handle, but we don't store it - this should be handled by VulkanRHI
        // For now, we'll leave it - proper cleanup would require refactoring
    }
}

IRHITexture* VulkanSwapchain::GetImage(uint32_t index) const {
    if (index >= m_textureWrappers.size()) {
        return nullptr;
    }
    return m_textureWrappers[index];
}

uint32_t VulkanSwapchain::AcquireNextImage(uint64_t timeout) {
    // Wait for previous frame to finish
    vkWaitForFences(m_device->GetDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // Acquire next image
    VkResult result = vkAcquireNextImageKHR(
        m_device->GetDevice(),
        m_swapchain,
        timeout,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE,
        &m_currentImageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain needs recreation
        return UINT32_MAX;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[VulkanSwapchain] Failed to acquire swapchain image" << std::endl;
        return UINT32_MAX;
    }

    // Reset fence only if we're submitting work
    vkResetFences(m_device->GetDevice(), 1, &m_inFlightFences[m_currentFrame]);

    return m_currentImageIndex;
}

bool VulkanSwapchain::Present() {
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];

    VkSwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_device->GetGraphicsQueue(), &presentInfo);

    // Advance to next frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false; // Swapchain needs recreation
    } else if (result != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] Failed to present swapchain image" << std::endl;
        return false;
    }

    return true;
}

bool VulkanSwapchain::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }

    // Wait for device to be idle
    m_device->WaitIdle();

    // Cleanup old swapchain
    CleanupSwapchain();

    // Create new swapchain
    if (!CreateSwapchain(width, height)) {
        std::cerr << "[VulkanSwapchain] Failed to recreate swapchain" << std::endl;
        return false;
    }

    m_width = width;
    m_height = height;

    return true;
}

void VulkanSwapchain::SetVSync(bool enabled) {
    if (m_vsync != enabled) {
        m_vsync = enabled;
        // Recreate swapchain with new present mode
        Resize(m_width, m_height);
    }
}


bool VulkanSwapchain::CreateSwapchain(uint32_t width, uint32_t height) {
    // Query swapchain support
    VkSurfaceCapabilitiesKHR capabilities = m_device->GetSurfaceCapabilities(m_surface);
    std::vector<VkSurfaceFormatKHR> formats = m_device->GetSurfaceFormats(m_surface);
    std::vector<VkPresentModeKHR> presentModes = m_device->GetSurfacePresentModes(m_surface);

    // Choose settings
    VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    VkPresentModeKHR presentMode = ChoosePresentMode(presentModes);
    VkExtent2D extent = ChooseSwapExtent(capabilities, width, height);

    // Determine number of images
    uint32_t imageCount = m_desiredImageCount;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    if (imageCount < capabilities.minImageCount) {
        imageCount = capabilities.minImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Queue family indices
    const auto& indices = m_device->GetQueueFamilyIndices();
    uint32_t queueFamilyIndices[] = {*indices.graphics, *indices.graphics};

    if (indices.graphics != indices.graphics) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device->GetDevice(), &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] Failed to create swapchain" << std::endl;
        return false;
    }

    // Get swapchain images
    uint32_t actualImageCount;
    vkGetSwapchainImagesKHR(m_device->GetDevice(), m_swapchain, &actualImageCount, nullptr);
    m_images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(m_device->GetDevice(), m_swapchain, &actualImageCount, m_images.data());

    // Create texture wrappers
    m_textureWrappers.clear();
    for (size_t i = 0; i < m_images.size(); i++) {
        m_textureWrappers.push_back(new VulkanTexture(m_images[i], surfaceFormat.format, extent.width, extent.height));
    }

    // Store format info
    m_vkFormat = surfaceFormat.format;
    m_format = VkFormatToRHIFormat(surfaceFormat.format);
    m_width = extent.width;
    m_height = extent.height;

    // Set debug name
    if (!m_debugName.empty()) {
        VulkanDebug::SetObjectName(m_device->GetDevice(), m_swapchain, m_debugName.c_str());
    }

    return true;
}

void VulkanSwapchain::CleanupSwapchain() {
    // Cleanup texture wrappers
    for (auto* wrapper : m_textureWrappers) {
        delete wrapper;
    }
    m_textureWrappers.clear();

    // Images are owned by swapchain, no need to destroy them
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device->GetDevice(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    // Prefer BGRA8 SRGB
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    // Fallback to first available
    return availableFormats[0];
}

VkPresentModeKHR VulkanSwapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    if (m_vsync) {
        // VSync enabled - use FIFO (guaranteed to be available)
        return VK_PRESENT_MODE_FIFO_KHR;
    } else {
        // VSync disabled - prefer mailbox, fallback to immediate
        for (const auto& mode : availablePresentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }

        for (const auto& mode : availablePresentModes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }

        // Fallback to FIFO
        return VK_PRESENT_MODE_FIFO_KHR;
    }
}

VkExtent2D VulkanSwapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = {width, height};

        actualExtent.width = std::clamp(actualExtent.width,
                                       capabilities.minImageExtent.width,
                                       capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height,
                                        capabilities.minImageExtent.height,
                                        capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

bool VulkanSwapchain::CreateSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanSwapchain] Failed to create synchronization objects" << std::endl;
            return false;
        }
    }

    return true;
}

void VulkanSwapchain::CleanupSyncObjects() {
    for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++) {
        if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device->GetDevice(), m_imageAvailableSemaphores[i], nullptr);
        }
        if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device->GetDevice(), m_renderFinishedSemaphores[i], nullptr);
        }
        if (m_inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(m_device->GetDevice(), m_inFlightFences[i], nullptr);
        }
    }

    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
}

TextureFormat VulkanSwapchain::VkFormatToRHIFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_SRGB: return TextureFormat::BGRA8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM: return TextureFormat::BGRA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return TextureFormat::RGBA8_SRGB;
        case VK_FORMAT_R8G8B8A8_UNORM: return TextureFormat::RGBA8_UNORM;
        default: return TextureFormat::Undefined;
    }
}

VkFormat VulkanSwapchain::RHIFormatToVkFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case TextureFormat::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

} // namespace CatEngine::RHI::Vulkan
