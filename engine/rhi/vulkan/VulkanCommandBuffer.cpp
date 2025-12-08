#include "VulkanCommandBuffer.hpp"
#include "VulkanRenderPass.hpp"
#include "../RHIBuffer.hpp"
#include "../RHITexture.hpp"
#include "../RHIPipeline.hpp"
#include <stdexcept>
#include <cstring>

namespace CatEngine::RHI {

// Forward declare VulkanDevice interface we need
// In a real implementation, this would be in VulkanDevice.hpp
class VulkanDevice {
public:
    virtual VkDevice GetDevice() const = 0;
    virtual VkPhysicalDevice GetPhysicalDevice() const = 0;
    virtual uint32_t GetGraphicsQueueFamily() const = 0;
};

// Helper to get VkBuffer from IRHIBuffer
static VkBuffer GetVulkanBuffer(IRHIBuffer* buffer) {
    // In actual implementation, this would cast and extract the handle
    // For now, we'll assume the buffer has a GetHandle method
    return reinterpret_cast<VkBuffer>(buffer);
}

// Helper to get VkImage from IRHITexture
static VkImage GetVulkanImage(IRHITexture* texture) {
    return reinterpret_cast<VkImage>(texture);
}

// Helper to get VkPipeline from IRHIPipeline
static VkPipeline GetVulkanPipeline(IRHIPipeline* pipeline) {
    return reinterpret_cast<VkPipeline>(pipeline);
}

// Helper to convert index type
static VkIndexType ToVulkanIndexType(IndexType indexType) {
    switch (indexType) {
        case IndexType::UInt16: return VK_INDEX_TYPE_UINT16;
        case IndexType::UInt32: return VK_INDEX_TYPE_UINT32;
        default: return VK_INDEX_TYPE_UINT32;
    }
}

// Helper to convert filter
static VkFilter ToVulkanFilter(Filter filter) {
    switch (filter) {
        case Filter::Nearest: return VK_FILTER_NEAREST;
        case Filter::Linear: return VK_FILTER_LINEAR;
        default: return VK_FILTER_LINEAR;
    }
}

// Helper to convert pipeline bind point
static VkPipelineBindPoint ToVulkanPipelineBindPoint(PipelineBindPoint bindPoint) {
    switch (bindPoint) {
        case PipelineBindPoint::Graphics: return VK_PIPELINE_BIND_POINT_GRAPHICS;
        case PipelineBindPoint::Compute: return VK_PIPELINE_BIND_POINT_COMPUTE;
        default: return VK_PIPELINE_BIND_POINT_GRAPHICS;
    }
}

// ============================================================================
// VulkanCommandPool Implementation
// ============================================================================

VulkanCommandPool::VulkanCommandPool(VulkanDevice* device, uint32_t queueFamilyIndex)
    : m_device(device)
    , m_commandPool(VK_NULL_HANDLE) {

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device->GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

VulkanCommandPool::~VulkanCommandPool() {
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device->GetDevice(), m_commandPool, nullptr);
    }
}

VkCommandBuffer VulkanCommandPool::AllocateCommandBuffer(VkCommandBufferLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = level;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(m_device->GetDevice(), &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffer");
    }

    m_allocatedBuffers.push_back(commandBuffer);
    return commandBuffer;
}

void VulkanCommandPool::FreeCommandBuffer(VkCommandBuffer commandBuffer) {
    std::lock_guard<std::mutex> lock(m_mutex);

    vkFreeCommandBuffers(m_device->GetDevice(), m_commandPool, 1, &commandBuffer);

    auto it = std::find(m_allocatedBuffers.begin(), m_allocatedBuffers.end(), commandBuffer);
    if (it != m_allocatedBuffers.end()) {
        m_allocatedBuffers.erase(it);
    }
}

void VulkanCommandPool::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    vkResetCommandPool(m_device->GetDevice(), m_commandPool, 0);
}

// ============================================================================
// VulkanCommandBuffer Implementation
// ============================================================================

VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevice* device, VulkanCommandPool* pool)
    : m_device(device)
    , m_pool(pool)
    , m_commandBuffer(VK_NULL_HANDLE)
    , m_recording(false) {

    m_commandBuffer = m_pool->AllocateCommandBuffer();
}

VulkanCommandBuffer::~VulkanCommandBuffer() {
    if (m_commandBuffer != VK_NULL_HANDLE) {
        m_pool->FreeCommandBuffer(m_commandBuffer);
    }
}

void VulkanCommandBuffer::Begin() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    m_recording = true;
}

void VulkanCommandBuffer::End() {
    if (!m_recording) {
        throw std::runtime_error("Command buffer is not in recording state");
    }

    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to end recording command buffer");
    }

    m_recording = false;
}

void VulkanCommandBuffer::Reset() {
    vkResetCommandBuffer(m_commandBuffer, 0);
    m_recording = false;
}

// ============================================================================
// Render Pass Commands
// ============================================================================

void VulkanCommandBuffer::BeginRenderPass(
    IRHIRenderPass* renderPass,
    void* framebuffer,
    const Rect2D& renderArea,
    const ClearValue* clearValues,
    uint32_t clearValueCount
) {
    VulkanRenderPass* vkRenderPass = static_cast<VulkanRenderPass*>(renderPass);
    VkFramebuffer vkFramebuffer = static_cast<VkFramebuffer>(framebuffer);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vkRenderPass->GetHandle();
    renderPassInfo.framebuffer = vkFramebuffer;
    renderPassInfo.renderArea.offset = {renderArea.x, renderArea.y};
    renderPassInfo.renderArea.extent = {renderArea.width, renderArea.height};

    // Convert clear values
    std::vector<VkClearValue> vkClearValues(clearValueCount);
    for (uint32_t i = 0; i < clearValueCount; ++i) {
        std::memcpy(&vkClearValues[i], &clearValues[i], sizeof(VkClearValue));
    }

    renderPassInfo.clearValueCount = clearValueCount;
    renderPassInfo.pClearValues = vkClearValues.data();

    vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanCommandBuffer::EndRenderPass() {
    vkCmdEndRenderPass(m_commandBuffer);
}

void VulkanCommandBuffer::NextSubpass() {
    vkCmdNextSubpass(m_commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
}

// ============================================================================
// Pipeline Commands
// ============================================================================

void VulkanCommandBuffer::BindPipeline(IRHIPipeline* pipeline) {
    VkPipeline vkPipeline = GetVulkanPipeline(pipeline);
    // Assume graphics pipeline for now - could be extended with pipeline type query
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
}

void VulkanCommandBuffer::BindDescriptorSets(
    PipelineBindPoint bindPoint,
    IRHIPipelineLayout* layout,
    uint32_t firstSet,
    IRHIDescriptorSet** descriptorSets,
    uint32_t descriptorSetCount,
    const uint32_t* dynamicOffsets,
    uint32_t dynamicOffsetCount
) {
    VkPipelineLayout vkLayout = reinterpret_cast<VkPipelineLayout>(layout);

    std::vector<VkDescriptorSet> vkDescriptorSets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        vkDescriptorSets[i] = reinterpret_cast<VkDescriptorSet>(descriptorSets[i]);
    }

    vkCmdBindDescriptorSets(
        m_commandBuffer,
        ToVulkanPipelineBindPoint(bindPoint),
        vkLayout,
        firstSet,
        descriptorSetCount,
        vkDescriptorSets.data(),
        dynamicOffsetCount,
        dynamicOffsets
    );
}

void VulkanCommandBuffer::SetViewport(const Viewport& viewport) {
    VkViewport vkViewport{};
    vkViewport.x = viewport.x;
    vkViewport.y = viewport.y;
    vkViewport.width = viewport.width;
    vkViewport.height = viewport.height;
    vkViewport.minDepth = viewport.minDepth;
    vkViewport.maxDepth = viewport.maxDepth;

    vkCmdSetViewport(m_commandBuffer, 0, 1, &vkViewport);
}

void VulkanCommandBuffer::SetScissor(const Rect2D& scissor) {
    VkRect2D vkScissor{};
    vkScissor.offset = {scissor.x, scissor.y};
    vkScissor.extent = {scissor.width, scissor.height};

    vkCmdSetScissor(m_commandBuffer, 0, 1, &vkScissor);
}

void VulkanCommandBuffer::SetLineWidth(float width) {
    vkCmdSetLineWidth(m_commandBuffer, width);
}

void VulkanCommandBuffer::SetBlendConstants(const float blendConstants[4]) {
    vkCmdSetBlendConstants(m_commandBuffer, blendConstants);
}

void VulkanCommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor) {
    vkCmdSetDepthBias(m_commandBuffer, constantFactor, clamp, slopeFactor);
}

void VulkanCommandBuffer::SetDepthBounds(float minDepthBounds, float maxDepthBounds) {
    vkCmdSetDepthBounds(m_commandBuffer, minDepthBounds, maxDepthBounds);
}

// ============================================================================
// Vertex/Index Buffer Commands
// ============================================================================

void VulkanCommandBuffer::BindVertexBuffers(
    uint32_t firstBinding,
    IRHIBuffer** buffers,
    const uint64_t* offsets,
    uint32_t bindingCount
) {
    std::vector<VkBuffer> vkBuffers(bindingCount);
    std::vector<VkDeviceSize> vkOffsets(bindingCount);

    for (uint32_t i = 0; i < bindingCount; ++i) {
        vkBuffers[i] = GetVulkanBuffer(buffers[i]);
        vkOffsets[i] = offsets[i];
    }

    vkCmdBindVertexBuffers(m_commandBuffer, firstBinding, bindingCount, vkBuffers.data(), vkOffsets.data());
}

void VulkanCommandBuffer::BindIndexBuffer(IRHIBuffer* buffer, uint64_t offset, IndexType indexType) {
    VkBuffer vkBuffer = GetVulkanBuffer(buffer);
    vkCmdBindIndexBuffer(m_commandBuffer, vkBuffer, offset, ToVulkanIndexType(indexType));
}

// ============================================================================
// Draw Commands
// ============================================================================

void VulkanCommandBuffer::Draw(
    uint32_t vertexCount,
    uint32_t instanceCount,
    uint32_t firstVertex,
    uint32_t firstInstance
) {
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandBuffer::DrawIndexed(
    uint32_t indexCount,
    uint32_t instanceCount,
    uint32_t firstIndex,
    int32_t vertexOffset,
    uint32_t firstInstance
) {
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandBuffer::DrawIndirect(
    IRHIBuffer* buffer,
    uint64_t offset,
    uint32_t drawCount,
    uint32_t stride
) {
    VkBuffer vkBuffer = GetVulkanBuffer(buffer);
    vkCmdDrawIndirect(m_commandBuffer, vkBuffer, offset, drawCount, stride);
}

void VulkanCommandBuffer::DrawIndexedIndirect(
    IRHIBuffer* buffer,
    uint64_t offset,
    uint32_t drawCount,
    uint32_t stride
) {
    VkBuffer vkBuffer = GetVulkanBuffer(buffer);
    vkCmdDrawIndexedIndirect(m_commandBuffer, vkBuffer, offset, drawCount, stride);
}

// ============================================================================
// Compute Commands
// ============================================================================

void VulkanCommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandBuffer::DispatchIndirect(IRHIBuffer* buffer, uint64_t offset) {
    VkBuffer vkBuffer = GetVulkanBuffer(buffer);
    vkCmdDispatchIndirect(m_commandBuffer, vkBuffer, offset);
}

// ============================================================================
// Copy Commands
// ============================================================================

void VulkanCommandBuffer::CopyBuffer(
    IRHIBuffer* srcBuffer,
    IRHIBuffer* dstBuffer,
    uint64_t srcOffset,
    uint64_t dstOffset,
    uint64_t size
) {
    VkBuffer vkSrcBuffer = GetVulkanBuffer(srcBuffer);
    VkBuffer vkDstBuffer = GetVulkanBuffer(dstBuffer);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;

    vkCmdCopyBuffer(m_commandBuffer, vkSrcBuffer, vkDstBuffer, 1, &copyRegion);
}

void VulkanCommandBuffer::CopyBufferToTexture(
    IRHIBuffer* srcBuffer,
    IRHITexture* dstTexture,
    uint32_t mipLevel,
    uint32_t arrayLayer,
    uint32_t width,
    uint32_t height,
    uint32_t depth
) {
    VkBuffer vkBuffer = GetVulkanBuffer(srcBuffer);
    VkImage vkImage = GetVulkanImage(dstTexture);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, depth};

    vkCmdCopyBufferToImage(
        m_commandBuffer,
        vkBuffer,
        vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
}

void VulkanCommandBuffer::CopyTextureToBuffer(
    IRHITexture* srcTexture,
    IRHIBuffer* dstBuffer,
    uint32_t mipLevel,
    uint32_t arrayLayer,
    uint32_t width,
    uint32_t height,
    uint32_t depth
) {
    VkImage vkImage = GetVulkanImage(srcTexture);
    VkBuffer vkBuffer = GetVulkanBuffer(dstBuffer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, depth};

    vkCmdCopyImageToBuffer(
        m_commandBuffer,
        vkImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vkBuffer,
        1,
        &region
    );
}

void VulkanCommandBuffer::CopyTexture(
    IRHITexture* srcTexture,
    IRHITexture* dstTexture,
    uint32_t srcMipLevel,
    uint32_t srcArrayLayer,
    uint32_t dstMipLevel,
    uint32_t dstArrayLayer,
    uint32_t width,
    uint32_t height,
    uint32_t depth
) {
    VkImage vkSrcImage = GetVulkanImage(srcTexture);
    VkImage vkDstImage = GetVulkanImage(dstTexture);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = srcMipLevel;
    region.srcSubresource.baseArrayLayer = srcArrayLayer;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = {0, 0, 0};
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = dstMipLevel;
    region.dstSubresource.baseArrayLayer = dstArrayLayer;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = {0, 0, 0};
    region.extent = {width, height, depth};

    vkCmdCopyImage(
        m_commandBuffer,
        vkSrcImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vkDstImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
}

void VulkanCommandBuffer::BlitTexture(
    IRHITexture* srcTexture,
    IRHITexture* dstTexture,
    uint32_t srcMipLevel,
    uint32_t srcArrayLayer,
    uint32_t dstMipLevel,
    uint32_t dstArrayLayer,
    Filter filter
) {
    VkImage vkSrcImage = GetVulkanImage(srcTexture);
    VkImage vkDstImage = GetVulkanImage(dstTexture);

    // Get texture dimensions (would need proper implementation)
    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.mipLevel = srcMipLevel;
    blitRegion.srcSubresource.baseArrayLayer = srcArrayLayer;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[0] = {0, 0, 0};
    blitRegion.srcOffsets[1] = {1, 1, 1}; // Would get from texture
    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.mipLevel = dstMipLevel;
    blitRegion.dstSubresource.baseArrayLayer = dstArrayLayer;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[0] = {0, 0, 0};
    blitRegion.dstOffsets[1] = {1, 1, 1}; // Would get from texture

    vkCmdBlitImage(
        m_commandBuffer,
        vkSrcImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vkDstImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
        ToVulkanFilter(filter)
    );
}

// ============================================================================
// Synchronization Commands
// ============================================================================

void VulkanCommandBuffer::PipelineBarrier() {
    // Simple barrier - full memory barrier
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(
        m_commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        1,
        &barrier,
        0,
        nullptr,
        0,
        nullptr
    );
}

void VulkanCommandBuffer::ClearColorAttachment(
    uint32_t attachmentIndex,
    const ClearColorValue& clearValue,
    const Rect2D& rect
) {
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment = attachmentIndex;
    std::memcpy(&attachment.clearValue.color, &clearValue, sizeof(VkClearColorValue));

    VkClearRect clearRect{};
    clearRect.rect.offset = {rect.x, rect.y};
    clearRect.rect.extent = {rect.width, rect.height};
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    vkCmdClearAttachments(m_commandBuffer, 1, &attachment, 1, &clearRect);
}

void VulkanCommandBuffer::ClearDepthStencilAttachment(
    const ClearDepthStencilValue& clearValue,
    const Rect2D& rect
) {
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    attachment.clearValue.depthStencil.depth = clearValue.depth;
    attachment.clearValue.depthStencil.stencil = clearValue.stencil;

    VkClearRect clearRect{};
    clearRect.rect.offset = {rect.x, rect.y};
    clearRect.rect.extent = {rect.width, rect.height};
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    vkCmdClearAttachments(m_commandBuffer, 1, &attachment, 1, &clearRect);
}

// ============================================================================
// Vulkan-specific Extensions
// ============================================================================

void VulkanCommandBuffer::PipelineBarrierFull(
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkDependencyFlags dependencyFlags,
    const VkMemoryBarrier* memoryBarriers,
    uint32_t memoryBarrierCount,
    const VkBufferMemoryBarrier* bufferBarriers,
    uint32_t bufferBarrierCount,
    const VkImageMemoryBarrier* imageBarriers,
    uint32_t imageBarrierCount
) {
    vkCmdPipelineBarrier(
        m_commandBuffer,
        srcStageMask,
        dstStageMask,
        dependencyFlags,
        memoryBarrierCount,
        memoryBarriers,
        bufferBarrierCount,
        bufferBarriers,
        imageBarrierCount,
        imageBarriers
    );
}

void VulkanCommandBuffer::PushConstants(
    VkPipelineLayout layout,
    VkShaderStageFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void* data
) {
    vkCmdPushConstants(m_commandBuffer, layout, stageFlags, offset, size, data);
}

} // namespace CatEngine::RHI
