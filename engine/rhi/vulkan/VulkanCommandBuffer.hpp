#pragma once

#include "../RHICommandBuffer.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <mutex>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;
class VulkanBuffer;
class VulkanTexture;
class VulkanPipeline;
class VulkanRenderPass;
class VulkanFramebuffer;
class VulkanDescriptorSet;
class VulkanPipelineLayout;

/**
 * Vulkan command pool wrapper
 * Manages command buffer allocation for a specific thread
 */
class VulkanCommandPool {
public:
    VulkanCommandPool(VulkanDevice* device, uint32_t queueFamilyIndex);
    ~VulkanCommandPool();

    VulkanCommandPool(const VulkanCommandPool&) = delete;
    VulkanCommandPool& operator=(const VulkanCommandPool&) = delete;

    /**
     * Allocate a command buffer from the pool
     */
    VkCommandBuffer AllocateCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    /**
     * Free a command buffer back to the pool
     */
    void FreeCommandBuffer(VkCommandBuffer commandBuffer);

    /**
     * Reset the entire pool (frees all command buffers)
     */
    void Reset();

    VkCommandPool GetHandle() const { return m_commandPool; }

private:
    VulkanDevice* m_device;
    VkCommandPool m_commandPool;
    std::vector<VkCommandBuffer> m_allocatedBuffers;
    std::mutex m_mutex;
};

/**
 * Vulkan implementation of IRHICommandBuffer
 * Records GPU commands for submission to queues
 */
class VulkanCommandBuffer : public IRHICommandBuffer {
public:
    VulkanCommandBuffer(VulkanDevice* device, VulkanCommandPool* pool);
    ~VulkanCommandBuffer() override;

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;

    // ========================================================================
    // IRHICommandBuffer Interface
    // ========================================================================

    void Begin() override;
    void End() override;
    void Reset() override;

    // Render Pass Commands
    void BeginRenderPass(
        IRHIRenderPass* renderPass,
        void* framebuffer,
        const Rect2D& renderArea,
        const ClearValue* clearValues,
        uint32_t clearValueCount
    ) override;

    void EndRenderPass() override;
    void NextSubpass() override;

    // Pipeline Commands
    void BindPipeline(IRHIPipeline* pipeline) override;

    void BindDescriptorSets(
        PipelineBindPoint bindPoint,
        IRHIPipelineLayout* layout,
        uint32_t firstSet,
        IRHIDescriptorSet** descriptorSets,
        uint32_t descriptorSetCount,
        const uint32_t* dynamicOffsets,
        uint32_t dynamicOffsetCount
    ) override;

    void SetViewport(const Viewport& viewport) override;
    void SetScissor(const Rect2D& scissor) override;
    void SetLineWidth(float width) override;
    void SetBlendConstants(const float blendConstants[4]) override;
    void SetDepthBias(float constantFactor, float clamp, float slopeFactor) override;
    void SetDepthBounds(float minDepthBounds, float maxDepthBounds) override;

    // Vertex/Index Buffer Commands
    void BindVertexBuffers(
        uint32_t firstBinding,
        IRHIBuffer** buffers,
        const uint64_t* offsets,
        uint32_t bindingCount
    ) override;

    void BindIndexBuffer(IRHIBuffer* buffer, uint64_t offset, IndexType indexType) override;

    // Draw Commands
    void Draw(
        uint32_t vertexCount,
        uint32_t instanceCount,
        uint32_t firstVertex,
        uint32_t firstInstance
    ) override;

    void DrawIndexed(
        uint32_t indexCount,
        uint32_t instanceCount,
        uint32_t firstIndex,
        int32_t vertexOffset,
        uint32_t firstInstance
    ) override;

    void DrawIndirect(
        IRHIBuffer* buffer,
        uint64_t offset,
        uint32_t drawCount,
        uint32_t stride
    ) override;

    void DrawIndexedIndirect(
        IRHIBuffer* buffer,
        uint64_t offset,
        uint32_t drawCount,
        uint32_t stride
    ) override;

    // Compute Commands
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void DispatchIndirect(IRHIBuffer* buffer, uint64_t offset) override;

    // Copy Commands
    void CopyBuffer(
        IRHIBuffer* srcBuffer,
        IRHIBuffer* dstBuffer,
        uint64_t srcOffset,
        uint64_t dstOffset,
        uint64_t size
    ) override;

    void CopyBufferToTexture(
        IRHIBuffer* srcBuffer,
        IRHITexture* dstTexture,
        uint32_t mipLevel,
        uint32_t arrayLayer,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) override;

    void CopyTextureToBuffer(
        IRHITexture* srcTexture,
        IRHIBuffer* dstBuffer,
        uint32_t mipLevel,
        uint32_t arrayLayer,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) override;

    void CopyTexture(
        IRHITexture* srcTexture,
        IRHITexture* dstTexture,
        uint32_t srcMipLevel,
        uint32_t srcArrayLayer,
        uint32_t dstMipLevel,
        uint32_t dstArrayLayer,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) override;

    void BlitTexture(
        IRHITexture* srcTexture,
        IRHITexture* dstTexture,
        uint32_t srcMipLevel,
        uint32_t srcArrayLayer,
        uint32_t dstMipLevel,
        uint32_t dstArrayLayer,
        Filter filter
    ) override;

    // Synchronization Commands
    void PipelineBarrier() override;

    void ClearColorAttachment(
        uint32_t attachmentIndex,
        const ClearColorValue& clearValue,
        const Rect2D& rect
    ) override;

    void ClearDepthStencilAttachment(
        const ClearDepthStencilValue& clearValue,
        const Rect2D& rect
    ) override;

    // ========================================================================
    // Vulkan-specific Extensions
    // ========================================================================

    /**
     * Insert full pipeline barrier with memory and image barriers
     */
    void PipelineBarrierFull(
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask,
        VkDependencyFlags dependencyFlags,
        const VkMemoryBarrier* memoryBarriers,
        uint32_t memoryBarrierCount,
        const VkBufferMemoryBarrier* bufferBarriers,
        uint32_t bufferBarrierCount,
        const VkImageMemoryBarrier* imageBarriers,
        uint32_t imageBarrierCount
    );

    /**
     * Push constants to the pipeline
     */
    void PushConstants(
        VkPipelineLayout layout,
        VkShaderStageFlags stageFlags,
        uint32_t offset,
        uint32_t size,
        const void* data
    );

    VkCommandBuffer GetHandle() const { return m_commandBuffer; }

private:
    VulkanDevice* m_device;
    VulkanCommandPool* m_pool;
    VkCommandBuffer m_commandBuffer;
    bool m_recording;
};

} // namespace CatEngine::RHI
