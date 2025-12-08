#pragma once

#include "../RHIBuffer.hpp"
#include "../RHITypes.hpp"
#include <vulkan/vulkan.h>
#include <string>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;

/**
 * Vulkan implementation of IRHIBuffer
 * Manages VkBuffer and VkDeviceMemory with optional VMA support
 */
class VulkanBuffer : public IRHIBuffer {
public:
    VulkanBuffer(VulkanDevice* device, const BufferDesc& desc);
    ~VulkanBuffer() override;

    // Disable copy, allow move
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&&) noexcept;

    // IRHIBuffer interface
    uint64_t GetSize() const override { return m_Size; }
    BufferUsage GetUsage() const override { return m_Usage; }
    MemoryProperty GetMemoryProperties() const override { return m_MemoryProperties; }

    void* Map(uint64_t offset = 0, uint64_t size = 0) override;
    void Unmap() override;
    void Flush(uint64_t offset = 0, uint64_t size = 0) override;
    void Invalidate(uint64_t offset = 0, uint64_t size = 0) override;
    void UpdateData(const void* data, uint64_t size, uint64_t offset = 0) override;
    uint64_t GetDeviceAddress() const override;
    const char* GetDebugName() const override { return m_DebugName.c_str(); }

    // Vulkan-specific getters
    VkBuffer GetVkBuffer() const { return m_Buffer; }
    VkDeviceMemory GetVkDeviceMemory() const { return m_Memory; }
    bool IsMapped() const { return m_MappedData != nullptr; }

    /**
     * Copy data from another buffer using a staging buffer
     * @param srcData Source data to copy
     * @param size Size of data to copy
     * @param dstOffset Offset in destination buffer
     */
    void CopyFromHostMemory(const void* srcData, uint64_t size, uint64_t dstOffset = 0);

private:
    void CreateBuffer();
    void AllocateMemory();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

private:
    VulkanDevice* m_Device;
    VkBuffer m_Buffer;
    VkDeviceMemory m_Memory;

    uint64_t m_Size;
    BufferUsage m_Usage;
    MemoryProperty m_MemoryProperties;
    std::string m_DebugName;

    void* m_MappedData;
    bool m_IsPersistentlyMapped;
};

} // namespace CatEngine::RHI
