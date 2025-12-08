#pragma once

#include "../RHI.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <optional>

namespace CatEngine::RHI::Vulkan {

/**
 * Queue family indices
 */
struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> compute;
    std::optional<uint32_t> transfer;

    bool IsComplete() const {
        return graphics.has_value() && compute.has_value() && transfer.has_value();
    }

    // Get unique queue family indices
    std::vector<uint32_t> GetUniqueIndices() const {
        std::vector<uint32_t> indices;
        if (graphics.has_value()) indices.push_back(*graphics);
        if (compute.has_value() && compute != graphics) indices.push_back(*compute);
        if (transfer.has_value() && transfer != graphics && transfer != compute)
            indices.push_back(*transfer);
        return indices;
    }
};

/**
 * Vulkan physical device and logical device management
 */
class VulkanDevice {
public:
    VulkanDevice() = default;
    ~VulkanDevice();

    // Disable copy
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    /**
     * Initialize device
     * @param instance Vulkan instance
     * @param surface Window surface (for presentation support)
     * @param enableValidation Enable validation layers
     * @return true on success
     */
    bool Initialize(VkInstance instance, VkSurfaceKHR surface, bool enableValidation);

    /**
     * Shutdown and cleanup device resources
     */
    void Shutdown();

    /**
     * Wait for device to be idle
     */
    void WaitIdle();

    // ========================================================================
    // Device Getters
    // ========================================================================

    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }

    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue GetComputeQueue() const { return m_computeQueue; }
    VkQueue GetTransferQueue() const { return m_transferQueue; }

    const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_queueFamilyIndices; }

    const VkPhysicalDeviceProperties& GetProperties() const { return m_deviceProperties; }
    const VkPhysicalDeviceFeatures& GetFeatures() const { return m_deviceFeatures; }
    const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return m_memoryProperties; }

    const DeviceLimits& GetRHILimits() const { return m_rhiLimits; }
    const DeviceFeatures& GetRHIFeatures() const { return m_rhiFeatures; }

    const char* GetDeviceName() const { return m_deviceProperties.deviceName; }
    const std::array<uint8_t, VK_UUID_SIZE>& GetDeviceUUID() const { return m_deviceUUID; }

    /**
     * Find suitable memory type index
     * @param typeFilter Memory type bits
     * @param properties Required memory properties
     * @return Memory type index or UINT32_MAX if not found
     */
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    /**
     * Query surface capabilities
     */
    VkSurfaceCapabilitiesKHR GetSurfaceCapabilities(VkSurfaceKHR surface) const;

    /**
     * Query surface formats
     */
    std::vector<VkSurfaceFormatKHR> GetSurfaceFormats(VkSurfaceKHR surface) const;

    /**
     * Query surface present modes
     */
    std::vector<VkPresentModeKHR> GetSurfacePresentModes(VkSurfaceKHR surface) const;

private:
    /**
     * Select best physical device
     */
    bool SelectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);

    /**
     * Check if device is suitable
     */
    bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);

    /**
     * Rate device suitability (higher is better)
     */
    int RateDeviceSuitability(VkPhysicalDevice device);

    /**
     * Find queue families
     */
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

    /**
     * Check device extension support
     */
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);

    /**
     * Create logical device
     */
    bool CreateLogicalDevice(bool enableValidation);

    /**
     * Query device properties and populate RHI structs
     */
    void QueryDeviceProperties();

    /**
     * Convert Vulkan limits to RHI limits
     */
    void PopulateRHILimits();

    /**
     * Convert Vulkan features to RHI features
     */
    void PopulateRHIFeatures();

    // Vulkan handles
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    // Queues
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;

    // Queue family indices
    QueueFamilyIndices m_queueFamilyIndices;

    // Device properties
    VkPhysicalDeviceProperties m_deviceProperties = {};
    VkPhysicalDeviceFeatures m_deviceFeatures = {};
    VkPhysicalDeviceMemoryProperties m_memoryProperties = {};
    std::array<uint8_t, VK_UUID_SIZE> m_deviceUUID = {};

    // RHI abstraction
    DeviceLimits m_rhiLimits = {};
    DeviceFeatures m_rhiFeatures = {};

    // Required device extensions
    static const std::vector<const char*> s_deviceExtensions;
};

} // namespace CatEngine::RHI::Vulkan
