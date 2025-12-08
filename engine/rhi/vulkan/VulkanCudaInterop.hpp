#pragma once

#include "../../cuda/CudaContext.hpp"
#include "../../cuda/CudaBuffer.hpp"
#include "../../cuda/CudaError.hpp"

#include <vulkan/vulkan.h>
#include <cuda_runtime.h>
#include <cudaVK.h>

#include <memory>
#include <vector>
#include <optional>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #define VK_USE_PLATFORM_WIN32_KHR
#else
    #include <unistd.h>
#endif

namespace CatEngine {
namespace RHI {

// Forward declarations
class VulkanDevice;
class VulkanBuffer;
class VulkanTexture;

/**
 * @brief Required Vulkan extensions for CUDA interop
 */
struct VulkanCudaExtensions {
    static constexpr const char* requiredDeviceExtensions[] = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#else
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#endif
    };

    static std::vector<const char*> getRequiredExtensions();
    static bool checkExtensionSupport(VkPhysicalDevice physicalDevice);
};

/**
 * @brief Helper functions for Vulkan-CUDA interoperability
 */
class VulkanCudaInterop {
public:
    /**
     * @brief Match CUDA device with Vulkan physical device by UUID
     * @param physicalDevice Vulkan physical device
     * @return CUDA device ID if match found, std::nullopt otherwise
     */
    static std::optional<int> matchCudaDevice(VkPhysicalDevice physicalDevice);

    /**
     * @brief Get external memory handle type for current platform
     */
    static VkExternalMemoryHandleTypeFlagBits getExternalMemoryHandleType();

    /**
     * @brief Get external semaphore handle type for current platform
     */
    static VkExternalSemaphoreHandleTypeFlagBits getExternalSemaphoreHandleType();

    /**
     * @brief Get CUDA external memory handle type for current platform
     */
    static cudaExternalMemoryHandleType getCudaExternalMemoryHandleType();

    /**
     * @brief Get CUDA external semaphore handle type for current platform
     */
    static cudaExternalSemaphoreHandleType getCudaExternalSemaphoreHandleType();

    /**
     * @brief Verify CUDA device supports external memory
     */
    static bool verifyCudaExternalMemorySupport(int cudaDeviceId);

private:
    static cudaUUID getVulkanDeviceUUID(VkPhysicalDevice physicalDevice);
};

/**
 * @brief Vulkan buffer exported to CUDA
 *
 * Creates a Vulkan buffer with external memory export capability,
 * then imports it into CUDA for direct GPU-GPU sharing.
 */
class CudaVulkanBuffer {
public:
    /**
     * @brief Create shared Vulkan-CUDA buffer
     * @param device Vulkan device
     * @param size Size in bytes
     * @param usage Vulkan buffer usage flags
     * @param cudaContext CUDA context for device matching
     */
    CudaVulkanBuffer(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        size_t size,
        VkBufferUsageFlags usage,
        const CUDA::CudaContext& cudaContext
    );

    ~CudaVulkanBuffer();

    // Non-copyable, movable
    CudaVulkanBuffer(const CudaVulkanBuffer&) = delete;
    CudaVulkanBuffer& operator=(const CudaVulkanBuffer&) = delete;
    CudaVulkanBuffer(CudaVulkanBuffer&& other) noexcept;
    CudaVulkanBuffer& operator=(CudaVulkanBuffer&& other) noexcept;

    /**
     * @brief Get Vulkan buffer handle
     */
    VkBuffer getVulkanBuffer() const { return m_buffer; }

    /**
     * @brief Get Vulkan device memory handle
     */
    VkDeviceMemory getVulkanMemory() const { return m_memory; }

    /**
     * @brief Get CUDA device pointer
     */
    void* getCudaDevicePtr() const { return m_cudaPtr; }

    /**
     * @brief Get typed CUDA device pointer
     */
    template<typename T>
    T* getCudaDevicePtr() const { return static_cast<T*>(m_cudaPtr); }

    /**
     * @brief Get buffer size in bytes
     */
    size_t getSize() const { return m_size; }

    /**
     * @brief Check if buffer is valid
     */
    bool isValid() const { return m_buffer != VK_NULL_HANDLE && m_cudaPtr != nullptr; }

private:
    void createVulkanBuffer(VkBufferUsageFlags usage);
    void allocateExternalMemory();
    void importToCuda();
    void cleanup();

    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkBuffer m_buffer;
    VkDeviceMemory m_memory;
    size_t m_size;

    cudaExternalMemory_t m_externalMemory;
    void* m_cudaPtr;

    const CUDA::CudaContext& m_cudaContext;
};

/**
 * @brief Vulkan image exported to CUDA
 *
 * Creates a Vulkan image with external memory export capability,
 * then imports it into CUDA as a cudaArray for texture/surface operations.
 */
class CudaVulkanImage {
public:
    /**
     * @brief Create shared Vulkan-CUDA image
     * @param device Vulkan device
     * @param physicalDevice Vulkan physical device
     * @param width Image width
     * @param height Image height
     * @param format Vulkan image format
     * @param usage Vulkan image usage flags
     * @param cudaContext CUDA context for device matching
     */
    CudaVulkanImage(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        const CUDA::CudaContext& cudaContext
    );

    ~CudaVulkanImage();

    // Non-copyable, movable
    CudaVulkanImage(const CudaVulkanImage&) = delete;
    CudaVulkanImage& operator=(const CudaVulkanImage&) = delete;
    CudaVulkanImage(CudaVulkanImage&& other) noexcept;
    CudaVulkanImage& operator=(CudaVulkanImage&& other) noexcept;

    /**
     * @brief Get Vulkan image handle
     */
    VkImage getVulkanImage() const { return m_image; }

    /**
     * @brief Get Vulkan device memory handle
     */
    VkDeviceMemory getVulkanMemory() const { return m_memory; }

    /**
     * @brief Get CUDA mipmapped array
     */
    cudaMipmappedArray_t getCudaMipmappedArray() const { return m_mipmappedArray; }

    /**
     * @brief Get CUDA array (level 0)
     */
    cudaArray_t getCudaArray() const { return m_array; }

    /**
     * @brief Get CUDA surface object for writing
     */
    cudaSurfaceObject_t getCudaSurface();

    /**
     * @brief Get CUDA texture object for reading
     */
    cudaTextureObject_t getCudaTexture();

    /**
     * @brief Get image dimensions
     */
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }
    VkFormat getFormat() const { return m_format; }

    /**
     * @brief Check if image is valid
     */
    bool isValid() const { return m_image != VK_NULL_HANDLE && m_array != nullptr; }

private:
    void createVulkanImage(VkImageUsageFlags usage);
    void allocateExternalMemory();
    void importToCuda();
    void createCudaSurface();
    void createCudaTexture();
    void cleanup();

    cudaChannelFormatDesc getCudaChannelFormat() const;

    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkImage m_image;
    VkDeviceMemory m_memory;
    uint32_t m_width;
    uint32_t m_height;
    VkFormat m_format;

    cudaExternalMemory_t m_externalMemory;
    cudaMipmappedArray_t m_mipmappedArray;
    cudaArray_t m_array;
    cudaSurfaceObject_t m_surfaceObject;
    cudaTextureObject_t m_textureObject;

    const CUDA::CudaContext& m_cudaContext;
};

/**
 * @brief Vulkan-CUDA semaphore synchronization
 *
 * Exports Vulkan semaphore to CUDA for GPU-GPU synchronization
 * without CPU involvement.
 */
class CudaVulkanSemaphore {
public:
    /**
     * @brief Create shared Vulkan-CUDA semaphore
     * @param device Vulkan device
     * @param cudaContext CUDA context
     */
    CudaVulkanSemaphore(VkDevice device, const CUDA::CudaContext& cudaContext);

    ~CudaVulkanSemaphore();

    // Non-copyable, movable
    CudaVulkanSemaphore(const CudaVulkanSemaphore&) = delete;
    CudaVulkanSemaphore& operator=(const CudaVulkanSemaphore&) = delete;
    CudaVulkanSemaphore(CudaVulkanSemaphore&& other) noexcept;
    CudaVulkanSemaphore& operator=(CudaVulkanSemaphore&& other) noexcept;

    /**
     * @brief Get Vulkan semaphore handle
     */
    VkSemaphore getVulkanSemaphore() const { return m_semaphore; }

    /**
     * @brief Get CUDA external semaphore
     */
    cudaExternalSemaphore_t getCudaSemaphore() const { return m_cudaSemaphore; }

    /**
     * @brief Wait for semaphore in CUDA stream
     * @param stream CUDA stream to wait in (0 for default stream)
     * @param value Semaphore value to wait for (for timeline semaphores)
     */
    void cudaWait(cudaStream_t stream = 0, uint64_t value = 0);

    /**
     * @brief Signal semaphore from CUDA stream
     * @param stream CUDA stream to signal from (0 for default stream)
     * @param value Semaphore value to signal (for timeline semaphores)
     */
    void cudaSignal(cudaStream_t stream = 0, uint64_t value = 0);

    /**
     * @brief Check if semaphore is valid
     */
    bool isValid() const { return m_semaphore != VK_NULL_HANDLE && m_cudaSemaphore != nullptr; }

private:
    void createVulkanSemaphore();
    void importToCuda();
    void cleanup();

    VkDevice m_device;
    VkSemaphore m_semaphore;
    cudaExternalSemaphore_t m_cudaSemaphore;

    const CUDA::CudaContext& m_cudaContext;
};

} // namespace RHI
} // namespace CatEngine
