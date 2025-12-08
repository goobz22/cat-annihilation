#pragma once

#include "../RHISwapchain.hpp"
#include "VulkanDevice.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace CatEngine::RHI::Vulkan {

// Forward declarations
class VulkanTexture;

/**
 * Vulkan swapchain implementation
 * Manages presentation of rendered images to the screen
 */
class VulkanSwapchain : public IRHISwapchain {
public:
    VulkanSwapchain(VulkanDevice* device, VkSurfaceKHR surface, const SwapchainDesc& desc);
    ~VulkanSwapchain() override;

    // Disable copy
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // ========================================================================
    // IRHISwapchain Interface
    // ========================================================================

    uint32_t GetWidth() const override { return m_width; }
    uint32_t GetHeight() const override { return m_height; }
    TextureFormat GetFormat() const override { return m_format; }
    uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_images.size()); }
    IRHITexture* GetImage(uint32_t index) const override;

    uint32_t AcquireNextImage(uint64_t timeout = UINT64_MAX) override;
    bool Present() override;
    bool Resize(uint32_t width, uint32_t height) override;

    bool IsVSyncEnabled() const override { return m_vsync; }
    void SetVSync(bool enabled) override;

    const char* GetDebugName() const override { return m_debugName.c_str(); }

    // ========================================================================
    // Vulkan-Specific
    // ========================================================================

    VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
    VkSurfaceKHR GetSurface() const { return m_surface; }
    uint32_t GetCurrentImageIndex() const { return m_currentImageIndex; }

    VkSemaphore GetImageAvailableSemaphore() const { return m_imageAvailableSemaphores[m_currentFrame]; }
    VkSemaphore GetRenderFinishedSemaphore() const { return m_renderFinishedSemaphores[m_currentFrame]; }
    VkFence GetInFlightFence() const { return m_inFlightFences[m_currentFrame]; }

    uint32_t GetCurrentFrameIndex() const { return m_currentFrame; }

private:
    /**
     * Create or recreate swapchain
     */
    bool CreateSwapchain(uint32_t width, uint32_t height);

    /**
     * Cleanup swapchain resources
     */
    void CleanupSwapchain();

    /**
     * Choose surface format
     */
    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

    /**
     * Choose present mode
     */
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

    /**
     * Choose swap extent
     */
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);

    /**
     * Create synchronization objects
     */
    bool CreateSyncObjects();

    /**
     * Cleanup synchronization objects
     */
    void CleanupSyncObjects();

    /**
     * Convert Vulkan format to RHI format
     */
    static TextureFormat VkFormatToRHIFormat(VkFormat format);

    /**
     * Convert RHI format to Vulkan format
     */
    static VkFormat RHIFormatToVkFormat(TextureFormat format);

    // Device reference
    VulkanDevice* m_device;

    // Vulkan handles
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

    // Swapchain images and views
    std::vector<VkImage> m_images;
    std::vector<VulkanTexture*> m_textureWrappers;

    // Swapchain properties
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    TextureFormat m_format = TextureFormat::Undefined;
    VkFormat m_vkFormat = VK_FORMAT_UNDEFINED;
    bool m_vsync = true;
    uint32_t m_desiredImageCount = 3;

    // Synchronization objects (per frame in flight)
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;

    // Current frame tracking
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Debug
    std::string m_debugName;

    // Window handle (stored for resize)
    void* m_windowHandle = nullptr;
};

} // namespace CatEngine::RHI::Vulkan
