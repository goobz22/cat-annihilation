#include "VulkanSync.hpp"
#include <stdexcept>
#include <limits>

namespace CatEngine::RHI {

// Forward declare VulkanDevice interface we need
class VulkanDevice {
public:
    virtual VkDevice GetDevice() const = 0;
};

// ============================================================================
// VulkanFence Implementation
// ============================================================================

VulkanFence::VulkanFence(VulkanDevice* device, bool signaled)
    : m_device(device)
    , m_fence(VK_NULL_HANDLE) {

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

    if (vkCreateFence(m_device->GetDevice(), &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fence");
    }
}

VulkanFence::~VulkanFence() {
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device->GetDevice(), m_fence, nullptr);
    }
}

bool VulkanFence::Wait(uint64_t timeout) {
    VkResult result = vkWaitForFences(m_device->GetDevice(), 1, &m_fence, VK_TRUE, timeout);

    if (result == VK_SUCCESS) {
        return true;
    } else if (result == VK_TIMEOUT) {
        return false;
    } else {
        throw std::runtime_error("Failed to wait for fence");
    }
}

void VulkanFence::Reset() {
    if (vkResetFences(m_device->GetDevice(), 1, &m_fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reset fence");
    }
}

bool VulkanFence::IsSignaled() {
    VkResult result = vkGetFenceStatus(m_device->GetDevice(), m_fence);

    if (result == VK_SUCCESS) {
        return true;
    } else if (result == VK_NOT_READY) {
        return false;
    } else {
        throw std::runtime_error("Failed to get fence status");
    }
}

// ============================================================================
// VulkanSemaphore Implementation
// ============================================================================

VulkanSemaphore::VulkanSemaphore(VulkanDevice* device)
    : m_device(device)
    , m_semaphore(VK_NULL_HANDLE) {

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, &m_semaphore) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create semaphore");
    }
}

VulkanSemaphore::~VulkanSemaphore() {
    if (m_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device->GetDevice(), m_semaphore, nullptr);
    }
}

// ============================================================================
// VulkanTimelineSemaphore Implementation
// ============================================================================

VulkanTimelineSemaphore::VulkanTimelineSemaphore(VulkanDevice* device, uint64_t initialValue)
    : m_device(device)
    , m_semaphore(VK_NULL_HANDLE) {

    VkSemaphoreTypeCreateInfo timelineCreateInfo{};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.pNext = nullptr;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = initialValue;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &timelineCreateInfo;
    semaphoreInfo.flags = 0;

    if (vkCreateSemaphore(m_device->GetDevice(), &semaphoreInfo, nullptr, &m_semaphore) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create timeline semaphore");
    }
}

VulkanTimelineSemaphore::~VulkanTimelineSemaphore() {
    if (m_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device->GetDevice(), m_semaphore, nullptr);
    }
}

bool VulkanTimelineSemaphore::Wait(uint64_t value, uint64_t timeout) {
    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.pNext = nullptr;
    waitInfo.flags = 0;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_semaphore;
    waitInfo.pValues = &value;

    VkResult result = vkWaitSemaphores(m_device->GetDevice(), &waitInfo, timeout);

    if (result == VK_SUCCESS) {
        return true;
    } else if (result == VK_TIMEOUT) {
        return false;
    } else {
        throw std::runtime_error("Failed to wait for timeline semaphore");
    }
}

void VulkanTimelineSemaphore::Signal(uint64_t value) {
    VkSemaphoreSignalInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    signalInfo.pNext = nullptr;
    signalInfo.semaphore = m_semaphore;
    signalInfo.value = value;

    if (vkSignalSemaphore(m_device->GetDevice(), &signalInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to signal timeline semaphore");
    }
}

uint64_t VulkanTimelineSemaphore::GetCounterValue() {
    uint64_t value = 0;
    if (vkGetSemaphoreCounterValue(m_device->GetDevice(), m_semaphore, &value) != VK_SUCCESS) {
        throw std::runtime_error("Failed to get timeline semaphore counter value");
    }
    return value;
}

// ============================================================================
// VulkanSyncHelper Implementation
// ============================================================================

VkMemoryBarrier VulkanSyncHelper::CreateMemoryBarrier(
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask
) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    return barrier;
}

VkBufferMemoryBarrier VulkanSyncHelper::CreateBufferMemoryBarrier(
    VkBuffer buffer,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    uint64_t offset,
    uint64_t size,
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex
) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
    barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
    barrier.buffer = buffer;
    barrier.offset = offset;
    barrier.size = size;
    return barrier;
}

VkImageMemoryBarrier VulkanSyncHelper::CreateImageMemoryBarrier(
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask,
    uint32_t baseMipLevel,
    uint32_t levelCount,
    uint32_t baseArrayLayer,
    uint32_t layerCount,
    uint32_t srcQueueFamilyIndex,
    uint32_t dstQueueFamilyIndex
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
    barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
    barrier.image = image;
    barrier.subresourceRange = CreateImageSubresourceRange(
        aspectMask,
        baseMipLevel,
        levelCount,
        baseArrayLayer,
        layerCount
    );
    return barrier;
}

VkImageSubresourceRange VulkanSyncHelper::CreateImageSubresourceRange(
    VkImageAspectFlags aspectMask,
    uint32_t baseMipLevel,
    uint32_t levelCount,
    uint32_t baseArrayLayer,
    uint32_t layerCount
) {
    VkImageSubresourceRange range{};
    range.aspectMask = aspectMask;
    range.baseMipLevel = baseMipLevel;
    range.levelCount = levelCount;
    range.baseArrayLayer = baseArrayLayer;
    range.layerCount = layerCount;
    return range;
}

void VulkanSyncHelper::TransitionImageLayout(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask,
    VkPipelineStageFlags srcStage,
    VkPipelineStageFlags dstStage,
    uint32_t baseMipLevel,
    uint32_t levelCount,
    uint32_t baseArrayLayer,
    uint32_t layerCount
) {
    VkImageMemoryBarrier barrier = CreateImageMemoryBarrier(
        image,
        GetAccessFlagsForLayout(oldLayout),
        GetAccessFlagsForLayout(newLayout),
        oldLayout,
        newLayout,
        aspectMask,
        baseMipLevel,
        levelCount,
        baseArrayLayer,
        layerCount
    );

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

VkPipelineStageFlags VulkanSyncHelper::GetPipelineStageForLayout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

        default:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

VkAccessFlags VulkanSyncHelper::GetAccessFlagsForLayout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return 0;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;

        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return 0;

        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        default:
            return 0;
    }
}

} // namespace CatEngine::RHI
