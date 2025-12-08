// Minimal Vulkan type stubs for compilation checking without Vulkan SDK
// This file provides just enough types to make headers parse without linking
#ifndef VULKAN_STUBS_H
#define VULKAN_STUBS_H

#include <stdint.h>

// Vulkan handle types
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;
typedef void* VkCommandBuffer;
typedef void* VkCommandPool;
typedef void* VkBuffer;
typedef void* VkImage;
typedef void* VkDeviceMemory;
typedef void* VkFence;
typedef void* VkSemaphore;
typedef void* VkEvent;
typedef void* VkQueryPool;
typedef void* VkBufferView;
typedef void* VkImageView;
typedef void* VkShaderModule;
typedef void* VkPipelineCache;
typedef void* VkPipelineLayout;
typedef void* VkRenderPass;
typedef void* VkPipeline;
typedef void* VkDescriptorSetLayout;
typedef void* VkSampler;
typedef void* VkDescriptorPool;
typedef void* VkDescriptorSet;
typedef void* VkFramebuffer;
typedef void* VkSwapchainKHR;
typedef void* VkSurfaceKHR;

// Vulkan result codes
typedef enum VkResult {
    VK_SUCCESS = 0,
    VK_ERROR_OUT_OF_HOST_MEMORY = -1,
    VK_ERROR_OUT_OF_DEVICE_MEMORY = -2,
    VK_ERROR_INITIALIZATION_FAILED = -3,
    VK_ERROR_DEVICE_LOST = -4,
    VK_ERROR_MEMORY_MAP_FAILED = -5
} VkResult;

// Vulkan flags
typedef uint32_t VkFlags;
typedef VkFlags VkBufferUsageFlags;
typedef VkFlags VkMemoryPropertyFlags;
typedef VkFlags VkShaderStageFlags;
typedef VkFlags VkPipelineStageFlags;
typedef VkFlags VkAccessFlags;
typedef VkFlags VkCommandBufferUsageFlags;

// Vulkan enums
typedef enum VkFormat {
    VK_FORMAT_UNDEFINED = 0,
    VK_FORMAT_R8G8B8A8_UNORM = 37,
    VK_FORMAT_D32_SFLOAT = 126
} VkFormat;

typedef enum VkImageLayout {
    VK_IMAGE_LAYOUT_UNDEFINED = 0,
    VK_IMAGE_LAYOUT_GENERAL = 1,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL = 2,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL = 4,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 5,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 6,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR = 1000001002
} VkImageLayout;

// Vulkan structures
typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} VkExtent3D;

typedef struct VkOffset2D {
    int32_t x;
    int32_t y;
} VkOffset2D;

typedef struct VkRect2D {
    VkOffset2D offset;
    VkExtent2D extent;
} VkRect2D;

typedef struct VkViewport {
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
} VkViewport;

// Stub function declarations
#ifdef __cplusplus
extern "C" {
#endif

VkResult vkCreateInstance(void*, void*, VkInstance*);
VkResult vkCreateDevice(VkPhysicalDevice, void*, void*, VkDevice*);
void vkDestroyInstance(VkInstance, void*);
void vkDestroyDevice(VkDevice, void*);

#ifdef __cplusplus
}
#endif

#endif // VULKAN_STUBS_H
