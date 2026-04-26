#pragma once

#include "../RHI.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <optional>

namespace CatEngine::RHI {

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

    // Aliases for compatibility
    VkPhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetVkDevice() const { return m_device; }

    // Debug utilities
    bool IsDebugUtilsSupported() const { return m_debugUtilsSupported; }
    void SetObjectName(VkObjectType objectType, uint64_t objectHandle, const char* name) const;

    // Command buffer helpers
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1) const;
    void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                               uint32_t baseMipLevel, uint32_t levelCount,
                               uint32_t baseArrayLayer, uint32_t layerCount) const;
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                           uint32_t mipLevel, uint32_t arrayLayer) const;
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                    VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0) const;

    // Feature queries
    bool IsBufferDeviceAddressSupported() const { return m_bufferDeviceAddressSupported; }

    // CUDA-interop extension support.
    //
    // Reports whether every Vulkan device extension required by
    // `engine/rhi/vulkan/VulkanCudaInterop` (external-memory + external-
    // semaphore + the platform Win32/FD glue) was both present on the
    // physical device AND successfully enabled at logical-device creation.
    // Downstream code (e.g. ScenePass when allocating a ribbon-trail vertex
    // buffer, ParticleSystem when importing it as a CUDA device pointer)
    // gates on this flag so the engine still runs cleanly on GPUs / drivers
    // that don't advertise the extensions: in that case the renderer falls
    // back to the legacy CPU-fill path and CUDA never touches the buffer.
    //
    // WHY a runtime probe instead of a build-time switch: the CMake build
    // already links CUDA and the interop helper unconditionally (CUDA itself
    // is a hard requirement of the engine — the simulation, particles, and
    // physics pipelines all depend on it). What's optional is the *Vulkan*
    // side of the bridge — `VK_KHR_external_memory_win32` is widely supported
    // on NVIDIA + AMD on Windows but not guaranteed on every driver / IHV
    // (e.g. some Intel iGPU drivers ship without the win32 variant). A
    // runtime probe matches the engine's existing pattern for `samplerAnisotropy`
    // and `geometryShader` — query the device, populate a bool, let callers
    // decide whether to take the fast path or the fallback.
    bool IsCudaInteropSupported() const { return m_cudaInteropSupported; }

    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue GetComputeQueue() const { return m_computeQueue; }
    VkQueue GetTransferQueue() const { return m_transferQueue; }

    // The device owns exactly one short-lived-command VkCommandPool, created
    // on init and reused for every one-shot operation the device already
    // exposes (TransitionImageLayout, CopyBuffer, CopyBufferToImage). Tools
    // that need to dispatch their own one-shot work — notably the
    // swapchain readback path in Renderer::CaptureSwapchainToPPM, which has
    // to allocate a primary command buffer, record a copy-image-to-buffer,
    // submit to the graphics queue, and wait — need direct access to that
    // pool. Exposing it read-only (callers can allocate + free, they cannot
    // mutate the pool itself) is strictly less invasive than publishing
    // yet another helper wrapper for every new one-shot use case.
    //
    // NOTE for future maintainers: do NOT reset this pool while a frame is
    // in flight; the same pool is used by the renderer's per-frame command
    // buffer allocation path. All current callers (and any new callers)
    // must either (a) follow a vkQueueWaitIdle/vkDeviceWaitIdle before
    // reusing buffers from this pool or (b) allocate with the primary
    // command buffer pattern already used here and free immediately after
    // the one-shot submit completes.
    VkCommandPool GetCommandPool() const { return m_commandPool; }

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
     * Probe whether every CUDA-interop optional extension is advertised
     * by `device`. Pure introspection — does NOT enable anything; the
     * extensions only get added to the actual device-create extension
     * list (and the m_cudaInteropSupported flag flipped) when this
     * returns true. Kept const-on-state-of-this so we can call it from
     * SelectPhysicalDevice before any per-device member fields are set.
     */
    bool ProbeCudaInteropExtensions(VkPhysicalDevice device) const;

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

    // Debug utilities support
    bool m_debugUtilsSupported = false;

    // Buffer device address support
    bool m_bufferDeviceAddressSupported = false;

    // True when every CUDA-interop extension was advertised by the physical
    // device AND successfully enabled at logical-device creation. Set by
    // SelectPhysicalDevice's optional-extension probe and confirmed at the
    // end of CreateLogicalDevice (vkCreateDevice can still return
    // VK_ERROR_EXTENSION_NOT_PRESENT in pathological driver bug cases even
    // after the probe says yes — the latter half is what flips the flag
    // back to false if the actual create fails on the optional list, so
    // downstream code never reads "supported" while the extension wasn't
    // actually enabled on the live device).
    bool m_cudaInteropSupported = false;

    // Command pool for one-time commands
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    // Required device extensions
    static const std::vector<const char*> s_deviceExtensions;
};

} // namespace CatEngine::RHI
