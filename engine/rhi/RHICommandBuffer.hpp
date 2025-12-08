#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace CatEngine::RHI {

// Forward declarations
class IRHIBuffer;
class IRHITexture;
class IRHIPipeline;
class IRHIRenderPass;
class IRHIDescriptorSet;

/**
 * Abstract interface for command buffers
 * Records GPU commands for submission to queues
 */
class IRHICommandBuffer {
public:
    virtual ~IRHICommandBuffer() = default;

    /**
     * Begin recording commands
     */
    virtual void Begin() = 0;

    /**
     * End recording commands
     */
    virtual void End() = 0;

    /**
     * Reset the command buffer for reuse
     */
    virtual void Reset() = 0;

    // ========================================================================
    // Render Pass Commands
    // ========================================================================

    /**
     * Begin a render pass
     * @param renderPass The render pass to begin
     * @param framebuffer The framebuffer to render to (implementation-specific)
     * @param renderArea The render area
     * @param clearValues Clear values for attachments
     * @param clearValueCount Number of clear values
     */
    virtual void BeginRenderPass(
        IRHIRenderPass* renderPass,
        void* framebuffer,
        const Rect2D& renderArea,
        const ClearValue* clearValues,
        uint32_t clearValueCount
    ) = 0;

    /**
     * End the current render pass
     */
    virtual void EndRenderPass() = 0;

    /**
     * Advance to next subpass
     */
    virtual void NextSubpass() = 0;

    // ========================================================================
    // Pipeline Commands
    // ========================================================================

    /**
     * Bind a pipeline
     */
    virtual void BindPipeline(IRHIPipeline* pipeline) = 0;

    /**
     * Bind descriptor sets
     * @param bindPoint Pipeline bind point
     * @param layout Pipeline layout
     * @param firstSet First descriptor set index
     * @param descriptorSets Array of descriptor sets to bind
     * @param descriptorSetCount Number of descriptor sets
     * @param dynamicOffsets Array of dynamic offsets
     * @param dynamicOffsetCount Number of dynamic offsets
     */
    virtual void BindDescriptorSets(
        PipelineBindPoint bindPoint,
        class IRHIPipelineLayout* layout,
        uint32_t firstSet,
        IRHIDescriptorSet** descriptorSets,
        uint32_t descriptorSetCount,
        const uint32_t* dynamicOffsets = nullptr,
        uint32_t dynamicOffsetCount = 0
    ) = 0;

    /**
     * Set viewport
     */
    virtual void SetViewport(const Viewport& viewport) = 0;

    /**
     * Set scissor rectangle
     */
    virtual void SetScissor(const Rect2D& scissor) = 0;

    /**
     * Set line width
     */
    virtual void SetLineWidth(float width) = 0;

    /**
     * Set blend constants
     */
    virtual void SetBlendConstants(const float blendConstants[4]) = 0;

    /**
     * Set depth bias
     */
    virtual void SetDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;

    /**
     * Set depth bounds
     */
    virtual void SetDepthBounds(float minDepthBounds, float maxDepthBounds) = 0;

    // ========================================================================
    // Vertex/Index Buffer Commands
    // ========================================================================

    /**
     * Bind vertex buffers
     * @param firstBinding First binding index
     * @param buffers Array of vertex buffers
     * @param offsets Array of offsets into each buffer
     * @param bindingCount Number of bindings
     */
    virtual void BindVertexBuffers(
        uint32_t firstBinding,
        IRHIBuffer** buffers,
        const uint64_t* offsets,
        uint32_t bindingCount
    ) = 0;

    /**
     * Bind index buffer
     */
    virtual void BindIndexBuffer(IRHIBuffer* buffer, uint64_t offset, IndexType indexType) = 0;

    // ========================================================================
    // Draw Commands
    // ========================================================================

    /**
     * Draw primitives
     */
    virtual void Draw(
        uint32_t vertexCount,
        uint32_t instanceCount,
        uint32_t firstVertex,
        uint32_t firstInstance
    ) = 0;

    /**
     * Draw indexed primitives
     */
    virtual void DrawIndexed(
        uint32_t indexCount,
        uint32_t instanceCount,
        uint32_t firstIndex,
        int32_t vertexOffset,
        uint32_t firstInstance
    ) = 0;

    /**
     * Draw indirect
     */
    virtual void DrawIndirect(
        IRHIBuffer* buffer,
        uint64_t offset,
        uint32_t drawCount,
        uint32_t stride
    ) = 0;

    /**
     * Draw indexed indirect
     */
    virtual void DrawIndexedIndirect(
        IRHIBuffer* buffer,
        uint64_t offset,
        uint32_t drawCount,
        uint32_t stride
    ) = 0;

    // ========================================================================
    // Compute Commands
    // ========================================================================

    /**
     * Dispatch compute shader
     */
    virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    /**
     * Dispatch compute shader indirect
     */
    virtual void DispatchIndirect(IRHIBuffer* buffer, uint64_t offset) = 0;

    // ========================================================================
    // Copy Commands
    // ========================================================================

    /**
     * Copy between buffers
     */
    virtual void CopyBuffer(
        IRHIBuffer* srcBuffer,
        IRHIBuffer* dstBuffer,
        uint64_t srcOffset,
        uint64_t dstOffset,
        uint64_t size
    ) = 0;

    /**
     * Copy buffer to texture
     */
    virtual void CopyBufferToTexture(
        IRHIBuffer* srcBuffer,
        IRHITexture* dstTexture,
        uint32_t mipLevel,
        uint32_t arrayLayer,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) = 0;

    /**
     * Copy texture to buffer
     */
    virtual void CopyTextureToBuffer(
        IRHITexture* srcTexture,
        IRHIBuffer* dstBuffer,
        uint32_t mipLevel,
        uint32_t arrayLayer,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) = 0;

    /**
     * Copy between textures
     */
    virtual void CopyTexture(
        IRHITexture* srcTexture,
        IRHITexture* dstTexture,
        uint32_t srcMipLevel,
        uint32_t srcArrayLayer,
        uint32_t dstMipLevel,
        uint32_t dstArrayLayer,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) = 0;

    /**
     * Blit texture (with filtering)
     */
    virtual void BlitTexture(
        IRHITexture* srcTexture,
        IRHITexture* dstTexture,
        uint32_t srcMipLevel,
        uint32_t srcArrayLayer,
        uint32_t dstMipLevel,
        uint32_t dstArrayLayer,
        Filter filter
    ) = 0;

    // ========================================================================
    // Synchronization Commands
    // ========================================================================

    /**
     * Insert pipeline barrier
     * Implementation-specific - actual barrier mechanism depends on backend
     */
    virtual void PipelineBarrier() = 0;

    /**
     * Clear color attachment
     */
    virtual void ClearColorAttachment(
        uint32_t attachmentIndex,
        const ClearColorValue& clearValue,
        const Rect2D& rect
    ) = 0;

    /**
     * Clear depth stencil attachment
     */
    virtual void ClearDepthStencilAttachment(
        const ClearDepthStencilValue& clearValue,
        const Rect2D& rect
    ) = 0;
};

} // namespace CatEngine::RHI
