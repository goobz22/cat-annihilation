#pragma once

/**
 * Mock Vulkan API - Stub implementations for testing without GPU
 *
 * This mock provides stub implementations of Vulkan functions
 * to allow testing of game logic without GPU hardware.
 */

#ifdef USE_MOCK_GPU

#include <cstdint>

// Mock Vulkan types
typedef uint64_t VkDevice;
typedef uint64_t VkInstance;
typedef uint64_t VkQueue;
typedef uint64_t VkCommandBuffer;
typedef uint64_t VkBuffer;
typedef uint64_t VkImage;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkSwapchainKHR;
typedef uint64_t VkRenderPass;
typedef uint64_t VkFramebuffer;
typedef uint64_t VkPipeline;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkSemaphore;
typedef uint64_t VkFence;

// Mock result codes
enum VkResult {
    VK_SUCCESS = 0,
    VK_ERROR_DEVICE_LOST = -4
};

// Mock Vulkan functions (no-ops)
namespace MockVulkan {
    inline VkResult createDevice() { return VK_SUCCESS; }
    inline VkResult createSwapchain() { return VK_SUCCESS; }
    inline VkResult createRenderPass() { return VK_SUCCESS; }
    inline VkResult createPipeline() { return VK_SUCCESS; }
    inline VkResult allocateCommandBuffer() { return VK_SUCCESS; }
    inline VkResult beginCommandBuffer() { return VK_SUCCESS; }
    inline VkResult endCommandBuffer() { return VK_SUCCESS; }
    inline VkResult queueSubmit() { return VK_SUCCESS; }
    inline VkResult queuePresent() { return VK_SUCCESS; }
    inline void deviceWaitIdle() {}
}

#endif // USE_MOCK_GPU
