#pragma once

#include <vulkan/vulkan.h>
#include <string>

namespace CatEngine::RHI::Vulkan {

/**
 * Debug severity levels for filtering
 */
enum class DebugSeverity {
    Verbose = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

/**
 * Vulkan debug messenger utilities
 * Handles validation layer callbacks and object naming
 */
class VulkanDebug {
public:
    /**
     * Create debug messenger for validation layers
     * @param instance Vulkan instance
     * @param minSeverity Minimum severity level to log
     * @return Debug messenger handle
     */
    static VkDebugUtilsMessengerEXT CreateDebugMessenger(
        VkInstance instance,
        DebugSeverity minSeverity = DebugSeverity::Warning
    );

    /**
     * Destroy debug messenger
     * @param instance Vulkan instance
     * @param messenger Debug messenger handle
     */
    static void DestroyDebugMessenger(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger
    );

    /**
     * Set debug name for a Vulkan object
     * @param device Logical device
     * @param objectType Type of the object
     * @param objectHandle Handle to the object
     * @param name Debug name
     */
    static void SetObjectName(
        VkDevice device,
        VkObjectType objectType,
        uint64_t objectHandle,
        const char* name
    );

    /**
     * Set debug name for a Vulkan object (templated version)
     */
    template<typename T>
    static void SetObjectName(VkDevice device, T objectHandle, const char* name) {
        VkObjectType objectType = GetObjectType<T>();
        SetObjectName(device, objectType, reinterpret_cast<uint64_t>(objectHandle), name);
    }

    /**
     * Get minimum severity level
     */
    static DebugSeverity GetMinSeverity() { return s_minSeverity; }

private:
    /**
     * Debug messenger callback
     */
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    );

    /**
     * Get VkObjectType from handle type
     */
    template<typename T>
    static VkObjectType GetObjectType();

    static DebugSeverity s_minSeverity;
};

// Template specializations for GetObjectType
template<> inline VkObjectType VulkanDebug::GetObjectType<VkInstance>() { return VK_OBJECT_TYPE_INSTANCE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkPhysicalDevice>() { return VK_OBJECT_TYPE_PHYSICAL_DEVICE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkDevice>() { return VK_OBJECT_TYPE_DEVICE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkQueue>() { return VK_OBJECT_TYPE_QUEUE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkSemaphore>() { return VK_OBJECT_TYPE_SEMAPHORE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkFence>() { return VK_OBJECT_TYPE_FENCE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkDeviceMemory>() { return VK_OBJECT_TYPE_DEVICE_MEMORY; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkBuffer>() { return VK_OBJECT_TYPE_BUFFER; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkImage>() { return VK_OBJECT_TYPE_IMAGE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkImageView>() { return VK_OBJECT_TYPE_IMAGE_VIEW; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkShaderModule>() { return VK_OBJECT_TYPE_SHADER_MODULE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkPipeline>() { return VK_OBJECT_TYPE_PIPELINE; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkPipelineLayout>() { return VK_OBJECT_TYPE_PIPELINE_LAYOUT; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkRenderPass>() { return VK_OBJECT_TYPE_RENDER_PASS; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkFramebuffer>() { return VK_OBJECT_TYPE_FRAMEBUFFER; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkDescriptorSetLayout>() { return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkDescriptorPool>() { return VK_OBJECT_TYPE_DESCRIPTOR_POOL; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkDescriptorSet>() { return VK_OBJECT_TYPE_DESCRIPTOR_SET; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkCommandPool>() { return VK_OBJECT_TYPE_COMMAND_POOL; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkCommandBuffer>() { return VK_OBJECT_TYPE_COMMAND_BUFFER; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkSwapchainKHR>() { return VK_OBJECT_TYPE_SWAPCHAIN_KHR; }
template<> inline VkObjectType VulkanDebug::GetObjectType<VkSurfaceKHR>() { return VK_OBJECT_TYPE_SURFACE_KHR; }

} // namespace CatEngine::RHI::Vulkan
