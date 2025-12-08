#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;

/**
 * Vulkan fence wrapper
 * CPU-GPU synchronization primitive
 */
class VulkanFence {
public:
    VulkanFence(VulkanDevice* device, bool signaled = false);
    ~VulkanFence();

    VulkanFence(const VulkanFence&) = delete;
    VulkanFence& operator=(const VulkanFence&) = delete;

    /**
     * Wait for the fence to be signaled
     * @param timeout Timeout in nanoseconds (UINT64_MAX = infinite)
     * @return true if fence was signaled, false if timeout occurred
     */
    bool Wait(uint64_t timeout = UINT64_MAX);

    /**
     * Reset the fence to unsignaled state
     */
    void Reset();

    /**
     * Check if fence is signaled (non-blocking)
     */
    bool IsSignaled();

    VkFence GetHandle() const { return m_fence; }

private:
    VulkanDevice* m_device;
    VkFence m_fence;
};

/**
 * Vulkan semaphore wrapper
 * GPU-GPU synchronization primitive (binary semaphore)
 */
class VulkanSemaphore {
public:
    VulkanSemaphore(VulkanDevice* device);
    ~VulkanSemaphore();

    VulkanSemaphore(const VulkanSemaphore&) = delete;
    VulkanSemaphore& operator=(const VulkanSemaphore&) = delete;

    VkSemaphore GetHandle() const { return m_semaphore; }

private:
    VulkanDevice* m_device;
    VkSemaphore m_semaphore;
};

/**
 * Vulkan timeline semaphore wrapper
 * GPU-GPU synchronization with timeline values (Vulkan 1.2+)
 */
class VulkanTimelineSemaphore {
public:
    VulkanTimelineSemaphore(VulkanDevice* device, uint64_t initialValue = 0);
    ~VulkanTimelineSemaphore();

    VulkanTimelineSemaphore(const VulkanTimelineSemaphore&) = delete;
    VulkanTimelineSemaphore& operator=(const VulkanTimelineSemaphore&) = delete;

    /**
     * Wait for the timeline semaphore to reach a specific value
     * @param value Value to wait for
     * @param timeout Timeout in nanoseconds (UINT64_MAX = infinite)
     * @return true if value was reached, false if timeout occurred
     */
    bool Wait(uint64_t value, uint64_t timeout = UINT64_MAX);

    /**
     * Signal the timeline semaphore with a specific value
     * @param value Value to signal
     */
    void Signal(uint64_t value);

    /**
     * Get current counter value
     */
    uint64_t GetCounterValue();

    VkSemaphore GetHandle() const { return m_semaphore; }

private:
    VulkanDevice* m_device;
    VkSemaphore m_semaphore;
};

/**
 * Helper functions for synchronization operations
 */
namespace VulkanSyncHelper {

    /**
     * Create a memory barrier
     */
    VkMemoryBarrier CreateMemoryBarrier(
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask
    );

    /**
     * Create a buffer memory barrier
     */
    VkBufferMemoryBarrier CreateBufferMemoryBarrier(
        VkBuffer buffer,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        uint64_t offset = 0,
        uint64_t size = VK_WHOLE_SIZE,
        uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
    );

    /**
     * Create an image memory barrier
     */
    VkImageMemoryBarrier CreateImageMemoryBarrier(
        VkImage image,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask,
        uint32_t baseMipLevel = 0,
        uint32_t levelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t baseArrayLayer = 0,
        uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS,
        uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
    );

    /**
     * Create an image subresource range
     */
    VkImageSubresourceRange CreateImageSubresourceRange(
        VkImageAspectFlags aspectMask,
        uint32_t baseMipLevel = 0,
        uint32_t levelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t baseArrayLayer = 0,
        uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS
    );

    /**
     * Helper to transition image layout
     */
    void TransitionImageLayout(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage,
        uint32_t baseMipLevel = 0,
        uint32_t levelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t baseArrayLayer = 0,
        uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS
    );

    /**
     * Get appropriate pipeline stage flags for image layout
     */
    VkPipelineStageFlags GetPipelineStageForLayout(VkImageLayout layout);

    /**
     * Get appropriate access flags for image layout
     */
    VkAccessFlags GetAccessFlagsForLayout(VkImageLayout layout);

} // namespace VulkanSyncHelper

} // namespace CatEngine::RHI
