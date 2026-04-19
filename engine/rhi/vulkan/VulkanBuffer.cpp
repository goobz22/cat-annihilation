#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace CatEngine::RHI {

// Helper to convert RHI buffer usage to Vulkan
static VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage) {
    VkBufferUsageFlags flags = 0;

    if (static_cast<uint32_t>(usage & BufferUsage::Vertex))
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (static_cast<uint32_t>(usage & BufferUsage::Index))
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (static_cast<uint32_t>(usage & BufferUsage::Uniform))
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (static_cast<uint32_t>(usage & BufferUsage::Storage))
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (static_cast<uint32_t>(usage & BufferUsage::TransferSrc))
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (static_cast<uint32_t>(usage & BufferUsage::TransferDst))
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (static_cast<uint32_t>(usage & BufferUsage::Indirect))
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    return flags;
}

// Helper to convert RHI memory properties to Vulkan
static VkMemoryPropertyFlags ToVkMemoryProperties(MemoryProperty props) {
    VkMemoryPropertyFlags flags = 0;

    if (static_cast<uint32_t>(props & MemoryProperty::DeviceLocal))
        flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (static_cast<uint32_t>(props & MemoryProperty::HostVisible))
        flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (static_cast<uint32_t>(props & MemoryProperty::HostCoherent))
        flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (static_cast<uint32_t>(props & MemoryProperty::HostCached))
        flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    if (static_cast<uint32_t>(props & MemoryProperty::LazilyAllocated))
        flags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;

    return flags;
}

VulkanBuffer::VulkanBuffer(VulkanDevice* device, const BufferDesc& desc)
    : m_Device(device)
    , m_Buffer(VK_NULL_HANDLE)
    , m_Memory(VK_NULL_HANDLE)
    , m_Size(desc.size)
    , m_Usage(desc.usage)
    , m_MemoryProperties(desc.memoryProperties)
    , m_DebugName(desc.debugName ? desc.debugName : "")
    , m_MappedData(nullptr)
    , m_IsPersistentlyMapped(false)
{
    if (m_Size == 0) {
        throw std::runtime_error("VulkanBuffer: Cannot create buffer with size 0");
    }

    CreateBuffer();
    AllocateMemory();

    // Automatically map host-visible buffers for convenience
    if (static_cast<uint32_t>(m_MemoryProperties & MemoryProperty::HostVisible)) {
        m_MappedData = Map();
        m_IsPersistentlyMapped = true;
    }
}

VulkanBuffer::~VulkanBuffer() {
    if (m_Device == nullptr) return;

    VkDevice device = m_Device->GetVkDevice();

    if (m_MappedData) {
        vkUnmapMemory(device, m_Memory);
        m_MappedData = nullptr;
    }

    if (m_Buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_Buffer, nullptr);
        m_Buffer = VK_NULL_HANDLE;
    }

    if (m_Memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_Memory, nullptr);
        m_Memory = VK_NULL_HANDLE;
    }
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : m_Device(other.m_Device)
    , m_Buffer(other.m_Buffer)
    , m_Memory(other.m_Memory)
    , m_Size(other.m_Size)
    , m_Usage(other.m_Usage)
    , m_MemoryProperties(other.m_MemoryProperties)
    , m_DebugName(std::move(other.m_DebugName))
    , m_MappedData(other.m_MappedData)
    , m_IsPersistentlyMapped(other.m_IsPersistentlyMapped)
{
    other.m_Device = nullptr;
    other.m_Buffer = VK_NULL_HANDLE;
    other.m_Memory = VK_NULL_HANDLE;
    other.m_MappedData = nullptr;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        this->~VulkanBuffer();

        // Move from other
        m_Device = other.m_Device;
        m_Buffer = other.m_Buffer;
        m_Memory = other.m_Memory;
        m_Size = other.m_Size;
        m_Usage = other.m_Usage;
        m_MemoryProperties = other.m_MemoryProperties;
        m_DebugName = std::move(other.m_DebugName);
        m_MappedData = other.m_MappedData;
        m_IsPersistentlyMapped = other.m_IsPersistentlyMapped;

        other.m_Device = nullptr;
        other.m_Buffer = VK_NULL_HANDLE;
        other.m_Memory = VK_NULL_HANDLE;
        other.m_MappedData = nullptr;
    }
    return *this;
}

void VulkanBuffer::CreateBuffer() {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_Size;
    bufferInfo.usage = ToVkBufferUsage(m_Usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreateBuffer(device, &bufferInfo, nullptr, &m_Buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanBuffer: Failed to create buffer");
    }

    // Set debug name if available
    if (!m_DebugName.empty() && m_Device->IsDebugUtilsSupported()) {
        m_Device->SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)m_Buffer, m_DebugName.c_str());
    }
}

void VulkanBuffer::AllocateMemory() {
    VkDevice device = m_Device->GetVkDevice();

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, m_Buffer, &memRequirements);

    VkMemoryPropertyFlags properties = ToVkMemoryProperties(m_MemoryProperties);
    uint32_t memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(device, &allocInfo, nullptr, &m_Memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanBuffer: Failed to allocate buffer memory");
    }

    // Bind buffer to memory
    result = vkBindBufferMemory(device, m_Buffer, m_Memory, 0);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanBuffer: Failed to bind buffer memory");
    }
}

uint32_t VulkanBuffer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_Device->GetVkPhysicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("VulkanBuffer: Failed to find suitable memory type");
}

void* VulkanBuffer::Map(uint64_t offset, uint64_t size) {
    if (!static_cast<uint32_t>(m_MemoryProperties & MemoryProperty::HostVisible)) {
        throw std::runtime_error("VulkanBuffer: Cannot map non-host-visible buffer");
    }

    if (m_MappedData && !m_IsPersistentlyMapped) {
        return m_MappedData; // Already mapped
    }

    VkDevice device = m_Device->GetVkDevice();
    VkDeviceSize mapSize = (size == 0) ? m_Size : size;

    void* data = nullptr;
    VkResult result = vkMapMemory(device, m_Memory, offset, mapSize, 0, &data);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanBuffer: Failed to map buffer memory");
    }

    if (!m_IsPersistentlyMapped) {
        m_MappedData = data;
    }

    return data;
}

void VulkanBuffer::Unmap() {
    if (!m_IsPersistentlyMapped && m_MappedData) {
        VkDevice device = m_Device->GetVkDevice();
        vkUnmapMemory(device, m_Memory);
        m_MappedData = nullptr;
    }
}

void VulkanBuffer::Flush(uint64_t offset, uint64_t size) {
    // Only needed for non-coherent memory
    if (!static_cast<uint32_t>(m_MemoryProperties & MemoryProperty::HostCoherent)) {
        VkDevice device = m_Device->GetVkDevice();

        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = m_Memory;
        range.offset = offset;
        range.size = (size == 0) ? VK_WHOLE_SIZE : size;

        vkFlushMappedMemoryRanges(device, 1, &range);
    }
}

void VulkanBuffer::Invalidate(uint64_t offset, uint64_t size) {
    // Only needed for non-coherent memory
    if (!static_cast<uint32_t>(m_MemoryProperties & MemoryProperty::HostCoherent)) {
        VkDevice device = m_Device->GetVkDevice();

        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = m_Memory;
        range.offset = offset;
        range.size = (size == 0) ? VK_WHOLE_SIZE : size;

        vkInvalidateMappedMemoryRanges(device, 1, &range);
    }
}

void VulkanBuffer::UpdateData(const void* data, uint64_t size, uint64_t offset) {
    if (!static_cast<uint32_t>(m_MemoryProperties & MemoryProperty::HostVisible)) {
        // Use staging buffer for device-local buffers
        CopyFromHostMemory(data, size, offset);
        return;
    }

    // Direct copy for host-visible buffers
    void* mapped = m_MappedData;
    bool needsUnmap = false;

    if (!mapped) {
        mapped = Map(offset, size);
        needsUnmap = true;
    }

    std::memcpy(static_cast<char*>(mapped) + offset, data, size);
    Flush(offset, size);

    if (needsUnmap) {
        Unmap();
    }
}

void VulkanBuffer::CopyFromHostMemory(const void* srcData, uint64_t size, uint64_t dstOffset) {
    // Create staging buffer
    BufferDesc stagingDesc{};
    stagingDesc.size = size;
    stagingDesc.usage = BufferUsage::TransferSrc;
    stagingDesc.memoryProperties = MemoryProperty::HostVisible | MemoryProperty::HostCoherent;
    stagingDesc.debugName = "StagingBuffer";

    VulkanBuffer stagingBuffer(m_Device, stagingDesc);
    stagingBuffer.UpdateData(srcData, size, 0);

    // VulkanDevice::CopyBuffer performs a one-time-submit on the graphics queue:
    // allocate + begin + vkCmdCopyBuffer + end + submit + vkQueueWaitIdle + free.
    // The staging buffer is destroyed when stagingBuffer goes out of scope, which
    // is safe because CopyBuffer waits for the queue to idle before returning.
    m_Device->CopyBuffer(stagingBuffer.GetVkBuffer(), m_Buffer, size, 0, dstOffset);
}

uint64_t VulkanBuffer::GetDeviceAddress() const {
    if (!m_Device->IsBufferDeviceAddressSupported()) {
        return 0;
    }

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = m_Buffer;

    return vkGetBufferDeviceAddress(m_Device->GetVkDevice(), &addressInfo);
}

} // namespace CatEngine::RHI
