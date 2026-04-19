#include "VulkanSwapchain.hpp"
#include "VulkanDebug.hpp"
#include <array>
#include <iostream>
#include <algorithm>
#include <limits>

namespace CatEngine::RHI {

// Lightweight wrapper for swapchain images (separate from full VulkanTexture)
class SwapchainTexture : public IRHITexture {
public:
    struct Dimensions {
        uint32_t width;
        uint32_t height;
    };

    SwapchainTexture(VkImage image, VkFormat format, Dimensions dims)
        : m_image(image), m_format(format), m_width(dims.width), m_height(dims.height) {}

    [[nodiscard]] TextureType GetType() const override { return TextureType::Texture2D; }
    [[nodiscard]] TextureFormat GetFormat() const override { return VulkanSwapchain::VkFormatToRHIFormat(m_format); }
    [[nodiscard]] TextureUsage GetUsage() const override { return TextureUsage::RenderTarget; }
    [[nodiscard]] uint32_t GetWidth() const override { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const override { return m_height; }
    [[nodiscard]] uint32_t GetDepth() const override { return 1; }
    [[nodiscard]] uint32_t GetMipLevels() const override { return 1; }
    [[nodiscard]] uint32_t GetArrayLayers() const override { return 1; }
    [[nodiscard]] uint32_t GetSampleCount() const override { return 1; }
    [[nodiscard]] const char* GetDebugName() const override { return "SwapchainImage"; }

    [[nodiscard]] VkImage GetVkImage() const { return m_image; }

private:
    VkImage m_image;
    VkFormat m_format;
    uint32_t m_width;
    uint32_t m_height;
};

VulkanSwapchain::VulkanSwapchain(VulkanDevice* device, VkInstance instance, VkSurfaceKHR surface, const SwapchainDesc& desc)
    : m_device(device)
    , m_instance(instance)
    , m_surface(surface)
    , m_width(desc.width)
    , m_height(desc.height)
    , m_vsync(desc.vsync)
    , m_desiredImageCount(desc.imageCount)
    , m_debugName(desc.debugName != nullptr ? desc.debugName : "")
    , m_windowHandle(desc.windowHandle)
{
    std::cout << "[VulkanSwapchain] Creating swapchain...\n";

    // Create swapchain
    if (!CreateSwapchain(desc.width, desc.height)) {
        throw std::runtime_error("Failed to create Vulkan swapchain");
    }
    std::cout << "[VulkanSwapchain] Swapchain created successfully\n";

    // Create synchronization objects
    if (!CreateSyncObjects()) {
        throw std::runtime_error("Failed to create synchronization objects");
    }
    std::cout << "[VulkanSwapchain] Sync objects created successfully\n";
}

VulkanSwapchain::~VulkanSwapchain() {
    CleanupSyncObjects();
    CleanupSwapchain();

    if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

IRHITexture* VulkanSwapchain::GetImage(uint32_t index) const {
    if (static_cast<size_t>(index) >= m_textureWrappers.size()) {
        return nullptr;
    }
    return m_textureWrappers[static_cast<size_t>(index)];
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
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[VulkanSwapchain] Failed to acquire swapchain image\n";
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

    std::array<VkSwapchainKHR, 1> swapchains = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains.data();
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_device->GetGraphicsQueue(), &presentInfo);

    // Advance to next frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false; // Swapchain needs recreation
    }
    if (result != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] Failed to present swapchain image\n";
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
        std::cerr << "[VulkanSwapchain] Failed to recreate swapchain\n";
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
    imageCount = std::max(imageCount, capabilities.minImageCount);

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

    // Queue family indices - use graphics queue for presentation
    // For simplicity, assume graphics queue supports presentation (most GPUs do)
    const auto& indices = m_device->GetQueueFamilyIndices();
    if (!indices.graphics.has_value()) {
        std::cerr << "[VulkanSwapchain] Graphics queue family not found\n";
        return false;
    }

    // Use exclusive mode with graphics queue (no concurrent access needed for simple case)
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device->GetDevice(), &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] Failed to create swapchain\n";
        return false;
    }

    // Get swapchain images
    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(m_device->GetDevice(), m_swapchain, &actualImageCount, nullptr);
    m_images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(m_device->GetDevice(), m_swapchain, &actualImageCount, m_images.data());

    // Create texture wrappers
    m_textureWrappers.clear();
    for (const auto& image : m_images) {
        m_textureWrappers.push_back(new SwapchainTexture(image, surfaceFormat.format, {.width = extent.width, .height = extent.height}));
    }

    // Store format info
    m_vkFormat = surfaceFormat.format;
    m_format = VkFormatToRHIFormat(surfaceFormat.format);
    m_width = extent.width;
    m_height = extent.height;

    // Create image views for each swapchain image
    std::cout << "[VulkanSwapchain] Creating " << m_images.size() << " image views...\n";
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device->GetDevice(), &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanSwapchain] Failed to create image view " << i << "\n";
            return false;
        }
    }
    std::cout << "[VulkanSwapchain] Image views created\n";

    // Create simple UI render pass (no depth, load existing content)
    std::cout << "[VulkanSwapchain] Creating UI render pass...\n";
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = surfaceFormat.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Preserve existing content
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(m_device->GetDevice(), &renderPassInfo, nullptr, &m_uiRenderPass) != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] Failed to create UI render pass\n";
        return false;
    }
    std::cout << "[VulkanSwapchain] UI render pass created\n";

    // Create the RHI wrapper for the UI render pass (so it can be used for pipeline creation)
    {
        AttachmentDesc rhiAttachment{};
        rhiAttachment.format = m_format;
        rhiAttachment.sampleCount = 1;
        rhiAttachment.loadOp = LoadOp::Load;
        rhiAttachment.storeOp = StoreOp::Store;
        rhiAttachment.stencilLoadOp = LoadOp::DontCare;
        rhiAttachment.stencilStoreOp = StoreOp::DontCare;

        AttachmentReference rhiColorRef{};
        rhiColorRef.attachmentIndex = 0;

        SubpassDesc rhiSubpass{};
        rhiSubpass.bindPoint = PipelineBindPoint::Graphics;
        rhiSubpass.colorAttachments.push_back(rhiColorRef);
        rhiSubpass.depthStencilAttachment = nullptr;

        m_uiRenderPassWrapper = std::make_unique<VulkanRenderPass>(
            m_device, m_uiRenderPass, 
            std::vector<AttachmentDesc>{rhiAttachment},
            std::vector<SubpassDesc>{rhiSubpass},
            "SwapchainUIRenderPass"
        );
        std::cout << "[VulkanSwapchain] UI render pass RHI wrapper created\n";
    }

    // Create framebuffers for each swapchain image
    std::cout << "[VulkanSwapchain] Creating " << m_images.size() << " framebuffers...\n";
    m_framebuffers.resize(m_images.size());
    for (size_t i = 0; i < m_imageViews.size(); i++) {
        VkImageView attachments[] = { m_imageViews[i] };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_uiRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device->GetDevice(), &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanSwapchain] Failed to create framebuffer " << i << "\n";
            return false;
        }
    }
    std::cout << "[VulkanSwapchain] Framebuffers created\n";

    // Set debug name
    if (!m_debugName.empty()) {
        VulkanDebug::SetObjectName(m_device->GetDevice(), m_swapchain, m_debugName.c_str());
    }

    return true;
}

void VulkanSwapchain::CleanupSwapchain() {
    std::cout << "[VulkanSwapchain] Cleaning up swapchain resources...\n";
    
    // Cleanup framebuffers
    for (auto framebuffer : m_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device->GetDevice(), framebuffer, nullptr);
        }
    }
    m_framebuffers.clear();

    // Cleanup UI render pass wrapper first (must be before raw handle cleanup)
    m_uiRenderPassWrapper.reset();

    // Cleanup UI render pass
    if (m_uiRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device->GetDevice(), m_uiRenderPass, nullptr);
        m_uiRenderPass = VK_NULL_HANDLE;
    }

    // Cleanup image views
    for (auto imageView : m_imageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device->GetDevice(), imageView, nullptr);
        }
    }
    m_imageViews.clear();

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
    std::cout << "[VulkanSwapchain] Cleanup complete\n";
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
    }

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

VkExtent2D VulkanSwapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {width, height};

    actualExtent.width = std::clamp(actualExtent.width,
                                   capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                    capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);

    return actualExtent;
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
            std::cerr << "[VulkanSwapchain] Failed to create synchronization objects\n";
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

} // namespace CatEngine::RHI
